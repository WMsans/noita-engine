#include <algorithm>

#include "doctest/doctest.h"
#include "grid_query.h"
#include "particles.h"
#include "physics_test_util.h"
#include "raycast.h"
#include "sim_test_util.h"

namespace {

constexpr int W = 8, H = 16;  // the hitbox under test
constexpr uint32_t SOLID_POWDER = RAY_SOLID | RAY_POWDER;

struct Arena {
	MaterialTable t = phystest::table();
	WorldGrid grid{t, 1};
	// Air in [x0,x1) x [y0,y1) plus a row of air chunks above, rock floor from floor_y down.
	Arena(int x0 = 0, int y0 = 0, int x1 = 256, int y1 = 256, int floor_y = 100) {
		phystest::arena(grid, t, x0, y0 - CHUNK, x1, y1);
		phystest::fill(grid, x0, floor_y, x1, y1, t.id_of("rock"));
	}
	MatId id(const char* name) const { return t.id_of(name); }
};

}  // namespace

TEST_CASE("sweep walks on flat ground and reports the floor side") {
	Arena a;
	const BoxSweep r = sweep_box(a.grid, 60, 92, 70, 92, W, H, SOLID_POWDER);
	CHECK(r.x == doctest::Approx(70));
	CHECK(r.y == doctest::Approx(92));
	CHECK_FALSE(r.stopped_x);
	CHECK_FALSE(r.stopped_y);
	CHECK(r.sides == SIDE_FLOOR);
}

TEST_CASE("sweep stops flush at a wall") {
	Arena a;
	phystest::fill(a.grid, 80, 0, 82, 100, a.id("rock"));
	const BoxSweep r = sweep_box(a.grid, 60, 92, 90, 92, W, H, SOLID_POWDER);
	CHECK(r.stopped_x);
	CHECK(r.x == doctest::Approx(76));
	CHECK_FALSE(r.stopped_y);
	CHECK(r.y == doctest::Approx(92));
	CHECK((r.sides & SIDE_RIGHT) != 0);
	CHECK((r.sides & SIDE_LEFT) == 0);
}

TEST_CASE("sweep does not tunnel through a one-cell wall") {
	Arena a;
	phystest::fill(a.grid, 80, 0, 81, 100, a.id("rock"));
	const BoxSweep across = sweep_box(a.grid, 20, 92, 2020, 92, W, H, SOLID_POWDER);
	CHECK(across.stopped_x);
	CHECK(across.x == doctest::Approx(76));

	Arena b(0, 0, 256, 256, 200);
	phystest::fill(b.grid, 0, 100, 256, 101, b.id("rock"));
	const BoxSweep down = sweep_box(b.grid, 60, 20, 60, 2000, W, H, SOLID_POWDER);
	CHECK(down.stopped_y);
	CHECK(down.y == doctest::Approx(92));
	CHECK((down.sides & SIDE_FLOOR) != 0);
}

TEST_CASE("liquid does not block a solid|powder sweep but blocks a liquid one") {
	Arena a(0, 0, 256, 256, 200);
	phystest::fill(a.grid, 0, 90, 256, 100, a.id("water"));
	const BoxSweep through = sweep_box(a.grid, 60, 60, 60, 95, W, H, SOLID_POWDER);
	CHECK_FALSE(through.stopped_y);
	CHECK(through.y == doctest::Approx(95));

	const BoxSweep blocked = sweep_box(a.grid, 60, 60, 60, 95, W, H, RAY_LIQUID);
	CHECK(blocked.stopped_y);
	CHECK(blocked.y == doctest::Approx(82));
	CHECK((blocked.sides & SIDE_FLOOR) != 0);
}

TEST_CASE("body pixels do not block") {
	Arena a;
	Cell c = spawn_cell(a.id("rock"), a.t, 1, 80, 92);
	c.flags |= FLAG_BODY;
	REQUIRE(a.grid.put(80, 92, c));
	CHECK_FALSE(box_blocked(a.grid, 80, 92, W, H, SOLID_POWDER));
	const BoxSweep r = sweep_box(a.grid, 60, 92, 90, 92, W, H, SOLID_POWDER);
	CHECK_FALSE(r.stopped_x);
	CHECK(r.x == doctest::Approx(90));
}

TEST_CASE("negative coordinates") {
	Arena a(-256, -256, 0, 0, -100);
	const BoxSweep walk = sweep_box(a.grid, -60, -108, -70, -108, W, H, SOLID_POWDER);
	CHECK(walk.x == doctest::Approx(-70));
	CHECK((walk.sides & SIDE_FLOOR) != 0);
	phystest::fill(a.grid, -82, -200, -80, -100, a.id("rock"));
	const BoxSweep wall = sweep_box(a.grid, -60, -108, -90, -108, W, H, SOLID_POWDER);
	CHECK(wall.stopped_x);
	CHECK(wall.x == doctest::Approx(-76));
	CHECK((wall.sides & SIDE_LEFT) != 0);
}

