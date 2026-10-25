#include "simulator.h"

#include <chrono>

#include "sim_rules.h"

namespace {
using Clock = std::chrono::steady_clock;
}  // namespace

void Simulator::tick() {
	const Clock::time_point start = Clock::now();
	// 1-based: a freshly spawned cell has stamp 0, which must not look "already updated".
	const uint32_t tick = ++tick_;

	// 1. Publish what last tick marked, then start collecting this tick's marks. This has to
	// be a full pass first: a mark written while simulating must not run in the same tick.
	grid.for_each_chunk([](Chunk& c) {
		c.cur = c.next;
		c.next.reset();
	});

	stats_ = SimStats{};

	// 2. Simulate every chunk that last tick woke, rows from the bottom up.
	// The four-phase checkerboard that uses `pool` lands here in Task 6; until then this is
	// always the single-threaded path.
	const uint32_t seed = grid.seed();
	const MaterialTable& mt = grid.materials();
	const bool left_to_right = (tick & 1) == 0;
	grid.for_each_chunk([&](Chunk& c) {
		if (c.cur.empty()) return;
		++stats_.chunks_simulated;
		Neighborhood nb;
		nb.build(grid, c.coord);
		for (int ly = c.cur.y1; ly >= c.cur.y0; --ly) {
			if (left_to_right) {
				for (int lx = c.cur.x0; lx <= c.cur.x1; ++lx) {
					update_cell(nb, lx, ly, tick, seed, mt);
					++stats_.cells_updated;
				}
			} else {
				for (int lx = c.cur.x1; lx >= c.cur.x0; --lx) {
					update_cell(nb, lx, ly, tick, seed, mt);
					++stats_.cells_updated;
				}
			}
		}
	});

	stats_.sim_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
