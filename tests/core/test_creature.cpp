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

// --- creatures in PhysicsWorld ---------------------------------------------------------------

namespace {

constexpr float GRAVITY = 600.0f;

CreatureDef creature_at(float x, float y) {
	CreatureDef d;
	d.x = x;
	d.y = y;
	return d;
}

void drive(PhysicsWorld& physics, int handle, float vx) {
	const b2BodyId id = physics.body_id(handle);
	b2Body_SetLinearVelocity(id, {vx, b2Body_GetLinearVelocity(id).y});
}

}  // namespace

TEST_CASE("a creature falls and stands on the floor") {
	Arena a;
	PhysicsWorld physics(a.grid, GRAVITY);
	const int c = physics.add_creature(creature_at(128, 40));
	REQUIRE(physics.is_creature(c));
	phystest::run(physics, 120);
	CHECK(phystest::pos(physics, c).y == doctest::Approx(92).epsilon(0.01));
	CHECK(physics.creature_state(c).floor);
	CHECK(phystest::angle(physics, c) == doctest::Approx(0));
	CHECK(physics.stats().creatures == 1);
}

TEST_CASE("walking into a crate pushes it") {
	Arena a;
	PhysicsWorld physics(a.grid, GRAVITY);
	const int crate = physics.add_body(phystest::box(a.t, "wood", 12, 12, 150, 94));
	const int c = physics.add_creature(creature_at(120, 92));
	phystest::run(physics, 30);
	const float start = phystest::pos(physics, crate).x;
	for (int i = 0; i < 60; ++i) {
		drive(physics, c, 100);
		phystest::run(physics, 1);
	}
	CHECK(phystest::pos(physics, crate).x > start + 10);
}

TEST_CASE("a creature stands on a crate") {
	Arena a;
	PhysicsWorld physics(a.grid, GRAVITY);
	physics.add_body(phystest::box(a.t, "wood", 12, 12, 128, 94));
	phystest::run(physics, 30);
	const int c = physics.add_creature(creature_at(128, 50));
	phystest::run(physics, 120);
	CHECK(std::fabs(phystest::pos(physics, c).y - 80.0f) <= 1.0f);
	CHECK(physics.creature_state(c).floor);
}

TEST_CASE("two creatures don't overlap") {
	Arena a;
	PhysicsWorld physics(a.grid, GRAVITY);
	const int c1 = physics.add_creature(creature_at(100, 40));
	const int c2 = physics.add_creature(creature_at(104, 40));
	phystest::run(physics, 120);
	CHECK(std::fabs(phystest::pos(physics, c1).x - phystest::pos(physics, c2).x) >= 7.5f);
}

TEST_CASE("a crate rests on a standing creature") {
	Arena a;
	PhysicsWorld physics(a.grid, GRAVITY);
	const int c = physics.add_creature(creature_at(128, 92));
	phystest::run(physics, 30);
	const int crate = physics.add_body(phystest::box(a.t, "wood", 12, 12, 128, 40));
	phystest::run(physics, 120);
	float lo = 1e9f, hi = -1e9f;
	for (int i = 0; i < 60; ++i) {
		phystest::run(physics, 1);
		const float y = phystest::pos(physics, crate).y;
		lo = std::min(lo, y);
		hi = std::max(hi, y);
	}
	CHECK(std::fabs(phystest::pos(physics, c).y + 8.0f - 100.0f) <= 0.5f);
	CHECK(hi - lo < 0.5f);
	CHECK(hi <= 78.5f);  // on the creature's head (top at 84), not beside it
}

TEST_CASE("a crowd in a pit") {
	Arena a(0, 0, 256, 256, 200);
	phystest::fill(a.grid, 70, 100, 80, 200, a.id("rock"));
	phystest::fill(a.grid, 180, 100, 190, 200, a.id("rock"));
	PhysicsWorld physics(a.grid, GRAVITY);
	std::vector<int> crowd;
	for (int row = 0; row < 4; ++row)
		for (int col = 0; col < 10; ++col) crowd.push_back(physics.add_creature(creature_at(85 + 10 * col, 20 + 20 * row)));
	phystest::run(physics, 240);
	for (size_t i = 0; i < crowd.size(); ++i) {
		const b2Vec2 p = phystest::pos(physics, crowd[i]);
		for (int y = (int)std::floor(p.y - 8 + 0.01f); y <= (int)std::floor(p.y + 8 - 0.01f); ++y)
			for (int x = (int)std::floor(p.x - 4 + 0.01f); x <= (int)std::floor(p.x + 4 - 0.01f); ++x)
				CHECK_FALSE(creature_blocks(a.grid, x, y));
		for (size_t j = i + 1; j < crowd.size(); ++j) {
			const b2Vec2 q = phystest::pos(physics, crowd[j]);
			const float ox = 8 - std::fabs(p.x - q.x), oy = 16 - std::fabs(p.y - q.y);
			CHECK_FALSE((ox > 0.5f && oy > 0.5f));
		}
	}
}

