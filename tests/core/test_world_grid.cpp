#include <string>
#include <vector>

#include "chunk.h"
#include "coords.h"
#include "doctest/doctest.h"
#include "generator.h"
#include "material.h"
#include "rng.h"
#include "world_grid.h"

namespace {

// air(0), sand(1), rock(2)
MaterialTable make_table() {
	std::vector<MaterialDef> defs(2);
	defs[0].name = "sand";
	defs[0].cls = MatClass::Powder;
	defs[0].density = 1.5f;
	defs[1].name = "rock";
	defs[1].cls = MatClass::Solid;
	defs[1].collision_solid = true;
	defs[1].base_temp = 400;
	defs[1].lifetime = 9;
	MaterialTable table;
	std::string error;
	REQUIRE(MaterialTable::compile(defs, {}, table, error));
	REQUIRE(table.id_of("sand") == 1);
	REQUIRE(table.id_of("rock") == 2);
	return table;
}

// Arena far larger than any chunk in these tests: every cell is air.
BoxGenerator open_air(const MaterialTable& table) {
	return BoxGenerator(-4096, -4096, 4096, 4096, table.id_of("rock"), table);
}

}  // namespace

TEST_CASE("set/get round trip at negative coords") {
	MaterialTable table = make_table();
	const MatId sand = table.id_of("sand");
	BoxGenerator gen = open_air(table);
	WorldGrid grid(table, 1234);
	grid.load({-1, -1}, gen);
	grid.load({0, 0}, gen);

	CHECK(grid.set(-1, -1, sand));
	Cell out{};
	REQUIRE(grid.get(-1, -1, out));
	CHECK(out.mat == sand);
	CHECK(out.temp == table.get(sand).base_temp);

	Chunk* neg = grid.find({-1, -1});
	REQUIRE(neg != nullptr);
	CHECK(neg->coord == ChunkCoord{-1, -1});
	CHECK(neg->at(63, 63).mat == sand);
	// Neighbouring world cell in chunk (0,0) is untouched.
	CHECK(grid.find({0, 0})->at(0, 0).mat == MAT_AIR);
}

TEST_CASE("set on unloaded chunk returns false") {
	MaterialTable table = make_table();
	BoxGenerator gen = open_air(table);
	WorldGrid grid(table, 7);

	CHECK(grid.set(1000, 1000, table.id_of("sand")) == false);
	CHECK(grid.find({15, 15}) == nullptr);
	Cell out{};
	CHECK(grid.get(1000, 1000, out) == false);

	int chunks = 0;
	grid.for_each_chunk([&](Chunk&) { ++chunks; });
	CHECK(chunks == 0);  // nothing was created

	// Unloading makes a loaded chunk behave like one that was never there.
	BoxGenerator open = open_air(table);
	grid.load({2, 2}, open);
	REQUIRE(grid.find({2, 2}) != nullptr);
	grid.unload({2, 2});
	CHECK(grid.find({2, 2}) == nullptr);
	CHECK(grid.set(128, 128, table.id_of("sand")) == false);
}

TEST_CASE("set marks neighbours across a chunk border dirty") {
	MaterialTable table = make_table();
	BoxGenerator gen = open_air(table);
	WorldGrid grid(table, 99);
	Chunk* left = grid.load({0, 0}, gen);
	Chunk* right = grid.load({1, 0}, gen);
	REQUIRE(left != nullptr);
	REQUIRE(right != nullptr);
	left->next.reset();
	right->next.reset();
	left->render_dirty = false;
	right->render_dirty = false;

	CHECK(grid.set(63, 10, table.id_of("sand")));

	// World 3x3 around (63,10) is x 62..64: local 62..63 in (0,0), local 0 in (1,0).
	CHECK(!left->next.empty());
	CHECK(left->next.x0 == 62);
	CHECK(left->next.x1 == 63);
	CHECK(left->next.y0 == 9);
	CHECK(left->next.y1 == 11);

	CHECK(!right->next.empty());
	CHECK(right->next.x0 == 0);
	CHECK(right->next.x1 == 0);
	CHECK(right->next.y0 == 9);
	CHECK(right->next.y1 == 11);

	CHECK(left->render_dirty.load());
	CHECK(right->render_dirty.load());
}

