#include "job_pool.h"

#include <algorithm>

JobPool::JobPool(int workers) : worker_count_(workers < 1 ? 1 : workers) {
	if (worker_count_ == 1) return;  // one worker is the caller: no threads are created
	threads_.reserve((size_t)(worker_count_ - 1));
	for (int i = 0; i < worker_count_ - 1; ++i)
		threads_.emplace_back([this] { worker_loop(); });
}

JobPool::~JobPool() {
	{
		std::lock_guard<std::mutex> lock(mutex_);
		stop_ = true;
	}
	wake_.notify_all();
	for (std::thread& t : threads_) t.join();
}

// Efficiency cores count too: on the M1 (4P + 4E) bench_world's tick measured 3.70 ms with 4
// workers and 3.12 ms with 7. The cap bounds the wake-up handshake on wide machines.
int JobPool::default_workers() {
	const int hw = (int)std::thread::hardware_concurrency();
	const int spare = hw > 0 ? hw - 1 : 1;  // hardware_concurrency() may report 0
	return std::max(1, std::min(8, spare));
}

// The generation handshake is the wake/sleep scheme: the caller bumps `generation_` and
// notifies, sleeping workers see a new generation, claim indices until the cursor runs past
// `job_count_`, then decrement `pending_`; the caller participates in the same loop and
// waits for `pending_ == 0`, which every worker decrements *after* its last callback
// returned. So the wait cannot be satisfied while callbacks are still running.
void JobPool::run_current_job() {
	for (;;) {
		const int i = next_.fetch_add(1, std::memory_order_relaxed);
		if (i >= job_count_) return;
		(*job_)(i);
	}
}

void JobPool::worker_loop() {
	uint64_t seen = 0;
	std::unique_lock<std::mutex> lock(mutex_);
	for (;;) {
		wake_.wait(lock, [&] { return stop_ || generation_ != seen; });
		if (stop_) return;
		seen = generation_;
		lock.unlock();
		run_current_job();
		lock.lock();
		if (--pending_ == 0) done_.notify_one();
	}
}

void JobPool::parallel_for(int count, const std::function<void(int)>& fn) {
	if (count <= 0) return;
	if (worker_count_ == 1) {  // inline: no handshake, no threads
		for (int i = 0; i < count; ++i) fn(i);
		return;
	}
	{
		std::lock_guard<std::mutex> lock(mutex_);
		job_ = &fn;
		job_count_ = count;
		next_.store(0, std::memory_order_relaxed);
		pending_ = worker_count_ - 1;
		++generation_;
	}
	wake_.notify_all();
	run_current_job();
	std::unique_lock<std::mutex> lock(mutex_);
	done_.wait(lock, [&] { return pending_ == 0; });
}
