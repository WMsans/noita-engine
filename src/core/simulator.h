#pragma once
#include <cstdint>

#include "world_grid.h"

class JobPool;  // Task 6; this build always runs single-threaded

struct SimStats {
	int chunks_simulated = 0;
	int cells_updated = 0;  // cells the tick visited (dirty cells, air included)
	double sim_ms = 0;
};

class Simulator {
public:
	// pool == nullptr runs the single-threaded tick.
	explicit Simulator(WorldGrid& grid, JobPool* pool = nullptr) : grid(grid), pool(pool) {}

	void tick();  // one simulation step; advances tick_count() by 1
	uint32_t tick_count() const { return tick_; }
	const SimStats& last_stats() const { return stats_; }

private:
	WorldGrid& grid;
	JobPool* pool;  // the parallel tick lands in Task 6
	uint32_t tick_ = 0;
	SimStats stats_{};
};
