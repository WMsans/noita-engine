#include "simulator.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <vector>

#include "job_pool.h"
#include "sim_rules.h"

namespace {
using Clock = std::chrono::steady_clock;

// Simulation order: chunk rows bottom-up, so a chunk runs after the three chunks of the row
// below it and a falling cell finds the space its neighbour vacated this tick, exactly as the
// bottom-up line scan inside a chunk does (a stream no longer jams and fans out on a border).
// Inside a row, chunks run as horizontal pairs whose lines are scanned interleaved, as one
// strip 128 cells wide: a chunk that ran all its lines before its neighbour would refill the
// column a border cell just left from above before the neighbour's cells could flow into it
// sideways, and a liquid front would stand as a wall on every vertical border. The pairing
// shifts by one chunk every two ticks, so each border is inside a pair half the time, for
// both scan directions. A cell moves at most 16 cells, well under a chunk, so it never
// reaches past its 3x3 neighbours, and pairs of one parity in a row are two chunks apart:
// they can run together.
int pairing_offset(uint32_t tick) { return (int)((tick >> 1) & 1); }

int pair_of(int chunk_x, int offset) { return floor_div(chunk_x - offset, 2); }

struct ChunkPair {
	int y, index;  // chunk row, pair_of() of its chunks
	Chunk* half[2] = {};  // left, right; null when that chunk is not awake
};

bool runs_before(const ChunkPair& a, const ChunkPair& b) {
	if (a.y != b.y) return a.y > b.y;
	return (a.index & 1) < (b.index & 1);
}

// Runs the pairs on the pool in an order the sorted list allows: a pair waits only for the
// pairs it can touch that come before it, the three pairs under it (it falls into them and
// they rise into it) and, for the second parity, its two same-row neighbours. A pair reaches
// one chunk past its own two, so pairs further apart touch no common cell and their order
// cannot change a result: the tick matches the single-threaded walk of the list cell for cell,
// while rows overlap where the awake chunks are uneven instead of meeting at a barrier.
template <class F>
void run_pairs(JobPool& pool, const std::vector<ChunkPair>& pairs, const F& simulate) {
	const int n = (int)pairs.size();
	auto find = [&](int y, int index) {  // pairs are sorted by row, then parity, then index
		for (int k = 0; k < n; ++k)
			if (pairs[(size_t)k].y == y && pairs[(size_t)k].index == index) return k;
		return -1;
	};
	std::vector<std::vector<int>> dependents((size_t)n);
	std::vector<int> pending((size_t)n);  // guarded by `lock` once the workers run
	std::vector<int> ready;
	for (int k = 0; k < n; ++k) {
		const ChunkPair& p = pairs[(size_t)k];
		int deps = 0;
		auto after = [&](int y, int index) {
			const int d = find(y, index);
			if (d < 0) return;
			dependents[(size_t)d].push_back(k);
			++deps;
		};
		for (int di = -1; di <= 1; ++di) after(p.y + 1, p.index + di);
		if (p.index & 1) {
			after(p.y, p.index - 1);
			after(p.y, p.index + 1);
		}
		pending[(size_t)k] = deps;
		if (deps == 0) ready.push_back(k);
	}

	// Lowest list position first: the bottom rows carry the longest chains of waiting pairs.
	std::mutex lock;
	std::condition_variable wake;  // a pair became ready, or the last one finished
	std::priority_queue<int, std::vector<int>, std::greater<int>> queue(std::greater<int>(), ready);
	int remaining = n;  // guarded by `lock`
	pool.parallel_for(pool.workers(), [&](int) {
		std::unique_lock<std::mutex> guard(lock);
		for (;;) {
			wake.wait(guard, [&] { return !queue.empty() || remaining == 0; });
			if (queue.empty()) return;
			const int k = queue.top();
			queue.pop();
			guard.unlock();
			simulate(pairs[(size_t)k]);
			int woke = 0;
			guard.lock();
			for (int d : dependents[(size_t)k])
				if (--pending[(size_t)d] == 0) {
					queue.push(d);
					++woke;
				}
			if (--remaining == 0) wake.notify_all();
			else if (woke > 1) wake.notify_all();
			else if (woke == 1) wake.notify_one();
		}
	});
}
}  // namespace