TEST_CASE("a creature in water reports submersion") {
	Arena a;
	phystest::fill(a.grid, 0, 60, 256, 100, a.id("water"));
	PhysicsWorld physics(a.grid, GRAVITY);
	const int c = physics.add_creature(creature_at(128, 20));
	phystest::run(physics, 120);
	CHECK(physics.creature_state(c).submerged_fraction > 0.5f);
	CHECK(physics.creature_state(c).liquid == a.id("water"));
}

TEST_CASE("a creature walks down a staircase") {
	Arena a(0, 0, 256, 256, 140);
	for (int step = 0; step < 10; ++step)  // 2 cells down every 8 cells from x = 100
		phystest::fill(a.grid, 100 + 8 * step, 100, 108 + 8 * step, 102 + 2 * step, MAT_AIR);
	phystest::fill(a.grid, 0, 100, 100, 140, a.id("rock"));
	for (int step = 0; step < 10; ++step)
		phystest::fill(a.grid, 100 + 8 * step, 102 + 2 * step, 108 + 8 * step, 140, a.id("rock"));
	PhysicsWorld physics(a.grid, GRAVITY);
	const int c = physics.add_creature(creature_at(80, 92));
	phystest::run(physics, 20);
	for (int i = 0; i < 90; ++i) {
		drive(physics, c, 60);
		phystest::run(physics, 1);
	}
	drive(physics, c, 0);  // stop: mid-stride it may be in the air over a 2-cell drop
	phystest::run(physics, 10);
	const b2Vec2 p = phystest::pos(physics, c);
	REQUIRE(p.x > 150);
	const int step = (int)((p.x - 100) / 8);
	CHECK(std::fabs(p.y + 8 - (102 + 2 * std::min(step, 9))) <= 2.5f);  // on the stair under it
	CHECK(physics.creature_state(c).floor);
}

// --- review fixes ------------------------------------------------------------------------------

TEST_CASE("a creature pushed into a wall by another creature is not overlapped") {
	Arena a;
	phystest::fill(a.grid, 150, 0, 152, 100, a.id("rock"));
	PhysicsWorld physics(a.grid, GRAVITY);
	const int enemy = physics.add_creature(creature_at(140, 92));
	const int player = physics.add_creature(creature_at(110, 92));
	phystest::run(physics, 10);
	for (int i = 0; i < 120; ++i) {
		drive(physics, player, 80);
		phystest::run(physics, 1);
	}
	CHECK(phystest::pos(physics, enemy).x - phystest::pos(physics, player).x >= 7.5f);
	CHECK(phystest::pos(physics, enemy).x <= 146.01f);
}

TEST_CASE("a driven crate stops against a creature at a wall") {
	Arena a;
	phystest::fill(a.grid, 150, 0, 152, 100, a.id("rock"));
	PhysicsWorld physics(a.grid, GRAVITY);
	const int c = physics.add_creature(creature_at(140, 92));
	const int crate = physics.add_body(phystest::box(a.t, "wood", 12, 12, 100, 94));
	phystest::run(physics, 10);
	for (int i = 0; i < 120; ++i) {
		drive(physics, crate, 60);
		phystest::run(physics, 1);
	}
	// crate right edge vs creature left edge
	CHECK(phystest::pos(physics, crate).x + 6 - (phystest::pos(physics, c).x - 4) <= 0.5f);
}

TEST_CASE("walking down a slope stays on the floor") {
	Arena a(0, 0, 256, 256, 200);
	for (int x = 0; x < 256; ++x) phystest::fill(a.grid, x, 100 + std::max(0, x - 60), x + 1, 200, a.id("rock"));
	PhysicsWorld physics(a.grid, GRAVITY);
	const int c = physics.add_creature(creature_at(40, 92));
	phystest::run(physics, 20);
	int on_floor = 0;
	for (int i = 0; i < 64; ++i) {
		drive(physics, c, 60);
		phystest::run(physics, 1);
		on_floor += physics.creature_state(c).floor;
	}
	CHECK(phystest::pos(physics, c).x > 90);
	CHECK(on_floor >= 58);
}

TEST_CASE("game code can accelerate a creature on the ground") {
	Arena a;
	PhysicsWorld physics(a.grid, GRAVITY);
	const int c = physics.add_creature(creature_at(60, 92));
	phystest::run(physics, 20);
	for (int i = 0; i < 60; ++i) {
		const b2Vec2 v = b2Body_GetLinearVelocity(physics.body_id(c));
		drive(physics, c, std::min(80.0f, v.x + 300.0f / 60.0f));  // move_toward(vx, 80, 300 * dt)
		phystest::run(physics, 1);
	}
	CHECK(b2Body_GetLinearVelocity(physics.body_id(c)).x >= 75.0f);
}

TEST_CASE("a creature buried deep in rock is freed") {
	Arena a;
	phystest::fill(a.grid, 50, 50, 70, 100, a.id("rock"));  // 34 cells above the box top
	const GridPass r = pass(a, 60, 92, 60, 92);
	CHECK(r.y <= 42.0f);
}
