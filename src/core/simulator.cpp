#include "simulator.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <vector>

#include "job_pool.h"
#include "sim_rules.h"

namespace {
using Clock = std::chrono::steady_clock;

// Simulation order: chunk rows bottom-up, and inside a row the two x parities. A chunk then
// runs after the three chunks of the row below it, so a falling cell finds the space its
// neighbour vacated this tick, exactly as the bottom-up row scan inside a chunk does, and a
// stream no longer jams and fans out on a chunk border. Rising and sideways moves meet a
// neighbour that has not run yet and wait a tick there (the stamp in update_cell keeps that
// from becoming a second update). A cell moves at most 16 cells, well under a chunk, so it
// never reaches past its 3x3 neighbours, and chunks of one parity in a row are never
// neighbours: they can run together.
bool runs_before(const Chunk* a, const Chunk* b) {
	if (a->coord.y != b->coord.y) return a->coord.y > b->coord.y;
	return (a->coord.x & 1) < (b->coord.x & 1);
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

	auto simulate = [&](Chunk& c) {
		if (c.cur.empty()) return;
		chunks.fetch_add(1, std::memory_order_relaxed);
		Neighborhood nb;
		nb.build(grid, c.coord);
		int visited = 0;
		for (int ly = c.cur.y1; ly >= c.cur.y0; --ly) {
			if (left_to_right) {
				for (int lx = c.cur.x0; lx <= c.cur.x1; ++lx) {
					update_cell(nb, lx, ly, tick, seed, mt);
					++visited;
				}
			} else {
				for (int lx = c.cur.x1; lx >= c.cur.x0; --lx) {
					update_cell(nb, lx, ly, tick, seed, mt);
					++visited;
				}
			}
		}
		cells.fetch_add(visited, std::memory_order_relaxed);
	};

	// Collect and sort the awake chunks here, before any worker starts: the list is shared, so
	// it must not be built while the workers run. Every (row, parity) run is one barrier
	// (parallel_for blocks), which is what makes a finished row's writes visible to the rows
	// above it. The single-threaded tick walks the same order, so both agree cell for cell.
	std::vector<Chunk*> awake;
	grid.for_each_chunk([&](Chunk& c) {
		if (!c.cur.empty()) awake.push_back(&c);
	});
	std::sort(awake.begin(), awake.end(), runs_before);
	for (size_t i = 0; i < awake.size();) {
		size_t end = i + 1;
		while (end < awake.size() && awake[end]->coord.y == awake[i]->coord.y &&
		       (awake[end]->coord.x & 1) == (awake[i]->coord.x & 1))
			++end;
		if (pool == nullptr) {
			for (size_t k = i; k < end; ++k) simulate(*awake[k]);
		} else {
			pool->parallel_for((int)(end - i), [&](int k) { simulate(*awake[i + (size_t)k]); });
		}
		i = end;
	}

	stats_.chunks_simulated = chunks.load(std::memory_order_relaxed);
	stats_.cells_updated = cells.load(std::memory_order_relaxed);
	stats_.sim_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
