#include <cmath>

#include "creature.h"
#include "doctest/doctest.h"
#include "particles.h"
#include "physics_test_util.h"
#include "sim_test_util.h"

namespace {

constexpr int W = 8, H = 16;  // the hitbox under test
constexpr int STEP = 3;

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

GridPass pass(Arena& a, float x0, float y0, float x1, float y1, bool can_step = true,
              ParticlePool* pool = nullptr, float speed = 0) {
	return creature_grid_pass(a.grid, pool, x0, y0, x1, y1, W, H, STEP, can_step, speed);
}

}  // namespace

TEST_CASE("walks on flat ground") {
	Arena a;
	const GridPass r = pass(a, 60, 92, 70, 92);
	CHECK(r.x == doctest::Approx(70));
	CHECK(r.y == doctest::Approx(92));
	CHECK(r.floor);
	CHECK(r.wall == 0);
	CHECK_FALSE(r.stopped_x);
}

TEST_CASE("stops at a wall") {
	Arena a;
	phystest::fill(a.grid, 80, 0, 82, 100, a.id("rock"));
	const GridPass r = pass(a, 60, 92, 90, 92);
	CHECK(r.x == doctest::Approx(76));
	CHECK(r.wall == 1);
	CHECK(r.stopped_x);
}

TEST_CASE("steps up a ledge of step_height") {
	Arena three;
	phystest::fill(three.grid, 80, 97, 256, 100, three.id("rock"));
	const GridPass up = pass(three, 60, 92, 90, 92);
	CHECK(up.x == doctest::Approx(90));
	CHECK(up.y == doctest::Approx(89));
	CHECK_FALSE(up.stopped_x);

	const GridPass no_step = pass(three, 60, 92, 90, 92, false);
	CHECK(no_step.x == doctest::Approx(76));

	Arena four;
	phystest::fill(four.grid, 80, 96, 256, 100, four.id("rock"));
	CHECK(pass(four, 60, 92, 90, 92).x == doctest::Approx(76));
}

TEST_CASE("lands on sand and climbs a sand pile") {
	Arena a;
	phystest::fill(a.grid, 0, 90, 256, 100, a.id("sand"));
	const GridPass land = pass(a, 60, 60, 60, 95);
	CHECK(land.y == doctest::Approx(82));
	CHECK(land.floor);
	phystest::fill(a.grid, 80, 87, 256, 90, a.id("sand"));
	const GridPass climb = pass(a, 60, 82, 90, 82);
	CHECK(climb.x == doctest::Approx(90));
	CHECK(climb.y == doctest::Approx(79));
}

TEST_CASE("liquid does not block and is measured") {
	Arena a;
	phystest::fill(a.grid, 0, 90, 256, 100, a.id("water"));
	const GridPass r = pass(a, 60, 60, 60, 95);
	CHECK(r.y == doctest::Approx(92));
	CHECK(r.floor);
	CHECK(r.submerged == W * 10);
	CHECK(r.liquid == a.id("water"));
}

TEST_CASE("body pixels do not block") {
	Arena a;
	Cell c = spawn_cell(a.id("rock"), a.t, 1, 80, 95);
	c.flags |= FLAG_BODY;
	REQUIRE(a.grid.put(80, 95, c));
	CHECK(pass(a, 60, 92, 90, 92).x == doctest::Approx(90));
}

TEST_CASE("no tunnelling") {
	Arena a;
	phystest::fill(a.grid, 80, 0, 81, 100, a.id("rock"));
	const GridPass across = pass(a, 20, 92, 2020, 92);
	CHECK(across.x == doctest::Approx(76));

	Arena b(0, 0, 256, 256, 200);
	phystest::fill(b.grid, 0, 100, 256, 101, b.id("rock"));
	const GridPass down = pass(b, 60, 20, 60, 2000);
	CHECK(down.y == doctest::Approx(92));
	CHECK(down.floor);
}

TEST_CASE("buried creature is freed") {
	Arena a;
	phystest::fill(a.grid, 50, 84, 70, 100, a.id("rock"));  // exactly over the box
	const GridPass rock = pass(a, 60, 92, 60, 92);
	CHECK(rock.y == doctest::Approx(76));
	CHECK(creature_blocks(a.grid, 60, (int)std::floor(rock.y + H / 2.0f)));  // standing on it

	Arena s;
	phystest::fill(s.grid, 50, 70, 70, 100, s.id("sand"));
	const int before = simtest::count_mat(s.grid, s.id("sand"));
	ParticlePool pool(s.grid, 1000);
	pass(s, 60, 92, 60, 92, true, &pool);
	for (int y = 84; y < 100; ++y)
		for (int x = 56; x < 64; ++x) CHECK(simtest::mat_at(s.grid, x, y) != s.id("sand"));
	CHECK(simtest::count_mat(s.grid, s.id("sand")) == before - (int)pool.material().size());
}

TEST_CASE("negative coordinates") {
	Arena a(-256, -256, 0, 0, -100);
	const GridPass walk = pass(a, -60, -108, -70, -108);
	CHECK(walk.x == doctest::Approx(-70));
	CHECK(walk.floor);
	phystest::fill(a.grid, -82, -200, -80, -100, a.id("rock"));
	const GridPass wall = pass(a, -60, -108, -90, -108);
	CHECK(wall.x == doctest::Approx(-76));
	CHECK(wall.wall == -1);
}
