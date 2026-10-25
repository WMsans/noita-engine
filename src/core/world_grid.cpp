#include "world_grid.h"

#include <utility>

#include "rng.h"

namespace {
// Window slot of the chunk owning local coord l (locals are centre-relative, [-64, 128)).
int slot(int l) { return floor_div(l, CHUNK) + 1; }
}  // namespace

Chunk* WorldGrid::load(ChunkCoord coord, const ChunkGenerator& gen) {
	auto it = chunks.find(chunk_key(coord));
	if (it != chunks.end()) return it->second.get();

	std::unique_ptr<Chunk> chunk = std::make_unique<Chunk>();
	chunk->coord = coord;
	gen.generate(coord, *chunk, world_seed);
	// A new chunk must simulate at least once, so its first tick processes everything.
	chunk->next.include(0, 0);
	chunk->next.include(CHUNK - 1, CHUNK - 1);

	Chunk* out = chunk.get();
	chunks.emplace(chunk_key(coord), std::move(chunk));
	return out;
}

void WorldGrid::unload(ChunkCoord coord) { chunks.erase(chunk_key(coord)); }

Chunk* WorldGrid::find(ChunkCoord coord) const {
	auto it = chunks.find(chunk_key(coord));
	return it == chunks.end() ? nullptr : it->second.get();
}

bool WorldGrid::get(int wx, int wy, Cell& out) const {
	Chunk* chunk = find(chunk_of(wx, wy));
	if (!chunk) return false;
	out = chunk->at(floor_mod(wx, CHUNK), floor_mod(wy, CHUNK));
	return true;
}

bool WorldGrid::set(int wx, int wy, MatId mat) {
	Chunk* chunk = find(chunk_of(wx, wy));
	if (!chunk) return false;
	chunk->at(floor_mod(wx, CHUNK), floor_mod(wy, CHUNK)) =
	    spawn_cell(mat, table, world_seed, wx, wy);
	mark_world(wx, wy);
	return true;
}

void WorldGrid::mark_world(int wx, int wy) {
	for (int dy = -1; dy <= 1; ++dy)
		for (int dx = -1; dx <= 1; ++dx) {
			const int x = wx + dx, y = wy + dy;
			Chunk* chunk = find(chunk_of(x, y));
			if (!chunk) continue;  // unloaded neighbours never wake
			chunk->mark_next(floor_mod(x, CHUNK), floor_mod(y, CHUNK));
			chunk->render_dirty.store(true);
		}
}

void Neighborhood::build(const WorldGrid& grid, ChunkCoord centre) {
	for (int dy = 0; dy < 3; ++dy)
		for (int dx = 0; dx < 3; ++dx)
			c[dx][dy] = grid.find({centre.x + dx - 1, centre.y + dy - 1});
}

Cell* Neighborhood::cell(int lx, int ly) {
	const int cx = slot(lx), cy = slot(ly);
	if ((unsigned)cx > 2u || (unsigned)cy > 2u) return nullptr;  // outside [-64, 128)
	Chunk* chunk = c[cx][cy];
	if (!chunk) return nullptr;
	return &chunk->at(floor_mod(lx, CHUNK), floor_mod(ly, CHUNK));
}

void Neighborhood::mark(int lx, int ly) {
	const int cx = slot(lx), cy = slot(ly);
	if ((unsigned)cx > 2u || (unsigned)cy > 2u) return;  // outside [-64, 128)
	if (!c[cx][cy]) return;

	for (int dy = -1; dy <= 1; ++dy)
		for (int dx = -1; dx <= 1; ++dx) {
			const int tx = lx + dx, ty = ly + dy;  // also centre-relative
			const int bx = slot(tx), by = slot(ty);
			// Only a neighbourhood reaching past the 3x3 window is dropped; that chunk's own
			// worker marks its cells.
			if ((unsigned)bx > 2u || (unsigned)by > 2u) continue;
			Chunk* chunk = c[bx][by];
			if (!chunk) continue;  // unloaded neighbours never wake
			chunk->mark_next(floor_mod(tx, CHUNK), floor_mod(ty, CHUNK));
			chunk->render_dirty.store(true);
		}
}