TEST_CASE("sweep stops at an unloaded chunk") {
	Arena a;
	const BoxSweep r = sweep_box(a.grid, 240, 92, 300, 92, W, H, SOLID_POWDER);
	CHECK(r.stopped_x);
	CHECK(r.x == doctest::Approx(252));  // right edge flush with the chunk border at 256
	CHECK((r.sides & SIDE_RIGHT) != 0);
}

TEST_CASE("an edge on a cell boundary covers no extra cell") {
	Arena a;
	phystest::fill(a.grid, 8, 0, 10, 100, a.id("rock"));
	CHECK_FALSE(box_blocked(a.grid, 4.0f, 92, 8, H, SOLID_POWDER));
	CHECK(box_blocked(a.grid, 4.01f, 92, 8, H, SOLID_POWDER));
}

TEST_CASE("box_blocked sees powder only with RAY_POWDER") {
	Arena a;
	phystest::fill(a.grid, 0, 90, 256, 100, a.id("sand"));
	CHECK(box_blocked(a.grid, 60, 92, W, H, RAY_POWDER));
	CHECK(box_blocked(a.grid, 60, 92, W, H, SOLID_POWDER));
	CHECK_FALSE(box_blocked(a.grid, 60, 92, W, H, RAY_SOLID));
	CHECK_FALSE(box_blocked(a.grid, 60, 60, W, H, SOLID_POWDER));
}

TEST_CASE("scan_box counts cells and names the most common liquid") {
	Arena a;
	phystest::fill(a.grid, 58, 92, 62, 94, a.id("water"));
	const BoxScan water = scan_box(a.grid, 60, 92, 4, 4);
	CHECK(water.liquid == 8);
	CHECK(water.liquid_mat == a.id("water"));
	CHECK(water.solid == 0);
	CHECK(water.powder == 0);

	Arena b;
	phystest::fill(b.grid, 58, 90, 59, 91, b.id("rock"));
	phystest::fill(b.grid, 59, 90, 60, 91, b.id("sand"));
	Cell body = spawn_cell(b.id("rock"), b.t, 1, 60, 90);
	body.flags |= FLAG_BODY;
	REQUIRE(b.grid.put(60, 90, body));
	const BoxScan mixed = scan_box(b.grid, 60, 92, 4, 4);
	CHECK(mixed.solid == 1);
	CHECK(mixed.powder == 1);
	CHECK(mixed.liquid == 0);
	CHECK(mixed.liquid_mat == MAT_AIR);
}

TEST_CASE("displace_box knocks powder outward") {
	Arena a;
	phystest::fill(a.grid, 58, 90, 62, 94, a.id("sand"));
	ParticlePool pool(a.grid, 1000);
	const int n = displace_box(a.grid, pool, 60, 92, 4, 4, RAY_POWDER, 2.0f, -2.0f, 16, true, 0.5f);
	CHECK(n == 16);
	for (int y = 90; y < 94; ++y)
		for (int x = 58; x < 62; ++x) CHECK(simtest::mat_at(a.grid, x, y) == MAT_AIR);
	CHECK(simtest::count_mat(a.grid, a.id("sand")) == 0);
	REQUIRE(pool.material().size() == 16);
	int left = 0, right = 0;
	float min_x = 1e9f, max_x = -1e9f, min_y = 1e9f, max_y = -1e9f;
	for (const Particle& p : pool.material()) {
		CHECK(p.vy == doctest::Approx(-2.0f));
		(p.vx < 0 ? left : right)++;
		min_x = std::min(min_x, p.x);
		max_x = std::max(max_x, p.x);
		min_y = std::min(min_y, p.y);
		max_y = std::max(max_y, p.y);
	}
	CHECK(left == 8);   // cx + 0.5 < 60: the two left columns
	CHECK(right == 8);
	CHECK(min_x == doctest::Approx(58.5f));
	CHECK(max_x == doctest::Approx(61.5f));
	CHECK(min_y == doctest::Approx(90.5f));  // spawn_dy = 0.5
	CHECK(max_y == doctest::Approx(93.5f));
}

TEST_CASE("displace_box splash takes at most max_cells from the top row first") {
	Arena a;
	phystest::fill(a.grid, 58, 90, 62, 94, a.id("water"));
	ParticlePool pool(a.grid, 1000);
	const int n = displace_box(a.grid, pool, 60, 92, 4, 4, RAY_LIQUID, 1.5f, -3.0f, 3, false, -0.5f);
	CHECK(n == 3);
	for (int x = 58; x < 61; ++x) CHECK(simtest::mat_at(a.grid, x, 90) == MAT_AIR);
	CHECK(simtest::mat_at(a.grid, 61, 90) == a.id("water"));
	for (int y = 91; y < 94; ++y)
		for (int x = 58; x < 62; ++x) CHECK(simtest::mat_at(a.grid, x, y) == a.id("water"));
	REQUIRE(pool.material().size() == 3);
	for (const Particle& p : pool.material()) {
		CHECK(p.y == doctest::Approx(89.5f));  // spawn_dy = -0.5
		CHECK(p.vx == doctest::Approx(1.5f));  // outward = false: the given velocity
		CHECK(p.vy == doctest::Approx(-3.0f));
	}
}
