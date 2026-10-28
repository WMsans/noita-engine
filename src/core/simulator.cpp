#include "simulator.h"

#include <atomic>
#include <chrono>
#include <vector>

#include "job_pool.h"
#include "sim_rules.h"

namespace {
using Clock = std::chrono::steady_clock;

// Checkerboard phase of a chunk: (cx & 1) | ((cy & 1) << 1), so (0,0) -> 0, (1,0) -> 1,
// (0,1) -> 2, (1,1) -> 3. Any +-1 step in x or y flips one of those two parity bits, and a
// chunk only ever touches its own cells and its 3x3 neighbours' (a cell moves at most 16
// cells, so at most one chunk border). So two chunks of the same phase are never neighbour
// to each other and never read or write each other's cells: they can run concurrently.
int phase_of(ChunkCoord c) { return (c.x & 1) | ((c.y & 1) << 1); }
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

	// 2. Simulate every chunk that last tick woke, rows from the bottom up.
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

	if (pool == nullptr) {
		grid.for_each_chunk(simulate);
	} else {
		// Bucket the awake chunks by phase on this thread, before any worker starts: the
		// lists are shared, so they must not be built while the phase loop runs. Each phase
		// is a barrier (parallel_for blocks), which is what makes another phase's writes to
		// our cells visible and finished before we read them.
		std::vector<Chunk*> phases[4];
		grid.for_each_chunk([&](Chunk& c) {
			if (!c.cur.empty()) phases[phase_of(c.coord)].push_back(&c);
		});
		for (std::vector<Chunk*>& phase : phases) {
			if (phase.empty()) continue;  // nothing awake in this phase this tick
			pool->parallel_for((int)phase.size(), [&](int i) { simulate(*phase[(size_t)i]); });
		}
	}

	stats_.chunks_simulated = chunks.load(std::memory_order_relaxed);
	stats_.cells_updated = cells.load(std::memory_order_relaxed);
	stats_.sim_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
