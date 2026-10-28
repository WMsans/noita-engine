#pragma once
#include <cstdint>
#include <memory>

#include "chunk.h"
#include "coords.h"
#include "material.h"

// A cell carrying a material's defaults plus its own per-pixel shade. Shared by the
// generators and by WorldGrid::set so both spawn identical cells.
Cell spawn_cell(MatId mat, const MaterialTable& table, uint32_t seed, int wx, int wy);

class ChunkGenerator {
public:
	virtual ~ChunkGenerator() = default;
	virtual void generate(ChunkCoord coord, Chunk& out, uint32_t seed) const = 0;
};

// Fresh heap chunk: coord set, cells generated, dirty rects untouched (the grid decides
// what to wake).
std::unique_ptr<Chunk> make_chunk(ChunkCoord coord, const ChunkGenerator& gen, uint32_t seed);

// Air inside the half-open world-pixel rect [x0,x1) x [y0,y1), `solid_mat` outside.
class BoxGenerator : public ChunkGenerator {
public:
	BoxGenerator(int x0, int y0, int x1, int y1, MatId solid_mat, const MaterialTable& table)
	    : x0(x0), y0(y0), x1(x1), y1(y1), solid_mat(solid_mat), table(table) {}

	void generate(ChunkCoord coord, Chunk& out, uint32_t seed) const override;

private:
	int x0, y0, x1, y1;
	MatId solid_mat;
	const MaterialTable& table;
};