TEST_CASE("BoxGenerator fills solid outside the arena") {
	MaterialTable table = make_table();
	const MatId rock = table.id_of("rock");
	BoxGenerator gen(0, 0, 64, 64, rock, table);
	WorldGrid grid(table, 42);

	Chunk* outside = grid.load({-1, 0}, gen);
	Chunk* inside = grid.load({0, 0}, gen);
	REQUIRE(outside != nullptr);
	REQUIRE(inside != nullptr);

	int wrong_outside = 0, wrong_inside = 0;
	for (int ly = 0; ly < CHUNK; ++ly)
		for (int lx = 0; lx < CHUNK; ++lx) {
			if (outside->at(lx, ly).mat != rock) ++wrong_outside;
			if (inside->at(lx, ly).mat != MAT_AIR) ++wrong_inside;
		}
	CHECK(wrong_outside == 0);
	CHECK(wrong_inside == 0);

	// Material defaults and per-pixel shade. Local (63,63) of chunk (-1,0) is world (-1,63).
	const Cell& solid = outside->at(63, 63);
	CHECK(solid.temp == table.get(rock).base_temp);
	CHECK(solid.life == table.get(rock).lifetime);
	CHECK(solid.shade == (hash5(42, 0, -1, 63, 0) & 0xFF));
	// Air is filled from air's own compiled entry.
	const Cell& air = inside->at(0, 0);
	CHECK(air.temp == table.get(MAT_AIR).base_temp);
	CHECK(air.life == table.get(MAT_AIR).lifetime);
	CHECK(air.shade == (hash5(42, 0, 0, 0, 0) & 0xFF));

	// A fresh chunk must simulate at least once.
	CHECK(!inside->next.empty());
	CHECK(inside->next.x0 == 0);
	CHECK(inside->next.y0 == 0);
	CHECK(inside->next.x1 == CHUNK - 1);
	CHECK(inside->next.y1 == CHUNK - 1);
}

TEST_CASE("Neighborhood returns nullptr for missing chunks") {
	MaterialTable table = make_table();
	BoxGenerator gen = open_air(table);
	WorldGrid grid(table, 3);
	grid.load({0, 0}, gen);

	Neighborhood nb;
	nb.build(grid, {0, 0});
	CHECK(nb.cell(-1, 5) == nullptr);   // chunk (-1,0) is not loaded
	CHECK(nb.cell(5, 5) != nullptr);
	CHECK(nb.cell(5, -1) == nullptr);   // chunk (0,-1) is not loaded
	CHECK(nb.cell(0, 0) != nullptr);    // first cell of the loaded chunk
	CHECK(nb.cell(63, 63) != nullptr);  // last cell of the loaded chunk
}

// Sixth case: the five above never exercise a negative-coordinate dirty rect,
// nor Neighborhood::mark crossing a chunk edge (Task 4 depends on both).
TEST_CASE("dirty marks cross chunk borders at negative coords") {
	MaterialTable table = make_table();
	const MatId sand = table.id_of("sand");
	BoxGenerator gen = open_air(table);
	WorldGrid grid(table, 5);
	Chunk* corner = grid.load({-1, -1}, gen);
	Chunk* centre = grid.load({0, 0}, gen);
	Chunk* left = grid.load({-1, 0}, gen);
	REQUIRE(corner != nullptr);
	REQUIRE(centre != nullptr);
	REQUIRE(left != nullptr);

	corner->next.reset();
	centre->next.reset();
	left->next.reset();
	CHECK(grid.set(-1, -1, sand));  // local (63,63) of chunk (-1,-1)
	// World 3x3 around (-1,-1) is x/y -2..0.
	CHECK(corner->next.x0 == 62);
	CHECK(corner->next.y0 == 62);
	CHECK(corner->next.x1 == 63);
	CHECK(corner->next.y1 == 63);
	CHECK(centre->next.x0 == 0);
	CHECK(centre->next.y0 == 0);
	CHECK(centre->next.x1 == 0);
	CHECK(centre->next.y1 == 0);
	CHECK(left->next.x0 == 62);  // world (-2,0) and (-1,0) are the last two cells of chunk (-1,0)
	CHECK(left->next.y0 == 0);
	CHECK(left->next.x1 == 63);
	CHECK(left->next.y1 == 0);

	// Neighborhood::mark on the low edge of the centre chunk wakes the chunk to its left.
	corner->next.reset();
	centre->next.reset();
	left->next.reset();
	Neighborhood nb;
	nb.build(grid, {0, 0});
	nb.mark(0, 5);  // local 0 of chunk (0,0) = world (0,5); its left neighbour is local 63 of (-1,0)
	CHECK(!centre->next.empty());
	CHECK(centre->next.x0 == 0);
	CHECK(centre->next.x1 == 1);  // world x 0..1 stays inside chunk (0,0)
	CHECK(centre->next.y0 == 4);
	CHECK(centre->next.y1 == 6);
	CHECK(left->next.x0 == 63);
	CHECK(left->next.x1 == 63);
	CHECK(left->next.y0 == 4);
	CHECK(left->next.y1 == 6);
	CHECK(corner->next.empty());  // two chunks up: never woken
}
