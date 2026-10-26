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

#include <cstring>

#include "physics_test_util.h"
#include "sim_test_util.h"

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

namespace {

int count_body_cells(WorldGrid& grid) {
	int n = 0;
	grid.for_each_chunk([&](Chunk& c) {
		for (const Cell& cell : c.cells)
			if (cell.flags & FLAG_BODY) ++n;
	});
	return n;
}

std::vector<Cell> snapshot(WorldGrid& grid, const std::vector<ChunkCoord>& coords) {
	std::vector<Cell> out;
	for (ChunkCoord c : coords) {
		Chunk* chunk = grid.find(c);
		REQUIRE(chunk != nullptr);
		out.insert(out.end(), chunk->cells.begin(), chunk->cells.end());
	}
	return out;
}

bool same_cells(const std::vector<Cell>& a, const std::vector<Cell>& b) {
	if (a.size() != b.size()) return false;
	for (size_t i = 0; i < a.size(); ++i)
		if (std::memcmp(&a[i], &b[i], sizeof(Cell)) != 0) return false;
	return true;
}

const std::vector<ChunkCoord> ALL_CHUNKS = [] {
	std::vector<ChunkCoord> v;
	for (int y = -1; y < 4; ++y)
		for (int x = 0; x < 4; ++x) v.push_back({x, y});
	return v;
}();

// The full tick in PixelWorld's order.
void tick_all(PhysicsWorld& physics, Simulator& sim, int ticks) {
	for (int i = 0; i < ticks; ++i) {
		physics.update_terrain();
		physics.step();
		physics.stamp();
		sim.tick();
		physics.unstamp();
	}
}

}  // namespace

TEST_CASE("stamp and unstamp restore held cells exactly") {
	World w;
	phystest::fill(w.grid, 0, 100, SIZE, SIZE, w.id("rock"));
	phystest::fill(w.grid, 0, 60, SIZE, 100, w.id("water"));
	PhysicsWorld physics(w.grid, GRAVITY);
	physics.add_body(phystest::box(w.t, "wood", 12, 12, 128, 62));
	const std::vector<Cell> before = snapshot(w.grid, ALL_CHUNKS);

	physics.stamp();
	Cell c{};
	REQUIRE(w.grid.get(128, 62, c));
	CHECK(c.mat == w.id("wood"));
	CHECK((c.flags & FLAG_BODY) != 0);
	CHECK(count_body_cells(w.grid) == 144);
	physics.unstamp();

	CHECK(count_body_cells(w.grid) == 0);
	CHECK(same_cells(before, snapshot(w.grid, ALL_CHUNKS)));
}

TEST_CASE("sand piles on a body") {
	World w;
	phystest::fill(w.grid, 0, 100, SIZE, SIZE, w.id("rock"));
	PhysicsWorld physics(w.grid, GRAVITY);
	Simulator sim(w.grid);
	const int crate = physics.add_body(phystest::box(w.t, "wood", 12, 12, 128, 40));
	tick_all(physics, sim, 120);
	phystest::fill(w.grid, 126, 10, 131, 40, w.id("sand"));
	tick_all(physics, sim, 200);

	CHECK(simtest::mat_at(w.grid, 128, 87) == w.id("sand"));
	for (int y = 88; y < 100; ++y)
		for (int x = 122; x < 134; ++x) CHECK(simtest::mat_at(w.grid, x, y) != w.id("sand"));
	// Sand resting on the crate makes no terrain shape pressing down on it.
	CHECK(std::fabs(phystest::pos(physics, crate).y - 94.0f) <= 1.0f);
	CHECK(std::fabs(phystest::angle(physics, crate)) < 0.05f);
}

TEST_CASE("stamped pixels are inert") {
	World w;
	phystest::fill(w.grid, 0, 100, SIZE, SIZE, w.id("wood"));  // acid doesn't eat wood
	PhysicsWorld physics(w.grid, GRAVITY);
	Simulator sim(w.grid);
	physics.add_body(phystest::box(w.t, "rock", 4, 6, 128, 97));
	phystest::fill(w.grid, 130, 94, 133, 100, w.id("acid"));
	const int acid = simtest::count_mat(w.grid, w.id("acid"));
	for (int i = 0; i < 30; ++i) {
		physics.stamp();
		sim.tick();
		physics.unstamp();
	}
	CHECK(simtest::count_mat(w.grid, w.id("acid")) == acid);

	const int sand_body = physics.add_body(phystest::box(w.t, "sand", 4, 4, 64, 40));
	REQUIRE(sand_body >= 0);
	for (int i = 0; i < 5; ++i) {
		physics.stamp();
		sim.tick();
		Cell c{};
		REQUIRE(w.grid.get(64, 40, c));
		CHECK(c.mat == w.id("sand"));
		CHECK((c.flags & FLAG_BODY) != 0);
		physics.unstamp();
	}
}

TEST_CASE("stamping never overwrites terrain or another body") {
	World w;
	phystest::fill(w.grid, 0, 100, SIZE, SIZE, w.id("rock"));
	PhysicsWorld physics(w.grid, GRAVITY);
	physics.add_body(phystest::box(w.t, "wood", 12, 12, 128, 100));
	physics.add_body(phystest::box(w.t, "wood", 12, 12, 131, 100));
	const int rock = simtest::count_mat(w.grid, w.id("rock"));
	const std::vector<Cell> before = snapshot(w.grid, ALL_CHUNKS);

	physics.stamp();
	CHECK(simtest::count_mat(w.grid, w.id("rock")) == rock);
	CHECK(count_body_cells(w.grid) == 12 * 6 + 3 * 6);  // upper halves; the overlap stamps once
	physics.unstamp();
	CHECK(count_body_cells(w.grid) == 0);
	CHECK(same_cells(before, snapshot(w.grid, ALL_CHUNKS)));
}

TEST_CASE("wood floats and rock sinks") {
	World w;
	phystest::fill(w.grid, 0, 140, SIZE, SIZE, w.id("rock"));
	phystest::fill(w.grid, 0, 100, SIZE, 140, w.id("water"));
	PhysicsWorld physics(w.grid, GRAVITY);
	Simulator sim(w.grid);
	const int wood = physics.add_body(phystest::box(w.t, "wood", 12, 12, 80, 60));
	const int rock = physics.add_body(phystest::box(w.t, "rock", 12, 12, 176, 60));
	tick_all(physics, sim, 400);

	const float wood_y = phystest::pos(physics, wood).y;
	CHECK(wood_y < 120.0f);
	CHECK(std::fabs(wood_y - 100.0f) <= 8.0f);
	CHECK(std::fabs(phystest::pos(physics, rock).y - 134.0f) <= 1.5f);
}
