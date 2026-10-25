#include <algorithm>
#include <vector>

#include "cell.h"
#include "coords.h"
#include "doctest/doctest.h"
#include "material.h"
#include "rng.h"
#include "sim_test_util.h"
#include "simulator.h"
#include "world_grid.h"

namespace {

constexpr int ARENA = 128;  // exactly 2x2 chunks, so cross-chunk moves are covered
constexpr int FLOOR = 100;  // world y of the first solid row in the floor tests

// Air inside [0,ARENA) x [0,ARENA), rock outside it: a closed box.
void boxed_arena(WorldGrid& grid, const MaterialTable& table) {
	simtest::load_arena(grid, table, 0, 0, ARENA, ARENA, table.id_of("rock"));
}

void floor_at_100(WorldGrid& grid, const MaterialTable& table) {
	simtest::paint(grid, {{0, FLOOR, ARENA, ARENA, table.id_of("rock")}});
}

}  // namespace

TEST_CASE("powder falls and rests on solid") {
	MaterialTable table = simtest::movement_table();
	const MatId sand = table.id_of("sand");
	WorldGrid grid(table, 1234);
	boxed_arena(grid, table);
	floor_at_100(grid, table);
	REQUIRE(grid.set(60, 10, sand));

	Simulator sim(grid);
	simtest::tick_n(sim, 120);

	Cell c{};
	REQUIRE(grid.get(60, 99, c));
	CHECK(c.mat == sand);
	REQUIRE(grid.get(60, 10, c));
	CHECK(c.mat == MAT_AIR);
	CHECK(simtest::count_mat(grid, sand) == 1);
}

TEST_CASE("powder accelerates") {
	MaterialTable table = simtest::movement_table();
	const MatId sand = table.id_of("sand");
	WorldGrid grid(table, 55);
	boxed_arena(grid, table);
	REQUIRE(grid.set(30, 0, sand));

	Simulator sim(grid);
	simtest::tick_n(sim, 8);

	const simtest::MatPos p = simtest::locate(grid, sand);
	CHECK(p.count == 1);
	CHECK(p.x == 30);
	// v = 1+2+...+8 = 36 cells of free fall in the best case, never less than 20.
	CHECK(p.y >= 20);
	CHECK(p.y <= 36);
}

TEST_CASE("powder piles diagonally") {
	MaterialTable table = simtest::movement_table();
	const MatId sand = table.id_of("sand");
	WorldGrid grid(table, 808);
	boxed_arena(grid, table);
	floor_at_100(grid, table);

	Simulator sim(grid);
	for (int i = 0; i < 20; ++i) {
		REQUIRE(grid.set(64, 0, sand));  // one cell per tick, always the same column
		sim.tick();
	}
	simtest::tick_n(sim, 200);

	int columns = 0, tallest = 0;
	for (int x = 0; x < ARENA; ++x) {
		int height = 0;
		for (int y = 0; y < FLOOR; ++y)
			if (simtest::mat_at(grid, x, y) == sand) ++height;
		if (height > 0) ++columns;
		tallest = std::max(tallest, height);
	}
	CHECK(simtest::count_mat(grid, sand) == 20);
	CHECK(columns > 1);       // it did not pile up in one column
	CHECK(tallest <= columns);  // and the heap is not taller than it is wide
}

TEST_CASE("powder sinks through water") {
	MaterialTable table = simtest::movement_table();
	const MatId sand = table.id_of("sand"), water = table.id_of("water"), rock = table.id_of("rock");
	WorldGrid grid(table, 4321);
	boxed_arena(grid, table);
	// A 1-wide shaft closed at the bottom, 10 cells of water in it, sand on top of them.
	simtest::paint(grid, {{40, 10, 41, 91, rock},
	                      {42, 10, 43, 91, rock},
	                      {0, 91, ARENA, ARENA, rock},
	                      {41, 81, 42, 91, water}});
	REQUIRE(grid.set(41, 80, sand));

	Simulator sim(grid);
	simtest::tick_n(sim, 60);

	CHECK(simtest::mat_at(grid, 41, 90) == sand);  // the bottom of the column, on the floor
	CHECK(simtest::count_mat(grid, sand) == 1);
	CHECK(simtest::count_mat(grid, water) == 10);
}

TEST_CASE("oil floats on water") {
	MaterialTable table = simtest::movement_table();
	const MatId oil = table.id_of("oil"), water = table.id_of("water"), rock = table.id_of("rock");
	WorldGrid grid(table, 9001);
	boxed_arena(grid, table);

	// A closed rock box with alternating oil/water rows filling it completely.
	constexpr int x0 = 10, x1 = 50, y0 = 50, y1 = 70;  // interior 38 x 18 = 684 cells
	std::vector<simtest::Rect> rects{{x0, y0, x1, y1, rock}};
	for (int y = y0 + 1; y < y1 - 1; ++y)
		rects.push_back({x0 + 1, y, x1 - 1, y + 1, ((y - y0) & 1) ? water : oil});
	simtest::paint(grid, rects);
	REQUIRE(simtest::count_mat(grid, oil) == 342);
	REQUIRE(simtest::count_mat(grid, water) == 342);

	Simulator sim(grid);
	simtest::tick_n(sim, 600);

	int bad = 0;
	for (int x = x0 + 1; x < x1 - 1; ++x) {
		bool water_seen = false;
		for (int y = y0 + 1; y < y1 - 1; ++y) {  // top to bottom
			const MatId m = simtest::mat_at(grid, x, y);
			if (m == water) {
				water_seen = true;
			} else if (m == oil) {
				if (water_seen) ++bad;  // this oil sits below water in its column
			} else {
				++bad;  // the box is full: anything that is not oil or water is wrong
			}
		}
	}
	CHECK(bad == 0);
	CHECK(simtest::count_mat(grid, oil) == 342);
	CHECK(simtest::count_mat(grid, water) == 342);
}

