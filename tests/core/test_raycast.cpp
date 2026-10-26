#include "coords.h"
#include "doctest/doctest.h"
#include "material.h"
#include "raycast.h"
#include "sim_test_util.h"
#include "world_grid.h"

namespace {

constexpr int ARENA = 128;  // 2x2 chunks of air; outside is unloaded

struct Fixture {
	MaterialTable table = simtest::movement_table();
	WorldGrid grid{table, 1};
	MatId rock = table.id_of("rock");
	MatId water = table.id_of("water");

	explicit Fixture(int x0 = 0, int y0 = 0, int x1 = ARENA, int y1 = ARENA) {
		simtest::load_arena(grid, table, x0, y0, x1, y1, rock);
	}
};

}  // namespace

TEST_CASE("ray hits the first matching cell with its face normal") {
	Fixture f;
	simtest::paint(f.grid, {{50, 0, 51, ARENA, f.rock}});
	const RayHit h = raycast(f.grid, 10.5f, 20.5f, 90.5f, 20.5f, RAY_SOLID);
	REQUIRE(h.hit);
	CHECK(h.x == 50);
	CHECK(h.y == 20);
	CHECK(h.mat == f.rock);
	CHECK_FALSE(h.unloaded);
	CHECK(h.nx == -1);
	CHECK(h.ny == 0);
}

TEST_CASE("mask filters classes") {
	Fixture f;
	simtest::paint(f.grid, {{30, 0, 31, ARENA, f.water}, {50, 0, 51, ARENA, f.rock}});
	CHECK(raycast(f.grid, 10.5f, 20.5f, 90.5f, 20.5f, RAY_SOLID).x == 50);
	const RayHit h = raycast(f.grid, 10.5f, 20.5f, 90.5f, 20.5f, RAY_SOLID | RAY_LIQUID);
	CHECK(h.x == 30);
	CHECK(h.mat == f.water);
}

TEST_CASE("ray starting inside a match hits at start") {
	Fixture f;
	simtest::paint(f.grid, {{40, 40, 60, 60, f.rock}});
	const RayHit h = raycast(f.grid, 50.5f, 50.5f, 90.5f, 50.5f, RAY_SOLID);
	REQUIRE(h.hit);
	CHECK(h.x == 50);
	CHECK(h.y == 50);
	CHECK(h.nx == 0);
	CHECK(h.ny == 0);
}

TEST_CASE("zero-length ray checks only its start cell") {
	Fixture f;
	simtest::paint(f.grid, {{40, 40, 41, 41, f.rock}});
	CHECK_FALSE(raycast(f.grid, 39.5f, 40.5f, 39.5f, 40.5f, RAY_SOLID).hit);
	CHECK(raycast(f.grid, 40.5f, 40.5f, 40.5f, 40.5f, RAY_SOLID).hit);
}

TEST_CASE("ray never skips a cell") {
	Fixture f;
	// A y = x diagonal crosses exact cell corners; x steps first, so it visits (k+1, k).
	for (int k = 10; k <= 20; ++k) simtest::paint(f.grid, {{k + 1, k, k + 2, k + 1, f.rock}});
	const RayHit stair = raycast(f.grid, 0.5f, 0.5f, 40.5f, 40.5f, RAY_SOLID);
	REQUIRE(stair.hit);
	CHECK(stair.x == 11);
	CHECK(stair.y == 10);

	Fixture g;
	simtest::paint(g.grid, {{15, 14, 16, 15, g.rock}});
	const RayHit corner = raycast(g.grid, 0.0f, 0.0f, 30.0f, 30.0f, RAY_SOLID);
	REQUIRE(corner.hit);
	CHECK(corner.x == 15);
	CHECK(corner.y == 14);

	Fixture v;  // axis-aligned, vertical
	simtest::paint(v.grid, {{20, 70, 21, 71, v.rock}});
	const RayHit down = raycast(v.grid, 20.5f, 5.5f, 20.5f, 120.5f, RAY_SOLID);
	REQUIRE(down.hit);
	CHECK(down.y == 70);
	CHECK(down.ny == -1);
}

TEST_CASE("mask zero never hits") {
	Fixture f;
	simtest::paint(f.grid, {{50, 0, 51, ARENA, f.rock}});
	CHECK_FALSE(raycast(f.grid, 10.5f, 20.5f, 200.5f, 20.5f, 0).hit);
}

TEST_CASE("unloaded counts as solid") {
	Fixture f;
	const RayHit h = raycast(f.grid, 100.5f, 20.5f, 200.5f, 20.5f, RAY_LIQUID);
	REQUIRE(h.hit);
	CHECK(h.unloaded);
	CHECK(h.mat == MAT_AIR);
	CHECK(h.x == ARENA);
	CHECK(h.nx == -1);
}

TEST_CASE("ray across the origin") {
	Fixture f(-64, -64, 64, 64);
	simtest::paint(f.grid, {{-5, 3, -4, 4, f.rock}});
	const RayHit h = raycast(f.grid, 5.5f, 3.5f, -20.5f, 3.5f, RAY_SOLID);
	REQUIRE(h.hit);
	CHECK(h.x == -5);
	CHECK(h.y == 3);
	CHECK(h.nx == 1);
	CHECK(h.ny == 0);
}
