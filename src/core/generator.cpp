#include "generator.h"

#include "rng.h"

std::unique_ptr<Chunk> make_chunk(ChunkCoord coord, const ChunkGenerator& gen, uint32_t seed) {
	std::unique_ptr<Chunk> chunk = std::make_unique<Chunk>();
	chunk->coord = coord;
	gen.generate(coord, *chunk, seed);
	return chunk;
}

Cell spawn_cell(MatId mat, const MaterialTable& table, uint32_t seed, int wx, int wy) {
	const CompiledMaterial& def = table.get(mat);
	Cell c{};
	c.mat = mat;
	c.temp = def.base_temp;
	c.life = def.lifetime;
	c.shade = (uint8_t)(hash5(seed, 0, wx, wy, 0) & 0xFF);
	return c;
}

void BoxGenerator::generate(ChunkCoord coord, Chunk& out, uint32_t seed) const {
	const int base_x = coord.x * CHUNK;
	const int base_y = coord.y * CHUNK;
	for (int ly = 0; ly < CHUNK; ++ly) {
		const int wy = base_y + ly;
		for (int lx = 0; lx < CHUNK; ++lx) {
			const int wx = base_x + lx;
			const bool inside = wx >= x0 && wx < x1 && wy >= y0 && wy < y1;
			out.at(lx, ly) = spawn_cell(inside ? MAT_AIR : solid_mat, table, seed, wx, wy);
		}
	}
}