TEST_CASE("liquid spreads sideways") {
	MaterialTable table = simtest::movement_table();
	const MatId water = table.id_of("water");
	WorldGrid grid(table, 66);
	boxed_arena(grid, table);
	floor_at_100(grid, table);
	simtest::paint(grid, {{60, 90, 70, 100, water}});  // a 10x10 block on a 128-wide floor

	Simulator sim(grid);
	simtest::tick_n(sim, 300);

	int tallest = 0;
	for (int x = 0; x < ARENA; ++x) {
		int height = 0;
		for (int y = 0; y < FLOOR; ++y)
			if (simtest::mat_at(grid, x, y) == water) ++height;
		tallest = std::max(tallest, height);
	}
	CHECK(tallest <= 2);
	CHECK(simtest::count_mat(grid, water) == 100);
}

TEST_CASE("gas rises") {
	MaterialTable table = simtest::movement_table();
	const MatId steam = table.id_of("steam"), rock = table.id_of("rock");
	WorldGrid grid(table, 77);
	boxed_arena(grid, table);
	simtest::paint(grid, {{0, 0, ARENA, 1, rock}, {0, ARENA - 1, ARENA, ARENA, rock}});
	REQUIRE(grid.set(30, 125, steam));  // bottom of the closed box

	Simulator sim(grid);
	simtest::tick_n(sim, 300);

	const simtest::MatPos p = simtest::locate(grid, steam);
	CHECK(p.count == 1);
	CHECK(p.y == 1);  // the top air row, just under the rock ceiling
}

TEST_CASE("missing chunk acts solid") {
	MaterialTable table = simtest::movement_table();
	const MatId sand = table.id_of("sand"), rock = table.id_of("rock");
	WorldGrid grid(table, 5);
	BoxGenerator gen(0, 0, CHUNK, CHUNK, rock, table);  // one chunk of air, nothing else loaded
	REQUIRE(grid.load({0, 0}, gen) != nullptr);
	REQUIRE(grid.set(20, 63, sand));  // bottom row: the chunk below is not loaded

	Simulator sim(grid);
	simtest::tick_n(sim, 60);

	Cell c{};
	REQUIRE(grid.get(20, 63, c));
	CHECK(c.mat == sand);
	CHECK(simtest::count_mat(grid, sand) == 1);
	CHECK(grid.find({0, 1}) == nullptr);  // nothing loaded it either
}

TEST_CASE("crossing chunk border") {
	MaterialTable table = simtest::movement_table();
	const MatId sand = table.id_of("sand");
	WorldGrid grid(table, 1);
	boxed_arena(grid, table);
	REQUIRE(grid.set(30, 63, sand));  // last row of chunk (0,0)

	Simulator sim(grid);
	sim.tick();  // first tick: velocity 0 -> 1 cell of fall

	Cell c{};
	REQUIRE(grid.get(30, 64, c));
	CHECK(c.mat == sand);
	REQUIRE(grid.get(30, 63, c));
	CHECK(c.mat == MAT_AIR);
	Chunk* lower = grid.find({0, 1});
	REQUIRE(lower != nullptr);
	CHECK(lower->at(30, 0).mat == sand);
}

TEST_CASE("settled world goes quiet") {
	MaterialTable table = simtest::movement_table();
	const MatId sand = table.id_of("sand");
	WorldGrid grid(table, 31);
	boxed_arena(grid, table);
	floor_at_100(grid, table);

	Simulator sim(grid);
	for (int i = 0; i < 50; ++i) {
		REQUIRE(grid.set(64, 0, sand));  // drop a pile one cell per tick
		sim.tick();
	}
	simtest::tick_n(sim, 600);

	bool quiet = sim.last_stats().chunks_simulated == 0;
	for (int i = 0; i < 5 && !quiet; ++i) {
		sim.tick();
		quiet = sim.last_stats().chunks_simulated == 0;
	}
	CHECK(quiet);
	CHECK(simtest::count_mat(grid, sand) == 50);
}

TEST_CASE("conservation") {
	MaterialTable table = simtest::movement_table();
	const MatId mats[3] = {table.id_of("sand"), table.id_of("water"), table.id_of("oil")};
	constexpr int BIG = 256;      // 4x4 chunks
	constexpr int PER_MAT = 2000;  // 6000 cells total
	WorldGrid grid(table, 2024);
	simtest::load_arena(grid, table, 0, 0, BIG, BIG, table.id_of("rock"));

	for (int mi = 0; mi < 3; ++mi) {
		int placed = 0;
		for (uint32_t attempt = 0; placed < PER_MAT && attempt < 400000; ++attempt) {
			const uint32_t h = hash5(777, attempt, mi * 7919, 0, 5);
			const int x = (int)(h & 0xFF), y = (int)((h >> 8) & 0xFF);
			if (simtest::mat_at(grid, x, y) != MAT_AIR) continue;  // never overwrite a cell
			REQUIRE(grid.set(x, y, mats[mi]));
			++placed;
		}
		REQUIRE(placed == PER_MAT);
	}
	for (int mi = 0; mi < 3; ++mi) REQUIRE(simtest::count_mat(grid, mats[mi]) == PER_MAT);

	Simulator sim(grid);
	simtest::tick_n(sim, 500);

	for (int mi = 0; mi < 3; ++mi) CHECK(simtest::count_mat(grid, mats[mi]) == PER_MAT);
}
