#include "chunk_streamer.h"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <utility>

#ifdef __APPLE__
#include <pthread/qos.h>
#elif defined(__linux__)
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace {

// Background generation must never compete with the frame's own work, so the workers ask the
// OS for the lowest useful class instead of merely being outnumbered.
void lower_thread_priority() {
#ifdef __APPLE__
	pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#elif defined(__linux__)
	// Per-thread priority: gettid(), not the process-wide getpid().
	setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 10);
#endif
}

}  // namespace

ChunkStreamer::ChunkStreamer(const ChunkGenerator& gen, uint32_t seed, int threads)
    : gen_(gen), seed_(seed) {
	const int count = threads < 1 ? 1 : threads;
	threads_.reserve((size_t)count);
	for (int i = 0; i < count; ++i) threads_.emplace_back([this] { worker_loop(); });
}

ChunkStreamer::~ChunkStreamer() {
	{
		std::lock_guard<std::mutex> lock(mutex_);
		stop_ = true;
	}
	work_.notify_all();
	for (std::thread& t : threads_) t.join();
}

int ChunkStreamer::pick_locked() const {
	// ponytail: linear nearest-pick over <= ~400 coords; a priority structure if it shows up in
	// a profile.
	int best = -1;
	int64_t best_dist = 0;
	for (size_t i = 0; i < req_.size(); ++i) {
		if (req_[i].state != State::Queued) continue;
		const int64_t dx = req_[i].coord.x - centre_.x;
		const int64_t dy = req_[i].coord.y - centre_.y;
		const int64_t dist = dx * dx + dy * dy;
		if (best < 0 || dist < best_dist) {
			best = (int)i;
			best_dist = dist;
		}
	}
	return best;
}

bool ChunkStreamer::idle_locked() const {
	for (const Entry& e : req_)
		if (e.state != State::Ready) return false;
	return true;
}

void ChunkStreamer::worker_loop() {
	lower_thread_priority();
	std::unique_lock<std::mutex> lock(mutex_);
	for (;;) {
		work_.wait(lock, [&] { return stop_ || pick_locked() >= 0; });
		if (stop_) return;
		const int index = pick_locked();
		req_[index].state = State::InFlight;
		const ChunkCoord coord = req_[index].coord;
		lock.unlock();

		const auto start = std::chrono::steady_clock::now();
		std::unique_ptr<Chunk> chunk = make_chunk(coord, gen_, seed_);
		const double ms =
		    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
		        .count();

		lock.lock();
		if (stop_) return;  // shutting down: this result is dropped
		stats_.generated += 1;
		stats_.gen_ms_total += ms;
		// The coord may have left the request while it generated; then the result is dropped.
		for (Entry& e : req_) {
			if (e.state == State::InFlight && e.coord == coord) {
				e.state = State::Ready;
				e.ready = std::move(chunk);
				break;
			}
		}
		if (idle_locked()) drained_.notify_all();
	}
}

void ChunkStreamer::request(const std::vector<ChunkCoord>& missing, ChunkCoord centre) {
	std::lock_guard<std::mutex> lock(mutex_);
	centre_ = centre;

	std::set<int64_t> missing_keys;
	for (ChunkCoord c : missing) missing_keys.insert(chunk_key(c));

	// A delivered chunk the caller no longer asks for has been inserted: forget it.
	for (auto it = delivered_.begin(); it != delivered_.end();)
		it = missing_keys.count(*it) ? std::next(it) : delivered_.erase(it);

	// Keep the entries still wanted (queued, in flight, or ready); everything else is dropped.
	std::vector<Entry> next;
	next.reserve(missing.size());
	std::set<int64_t> have;
	for (Entry& e : req_) {
		const int64_t key = chunk_key(e.coord);
		if (!missing_keys.count(key)) continue;
		have.insert(key);
		next.push_back(std::move(e));
	}
	for (ChunkCoord c : missing) {
		const int64_t key = chunk_key(c);
		if (delivered_.count(key) || have.count(key)) continue;
		have.insert(key);
		next.push_back(Entry{c, State::Queued, nullptr});
	}
	req_ = std::move(next);
	work_.notify_all();
}

std::vector<std::unique_ptr<Chunk>> ChunkStreamer::take_ready(int max) {
	std::lock_guard<std::mutex> lock(mutex_);
	std::vector<std::unique_ptr<Chunk>> out;
	for (auto it = req_.begin(); it != req_.end() && (int)out.size() < max;) {
		if (it->state != State::Ready) {
			++it;
			continue;
		}
		delivered_.insert(chunk_key(it->coord));
		out.push_back(std::move(it->ready));
		it = req_.erase(it);
	}
	return out;
}

void ChunkStreamer::wait_idle() {
	std::unique_lock<std::mutex> lock(mutex_);
	drained_.wait(lock, [&] { return idle_locked(); });
}

int ChunkStreamer::pending() const {
	std::lock_guard<std::mutex> lock(mutex_);
	int n = 0;
	for (const Entry& e : req_)
		if (e.state != State::Ready) ++n;
	return n;
}

StreamStats ChunkStreamer::take_stats() {
	std::lock_guard<std::mutex> lock(mutex_);
	const StreamStats stats = stats_;
	stats_ = StreamStats();
	return stats;
}
