#pragma once
#include <cstdint>
#include <memory>
#include <unordered_map>

#include "chunk.h"
#include "coords.h"
#include "generator.h"
#include "material.h"

class WorldGrid {
public:
	explicit WorldGrid(const MaterialTable& table, uint32_t seed) : table(table), world_seed(seed) {}

	// Chunks are created only on the main thread, between ticks.
	Chunk* load(ChunkCoord coord, const ChunkGenerator& gen);
	void unload(ChunkCoord coord);
	Chunk* find(ChunkCoord coord) const;

	bool get(int wx, int wy, Cell& out) const;
	bool set(int wx, int wy, MatId mat);

	template <class F>
	void for_each_chunk(F&& f) {
		for (auto& kv : chunks) f(*kv.second);
	}

	const MaterialTable& materials() const { return table; }
	uint32_t seed() const { return world_seed; }

private:
	void mark_world(int wx, int wy);

	const MaterialTable& table;
	uint32_t world_seed;
	std::unordered_map<int64_t, std::unique_ptr<Chunk>> chunks;
};

// Hot-path accessor: the 3x3 chunks around a centre one, with local coords in [-64, 128).
// A null slot means the chunk is unloaded, and unloaded acts as solid to the sim rules.
struct Neighborhood {
	Chunk* c[3][3] = {};

	void build(const WorldGrid& grid, ChunkCoord centre);
	Cell* cell(int lx, int ly);
	// Wakes the 3x3 world neighbourhood of a changed cell, crossing chunk borders so a
	// moved cell also wakes the dirty rect of the chunk it moved into. Idempotent.
	void mark(int lx, int ly);
};
