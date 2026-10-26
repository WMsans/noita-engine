#include "box2d/box2d.h"
#include "doctest/doctest.h"

TEST_CASE("box2d links and a box falls") {
	b2SetLengthUnitsPerMeter(10.0f);
	b2WorldDef wd = b2DefaultWorldDef();
	wd.gravity = {0.0f, 600.0f};
	const b2WorldId world = b2CreateWorld(&wd);
	b2BodyDef bd = b2DefaultBodyDef();
	bd.type = b2_dynamicBody;
	const b2BodyId body = b2CreateBody(world, &bd);
	const b2ShapeDef sd = b2DefaultShapeDef();
	const b2Polygon box = b2MakeBox(2.0f, 2.0f);
	b2CreatePolygonShape(body, &sd, &box);
	for (int i = 0; i < 60; ++i) b2World_Step(world, 1.0f / 60.0f, 4);
	CHECK(b2Body_GetPosition(body).y > 200.0f);
	b2DestroyWorld(world);
}

#include "physics_test_util.h"

namespace {

constexpr int SIZE = 256;  // 4x4 chunks
constexpr float GRAVITY = 600.0f;

struct World {
	MaterialTable t = phystest::table();
	WorldGrid grid{t, 1};
	// One row of air chunks above y = 0, so a body spawned near the top keeps loaded chunks
	// all around it (a body next to an unloaded chunk is disabled).
	explicit World(int w = SIZE, int h = SIZE) { phystest::arena(grid, t, 0, -CHUNK, w, h); }
	MatId id(const char* name) const { return t.id_of(name); }
};

}  // namespace

TEST_CASE("a crate rests on a rock floor") {
	World w;
	phystest::fill(w.grid, 0, 100, SIZE, SIZE, w.id("rock"));
	PhysicsWorld physics(w.grid, GRAVITY);
	const int crate = physics.add_body(phystest::box(w.t, "wood", 12, 12, 128, 20));
	REQUIRE(crate >= 0);
	phystest::run(physics, 180);
	CHECK(std::fabs(phystest::pos(physics, crate).y - 94.0f) <= 1.0f);
	CHECK(std::fabs(phystest::angle(physics, crate)) < 0.05f);
	CHECK(b2Length(b2Body_GetLinearVelocity(physics.body_id(crate))) < 1.0f);
}

TEST_CASE("a crate rests on sand") {
	World w;
	phystest::fill(w.grid, 0, 100, SIZE, 120, w.id("sand"));
	phystest::fill(w.grid, 0, 120, SIZE, SIZE, w.id("rock"));
	PhysicsWorld physics(w.grid, GRAVITY);
	const int crate = physics.add_body(phystest::box(w.t, "wood", 12, 12, 128, 20));
	phystest::run(physics, 180);
	CHECK(std::fabs(phystest::pos(physics, crate).y - 94.0f) <= 1.0f);
}

TEST_CASE("digging under a resting body drops it") {
	World w;
	phystest::fill(w.grid, 0, 100, SIZE, SIZE, w.id("rock"));
	PhysicsWorld physics(w.grid, GRAVITY);
	const int crate = physics.add_body(phystest::box(w.t, "wood", 12, 12, 128, 20));
	phystest::run(physics, 180);
	phystest::fill(w.grid, 110, 100, 146, 120, MAT_AIR);
	phystest::run(physics, 120);
	CHECK(phystest::pos(physics, crate).y >= 104.0f);
}

TEST_CASE("fast body does not tunnel") {
	World w(128, 704);
	phystest::fill(w.grid, 0, 600, 128, 602, w.id("rock"));
	PhysicsWorld physics(w.grid, GRAVITY);
	const int crate = physics.add_body(phystest::box(w.t, "wood", 12, 12, 64, 10));
	b2Body_SetLinearVelocity(physics.body_id(crate), {0.0f, 2000.0f});
	phystest::run(physics, 120);
	CHECK(phystest::pos(physics, crate).y < 600.0f);
}

