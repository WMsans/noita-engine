#pragma once
#include <string>
#include <vector>

#include "chunk.h"
#include "coords.h"
#include "doctest/doctest.h"
#include "generator.h"
#include "material.h"
#include "simulator.h"
#include "world_grid.h"

// Helpers shared by the simulation tests: one material set, a boxed arena of chunks, and
// the paint/count/locate helpers the tests use to set up and inspect the world.
namespace simtest {

// air(implicit) + sand, rock, water, oil, steam. Densities match the starter set.
inline MaterialTable movement_table() {
	std::vector<MaterialDef> defs(5);
	defs[0].name = "sand";
	defs[0].cls = MatClass::Powder;
	defs[0].density = 1.6f;
	defs[1].name = "rock";
	defs[1].cls = MatClass::Solid;
	defs[1].density = 3.0f;
	defs[1].collision_solid = true;
	defs[2].name = "water";
	defs[2].cls = MatClass::Liquid;
	defs[2].density = 1.0f;
	defs[2].dispersion = 5;
	defs[3].name = "oil";
	defs[3].cls = MatClass::Liquid;
	defs[3].density = 0.8f;
	defs[3].dispersion = 4;
	defs[4].name = "steam";
	defs[4].cls = MatClass::Gas;
	defs[4].density = 0.3f;
	defs[4].dispersion = 6;

	MaterialTable table;
	std::string error;
	REQUIRE(MaterialTable::compile(defs, {}, table, error));
	return table;
}

// Half-open world rect [x0,x1) x [y0,y1) painted with one material.
struct Rect {
	int x0, y0, x1, y1;
	MatId mat;
};

// Loads every chunk covering the half-open world rect [x0,x1) x [y0,y1): air inside the
// rect, `solid` outside. The rect should span whole chunks so no chunk keeps a stale
// partial dirty rect; the tests use at least 2x2 chunks (128x128 world cells).
inline void load_arena(WorldGrid& grid, const MaterialTable& table, int x0, int y0, int x1, int y1,
                       MatId solid) {
	BoxGenerator gen(x0, y0, x1, y1, solid, table);
	for (int cy = floor_div(y0, CHUNK); cy <= floor_div(y1 - 1, CHUNK); ++cy)
		for (int cx = floor_div(x0, CHUNK); cx <= floor_div(x1 - 1, CHUNK); ++cx)
			REQUIRE(grid.load({cx, cy}, gen) != nullptr);
}

// Paints rects in order, so a later rect overwrites an earlier one.
inline void paint(WorldGrid& grid, const std::vector<Rect>& rects) {
	int missed = 0;
	for (const Rect& r : rects)
		for (int y = r.y0; y < r.y1; ++y)
			for (int x = r.x0; x < r.x1; ++x)
				if (!grid.set(x, y, r.mat)) ++missed;
	REQUIRE(missed == 0);  // every painted coordinate must be inside a loaded chunk
}

// Material at a world position; MAT_AIR when it is outside every loaded chunk.
inline MatId mat_at(WorldGrid& grid, int wx, int wy) {
	Cell c{};
	return grid.get(wx, wy, c) ? c.mat : MAT_AIR;
}

inline int count_mat(WorldGrid& grid, MatId mat) {
	int n = 0;
	grid.for_each_chunk([&](Chunk& c) {
		for (int ly = 0; ly < CHUNK; ++ly)
			for (int lx = 0; lx < CHUNK; ++lx)
				if (c.at(lx, ly).mat == mat) ++n;
	});
	return n;
}

struct MatPos {
	int x = -1, y = -1;  // world position of the first cell holding the material
	int count = 0;       // how many cells hold it
};

inline MatPos locate(WorldGrid& grid, MatId mat) {
	MatPos out;
	grid.for_each_chunk([&](Chunk& c) {
		for (int ly = 0; ly < CHUNK; ++ly)
			for (int lx = 0; lx < CHUNK; ++lx)
				if (c.at(lx, ly).mat == mat) {
					if (out.count == 0) {
						out.x = c.coord.x * CHUNK + lx;
						out.y = c.coord.y * CHUNK + ly;
					}
					++out.count;
				}
	});
	return out;
}

inline void tick_n(Simulator& sim, int n) {
	for (int i = 0; i < n; ++i) sim.tick();
}

}  // namespace simtest
