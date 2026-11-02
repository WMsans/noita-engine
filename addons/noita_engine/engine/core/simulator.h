#pragma once
#include <cstdint>
#include <optional>

#include "world_grid.h"

class JobPool;

struct SimStats {
	int chunks_simulated = 0;
	int cells_updated = 0;  // cells the tick visited (dirty cells, air included)
	int chunks_active = 0;  // chunks inside the active rect (all loaded when there is none)
	double sim_ms = 0;
};

class Simulator {
public:
	// pool == nullptr runs the single-threaded tick.
	explicit Simulator(WorldGrid& grid, JobPool* pool = nullptr) : grid(grid), pool(pool) {}

	void tick();  // one simulation step; advances tick_count() by 1
	// Chunks outside the rect sleep: their `cur` is dropped and their marks keep
	// accumulating until they wake. nullopt (the default) makes everything active.
	void set_active_rect(std::optional<ChunkRect> rect) { active_rect = rect; }
	uint32_t tick_count() const { return tick_; }
	const SimStats& last_stats() const { return stats_; }

private:
	WorldGrid& grid;
	JobPool* pool;  // null runs the single-threaded tick; see tick() for the phased one
	uint32_t tick_ = 0;
	std::optional<ChunkRect> active_rect;  // nullopt = every loaded chunk is active
	SimStats stats_{};
};