TEST_CASE("hit events report terrain hits") {
	World w;
	phystest::fill(w.grid, 0, 100, SIZE, SIZE, w.id("rock"));
	PhysicsWorld physics(w.grid, GRAVITY);
	const int crate = physics.add_body(phystest::box(w.t, "wood", 12, 12, 128, 30));
	bool hit = false;
	for (int i = 0; i < 120 && !hit; ++i) {
		physics.update_terrain();
		physics.step();
		for (const BodyHit& h : physics.hits())
			if (h.a == crate && h.b == -1 && h.speed > 10.0f) hit = true;
	}
	CHECK(hit);
}

TEST_CASE("bodies outside the active rect are disabled") {
	World w;
	phystest::fill(w.grid, 0, 100, SIZE, SIZE, w.id("rock"));
	PhysicsWorld physics(w.grid, GRAVITY);
	const int crate = physics.add_body(phystest::box(w.t, "wood", 12, 12, 128, 20));
	physics.set_active_rect(ChunkRect{3, 3, 3, 3});
	phystest::run(physics, 1);
	CHECK_FALSE(b2Body_IsEnabled(physics.body_id(crate)));
	physics.set_active_rect(ChunkRect{0, 0, 3, 3});
	phystest::run(physics, 1);
	CHECK(b2Body_IsEnabled(physics.body_id(crate)));
}

TEST_CASE("a body next to an unloaded chunk is disabled") {
	World w(128, 128);
	PhysicsWorld physics(w.grid, GRAVITY);
	const int crate = physics.add_body(phystest::box(w.t, "wood", 12, 12, 120, 50));
	phystest::run(physics, 1);
	CHECK_FALSE(b2Body_IsEnabled(physics.body_id(crate)));
}

TEST_CASE("unloading a chunk destroys its terrain body") {
	World w;
	phystest::fill(w.grid, 0, 100, SIZE, SIZE, w.id("rock"));
	PhysicsWorld physics(w.grid, GRAVITY);
	const int crate = physics.add_body(phystest::box(w.t, "wood", 12, 12, 160, 90));
	phystest::run(physics, 30);
	const int before = physics.stats().terrain_bodies;
	REQUIRE(before > 0);
	w.grid.unload({2, 1});
	phystest::run(physics, 1);
	CHECK(physics.stats().terrain_bodies < before);
	const std::vector<int> left = physics.take_left_world();
	REQUIRE(left.size() == 1);
	CHECK(left[0] == crate);
	phystest::run(physics, 1);
	CHECK(physics.take_left_world().empty());
}

TEST_CASE("raycast hits a body") {
	World w;
	phystest::fill(w.grid, 0, 100, SIZE, SIZE, w.id("rock"));
	PhysicsWorld physics(w.grid, GRAVITY);
	const int crate = physics.add_body(phystest::box(w.t, "wood", 12, 12, 128, 20));
	phystest::run(physics, 180);
	int handle = -1;
	float hx, hy, nx, ny;
	REQUIRE(physics.raycast(10, 94, 250, 94, handle, hx, hy, nx, ny));
	CHECK(handle == crate);
	CHECK(std::fabs(hx - 122.0f) <= 1.0f);
	CHECK(nx < -0.9f);
}

TEST_CASE("overlapping spawn does not explode") {
	World w;
	phystest::fill(w.grid, 0, 100, SIZE, SIZE, w.id("rock"));
	PhysicsWorld physics(w.grid, GRAVITY);
	const int a = physics.add_body(phystest::box(w.t, "wood", 12, 12, 128, 60));
	const int b = physics.add_body(phystest::box(w.t, "wood", 12, 12, 128, 60));
	const int c = physics.add_body(phystest::box(w.t, "wood", 12, 12, 64, 100));
	phystest::run(physics, 120);
	for (int h : {a, b, c}) {
		const b2Vec2 p = phystest::pos(physics, h);
		CHECK(std::isfinite(p.x));
		CHECK(std::isfinite(p.y));
		CHECK(std::isfinite(phystest::angle(physics, h)));
	}
}

TEST_CASE("a body with no opaque pixels is refused") {
	World w;
	PhysicsWorld physics(w.grid, GRAVITY);
	BodyDef d = phystest::box(w.t, "wood", 4, 4, 128, 20);
	d.pixels.assign(16, NO_MAT);
	CHECK(physics.add_body(d) == -1);
	CHECK(physics.stats().bodies == 0);
}
