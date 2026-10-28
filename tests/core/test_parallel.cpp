#include <atomic>
#include <thread>
#include <vector>

#include "cell.h"
#include "doctest/doctest.h"
#include "job_pool.h"
#include "material.h"
#include "rng.h"
#include "sim_test_util.h"
#include "simulator.h"
#include "world_grid.h"

namespace {

constexpr int PAR_ARENA = 512;   // 8x8 chunks: every checkerboard phase gets 16 of them
constexpr int PAR_PER_MAT = 8000;  // cells placed per material

// Paints the brief's five materials at hash-chosen positions. Stateless hashing and a fixed
// seed are the whole point: the same table and seed must build the same two worlds.
void scatter(WorldGrid& grid, const MaterialTable& table) {
	const char* names[5] = {"sand", "water", "oil", "lava", "wood"};
	for (int mi = 0; mi < 5; ++mi) {
		const MatId mat = table.id_of(names[mi]);
		int placed = 0;
		for (uint32_t attempt = 0; placed < PAR_PER_MAT && attempt < 800000; ++attempt) {
			const uint32_t h = hash5(99, attempt, (uint32_t)mi * 7919, 0, 13);
			const int x = (int)(h & 0x1FF), y = (int)((h >> 9) & 0x1FF);
			if (simtest::mat_at(grid, x, y) != MAT_AIR) continue;  // never overwrite a cell
			REQUIRE(grid.set(x, y, mat));
			++placed;
		}
		REQUIRE(placed == PAR_PER_MAT);
	}
}

// Cells of `a` that differ from `b` in mat, temp, life or flags. Stamp is the tick counter
// the scan uses internally, not simulation state, so it is not compared.
struct WorldDiff {
	int cells = 0;
	int chunks = 0;  // chunks of `a` that `b` does not have
};

WorldDiff compare_worlds(WorldGrid& a, WorldGrid& b) {
	WorldDiff d;
	a.for_each_chunk([&](Chunk& ca) {
		Chunk* cb = b.find(ca.coord);
		if (!cb) {
			++d.chunks;
			return;
		}
		for (int ly = 0; ly < CHUNK; ++ly)
			for (int lx = 0; lx < CHUNK; ++lx) {
				const Cell& x = ca.at(lx, ly);
				const Cell& y = cb->at(lx, ly);
				if (x.mat != y.mat || x.temp != y.temp || x.life != y.life ||
				    x.flags != y.flags)
					++d.cells;
			}
	});
	return d;
}

int count_chunks(WorldGrid& grid) {
	int n = 0;
	grid.for_each_chunk([&](Chunk&) { ++n; });
	return n;
}

}  // namespace

TEST_CASE("1 thread and 4 threads give bit-identical worlds") {
	MaterialTable table = simtest::make_chem_table();
	const MatId rock = table.id_of("rock");
	WorldGrid one(table, 4242), four(table, 4242);
	simtest::load_arena(one, table, 0, 0, PAR_ARENA, PAR_ARENA, rock);
	simtest::load_arena(four, table, 0, 0, PAR_ARENA, PAR_ARENA, rock);
	scatter(one, table);
	scatter(four, table);

	JobPool p1(1), p4(4);
	Simulator s1(one, &p1), s4(four, &p4);
	for (int t = 0; t < 400; ++t) {
		s1.tick();
		s4.tick();
	}

	REQUIRE(s1.tick_count() == 400);
	CHECK(s4.tick_count() == 400);
	CHECK(count_chunks(one) == count_chunks(four));
	const WorldDiff d = compare_worlds(one, four);
	CHECK(d.chunks == 0);
	CHECK(d.cells == 0);
}

TEST_CASE("conservation under 4 threads") {
	MaterialTable table = simtest::movement_table();
	const MatId mats[3] = {table.id_of("sand"), table.id_of("water"), table.id_of("oil")};
	constexpr int PER_MAT = 2000;  // Task 4's conservation scenario, 512x512 instead of 256
	WorldGrid grid(table, 2024);
	simtest::load_arena(grid, table, 0, 0, PAR_ARENA, PAR_ARENA, table.id_of("rock"));

	for (int mi = 0; mi < 3; ++mi) {
		int placed = 0;
		for (uint32_t attempt = 0; placed < PER_MAT && attempt < 800000; ++attempt) {
			const uint32_t h = hash5(777, attempt, (uint32_t)mi * 7919, 0, 5);
			const int x = (int)(h & 0x1FF), y = (int)((h >> 9) & 0x1FF);
			if (simtest::mat_at(grid, x, y) != MAT_AIR) continue;  // never overwrite a cell
			REQUIRE(grid.set(x, y, mats[mi]));
			++placed;
		}
		REQUIRE(placed == PER_MAT);
	}
	for (int mi = 0; mi < 3; ++mi) REQUIRE(simtest::count_mat(grid, mats[mi]) == PER_MAT);

	JobPool pool(4);
	Simulator sim(grid, &pool);
	simtest::tick_n(sim, 500);

	for (int mi = 0; mi < 3; ++mi) CHECK(simtest::count_mat(grid, mats[mi]) == PER_MAT);
}

TEST_CASE("parallel_for runs every index once") {
	std::vector<std::atomic<int>> counts(10000);
	for (std::atomic<int>& c : counts) c.store(0);

	SUBCASE("four workers") {
		JobPool pool(4);
		pool.parallel_for((int)counts.size(), [&](int i) {
			counts[(size_t)i].fetch_add(1, std::memory_order_relaxed);
		});
		int bad = 0;
		for (std::atomic<int>& c : counts)
			if (c.load() != 1) ++bad;
		CHECK(bad == 0);
	}

	SUBCASE("one worker runs inline on the caller") {
		JobPool pool(1);
		const std::thread::id caller = std::this_thread::get_id();
		int calls = 0, wrong_thread = 0;
		pool.parallel_for(64, [&](int) {
			++calls;
			if (std::this_thread::get_id() != caller) ++wrong_thread;
		});
		CHECK(calls == 64);
		CHECK(wrong_thread == 0);
		CHECK(pool.workers() == 1);
	}

	SUBCASE("count 0 and count below the worker count") {
		JobPool pool(4);
		int calls = 0;
		pool.parallel_for(0, [&](int) { ++calls; });
		CHECK(calls == 0);
		pool.parallel_for(2, [&](int i) {
			counts[(size_t)i].fetch_add(1, std::memory_order_relaxed);
		});
		CHECK(counts[0].load() == 1);  // each subcase starts from a fresh `counts`
		CHECK(counts[1].load() == 1);
		CHECK(counts[2].load() == 0);
	}
}

TEST_CASE("default_workers is at least one and at most eight") {
	const int n = JobPool::default_workers();
	CHECK(n >= 1);
	CHECK(n <= 8);
}
