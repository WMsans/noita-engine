#pragma once
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "chunk.h"
#include "coords.h"
#include "generator.h"
#include "material.h"

class WorldGrid {
public:
	explicit WorldGrid(const MaterialTable& table, uint32_t seed) : table(table), world_seed(seed) {}

	// Chunks are created only on the main thread, between ticks.
	Chunk* load(ChunkCoord coord, const ChunkGenerator& gen);
	// Single main-thread entry point for a chunk made elsewhere (e.g. a generator thread).
	// A duplicate coord keeps the loaded chunk, drops the argument and returns the loaded
	// one; a new chunk is woken whole and wakes the touched borders of loaded neighbours.
	Chunk* insert(std::unique_ptr<Chunk> chunk);
	void unload(ChunkCoord coord);
	Chunk* find(ChunkCoord coord) const;
	size_t size() const { return chunks.size(); }

	// Chunks unloaded since the last call: the light inputs repaint them as opaque, dark cells.
	std::vector<ChunkCoord> take_light_unloaded();

	bool get(int wx, int wy, Cell& out) const;
	bool set(int wx, int wy, MatId mat);
	// Writes `cell` verbatim (a landing particle keeps its temperature and shade) and wakes
	// it like set(). The stamp is cleared so the next tick doesn't take it as already moved.
	bool put(int wx, int wy, const Cell& cell);
	// Wakes the 3x3 around a cell for the sim and the light input: no render or shape flags, for
	// cells whose look did not change for the renderer (body stamping writes and restores them each
	// tick).
	void wake(int wx, int wy);

	template <class F>
	void for_each_chunk(F&& f) {
		for (auto& kv : chunks) f(*kv.second);
	}

	const MaterialTable& materials() const { return table; }
	uint32_t seed() const { return world_seed; }

private:
	void mark_world(int wx, int wy);
	void wake_neighbour_borders(ChunkCoord coord);

	const MaterialTable& table;
	uint32_t world_seed;
	std::unordered_map<int64_t, std::unique_ptr<Chunk>> chunks;
	std::vector<ChunkCoord> light_unloaded;
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