void Simulator::tick() {
	const Clock::time_point start = Clock::now();
	// 1-based: a freshly spawned cell has stamp 0, which must not look "already updated".
	const uint32_t tick = ++tick_;

	// 1. Publish what last tick marked, then start collecting this tick's marks. This has to
	// be a full pass first: a mark written while simulating must not run in the same tick.
	// A sleeping chunk (outside the active rect) instead drops `cur` and keeps `next`, so its
	// accumulated marks survive until it wakes; an empty `cur` also skips it below.
	int chunks_active = 0;
	grid.for_each_chunk([&](Chunk& c) {
		if (active_rect && !active_rect->contains(c.coord)) {
			c.cur.reset();
			return;
		}
		++chunks_active;
		c.cur = c.next;
		c.next.reset();
	});

	stats_ = SimStats{};
	stats_.chunks_active = chunks_active;

	// 2. Simulate every chunk that last tick woke, in the order above.
	// SimStats is filled from these relaxed counters once every worker is done.
	std::atomic<int> chunks{0}, cells{0};
	const uint32_t seed = grid.seed();
	const MaterialTable& mt = grid.materials();
	const bool left_to_right = (tick & 1) == 0;

	auto scan_line = [&](Chunk& c, Neighborhood& nb, int ly) {
		if (ly < c.cur.y0 || ly > c.cur.y1) return 0;
		if (left_to_right) {
			for (int lx = c.cur.x0; lx <= c.cur.x1; ++lx) update_cell(nb, lx, ly, tick, seed, mt);
		} else {
			for (int lx = c.cur.x1; lx >= c.cur.x0; --lx) update_cell(nb, lx, ly, tick, seed, mt);
		}
		return c.cur.x1 - c.cur.x0 + 1;
	};

	auto simulate = [&](const ChunkPair& p) {
		// Scan order: the line runs left to right across both chunks, or right to left.
		Chunk* order[2] = {p.half[0], p.half[1]};
		if (!left_to_right) std::swap(order[0], order[1]);
		Neighborhood nb[2];
		int top = -1, bottom = CHUNK;
		for (int h = 0; h < 2; ++h) {
			if (!order[h]) continue;
			chunks.fetch_add(1, std::memory_order_relaxed);
			nb[h].build(grid, order[h]->coord);
			top = std::max(top, order[h]->cur.y1);
			bottom = std::min(bottom, order[h]->cur.y0);
		}
		int visited = 0;
		for (int ly = top; ly >= bottom; --ly)
			for (int h = 0; h < 2; ++h)
				if (order[h]) visited += scan_line(*order[h], nb[h], ly);
		cells.fetch_add(visited, std::memory_order_relaxed);
	};

	// Collect and sort the awake pairs here, before any worker starts: the list is shared, so
	// it must not be built while the workers run. The single-threaded tick walks the list in
	// order; run_pairs keeps every order that can matter, so both agree cell for cell.
	const int offset = pairing_offset(tick);
	std::vector<Chunk*> awake;
	grid.for_each_chunk([&](Chunk& c) {
		if (!c.cur.empty()) awake.push_back(&c);
	});
	std::sort(awake.begin(), awake.end(), [&](const Chunk* a, const Chunk* b) {
		if (a->coord.y != b->coord.y) return a->coord.y < b->coord.y;
		return a->coord.x < b->coord.x;
	});
	std::vector<ChunkPair> pairs;
	for (Chunk* c : awake) {
		const int index = pair_of(c->coord.x, offset);
		if (pairs.empty() || pairs.back().y != c->coord.y || pairs.back().index != index)
			pairs.push_back(ChunkPair{c->coord.y, index});
		pairs.back().half[c->coord.x - offset - 2 * index] = c;
	}
	std::stable_sort(pairs.begin(), pairs.end(), runs_before);
	if (pool == nullptr) {
		for (const ChunkPair& p : pairs) simulate(p);
	} else {
		run_pairs(*pool, pairs, simulate);
	}

	stats_.chunks_simulated = chunks.load(std::memory_order_relaxed);
	stats_.cells_updated = cells.load(std::memory_order_relaxed);
	stats_.sim_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
