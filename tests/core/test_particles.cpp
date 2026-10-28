#include <cmath>
#include <limits>

#include "cell.h"
#include "coords.h"
#include "doctest/doctest.h"
#include "generator.h"
#include "material.h"
#include "particles.h"
#include "sim_test_util.h"
#include "world_grid.h"

namespace {

constexpr int ARENA = 128;  // 2x2 chunks of air; everything outside is unloaded (solid)

void arena(WorldGrid& grid, const MaterialTable& table) {
	simtest::load_arena(grid, table, 0, 0, ARENA, ARENA, table.id_of("rock"));
}

Cell sand_cell(const MaterialTable& table, int16_t temp = AMBIENT_TEMP) {
	Cell c = spawn_cell(table.id_of("sand"), table, 1, 0, 0);
	c.temp = temp;
	return c;
}

void step_n(ParticlePool& pool, int n) {
	for (int i = 0; i < n; ++i) pool.step();
}

}  // namespace

TEST_CASE("put writes the exact cell and wakes it") {
	MaterialTable table = simtest::movement_table();
	WorldGrid grid(table, 1);
	arena(grid, table);
	Chunk* chunk = grid.find({0, 0});
	chunk->next.reset();

	Cell c = sand_cell(table, 77);
	c.shade = 9;
	REQUIRE(grid.put(10, 10, c));
	Cell out{};
	REQUIRE(grid.get(10, 10, out));
	CHECK(out.mat == table.id_of("sand"));
	CHECK(out.temp == 77);
	CHECK(out.shade == 9);
	CHECK_FALSE(chunk->next.empty());
	CHECK_FALSE(grid.put(-10, 10, c));
}

TEST_CASE("material particle falls and lands as its cell") {
	MaterialTable table = simtest::movement_table();
	const MatId sand = table.id_of("sand");
	WorldGrid grid(table, 1);
	arena(grid, table);
	simtest::paint(grid, {{0, 100, ARENA, ARENA, table.id_of("rock")}});
	ParticlePool pool(grid, 100);

	REQUIRE(pool.emit(60.5f, 10.5f, 0, 0, sand_cell(table, 55)));
	step_n(pool, 120);

	CHECK(pool.material().empty());
	Cell out{};
	REQUIRE(grid.get(60, 99, out));
	CHECK(out.mat == sand);
	CHECK(out.temp == 55);
	CHECK(simtest::count_mat(grid, sand) == 1);
}

TEST_CASE("thrown particle follows an arc and does not tunnel") {
	MaterialTable table = simtest::movement_table();
	const MatId sand = table.id_of("sand");
	WorldGrid grid(table, 1);
	arena(grid, table);
	simtest::paint(grid, {{80, 0, 81, ARENA, table.id_of("rock")}});
	ParticlePool pool(grid, 100);

	REQUIRE(pool.emit(20.5f, 50.5f, 16, -4, sand_cell(table)));
	step_n(pool, 200);

	const simtest::MatPos p = simtest::locate(grid, sand);
	CHECK(p.count == 1);
	CHECK(p.x < 80);
}

TEST_CASE("landing searches back along the path") {
	MaterialTable table = simtest::movement_table();
	const MatId sand = table.id_of("sand");
	WorldGrid grid(table, 1);
	arena(grid, table);
	simtest::paint(grid, {{0, 100, ARENA, ARENA, table.id_of("rock")}});
	ParticlePool pool(grid, 100);

	REQUIRE(pool.emit(60.5f, 98.5f, 0, 0, sand_cell(table)));
	REQUIRE(grid.set(60, 98, table.id_of("rock")));
	pool.step();

	CHECK(pool.material().empty());
	CHECK(simtest::mat_at(grid, 60, 97) == sand);
	CHECK(simtest::count_mat(grid, sand) == 1);
}

TEST_CASE("unloaded ground blocks; a chunk unloaded under a particle drops it") {
	MaterialTable table = simtest::movement_table();
	WorldGrid grid(table, 1);
	simtest::load_arena(grid, table, 0, 0, CHUNK, CHUNK, table.id_of("rock"));
	ParticlePool pool(grid, 100);

	REQUIRE(pool.emit(60.5f, 30.5f, 16, -0.25f, sand_cell(table)));
	pool.step();
	CHECK(pool.material().empty());
	CHECK(simtest::mat_at(grid, 63, 30) == table.id_of("sand"));
	CHECK(pool.stats().dropped == 0);

	REQUIRE(pool.emit(30.5f, 10.5f, 0, 0, sand_cell(table)));
	grid.unload({0, 0});
	pool.step();
	CHECK(pool.stats().dropped == 1);
	CHECK(pool.material().empty());
}

TEST_CASE("negative coordinates use floor") {
	MaterialTable table = simtest::movement_table();
	const MatId sand = table.id_of("sand");
	WorldGrid grid(table, 1);
	simtest::load_arena(grid, table, -ARENA, -ARENA, 0, 0, table.id_of("rock"));
	simtest::paint(grid, {{-ARENA, -20, 0, 0, table.id_of("rock")}});
	ParticlePool pool(grid, 100);

	REQUIRE(pool.emit(-0.5f, -100.5f, 0, 0, sand_cell(table)));
	step_n(pool, 120);

	CHECK(simtest::mat_at(grid, -1, -21) == sand);
	CHECK(simtest::count_mat(grid, sand) == 1);
}

TEST_CASE("visual particle fades out without touching the grid") {
	MaterialTable table = simtest::movement_table();
	const MatId rock = table.id_of("rock");
	WorldGrid grid(table, 1);
	arena(grid, table);
	simtest::paint(grid, {{40, 40, 80, 80, rock}});
	ParticlePool pool(grid, 100);

	VisualParticle v{};
	v.x = 60.5f;
	v.y = 60.5f;
	v.life = v.lifetime = 30;
	v.collides = false;
	REQUIRE(pool.emit_visual(v));
	step_n(pool, 29);
	CHECK(pool.visual().size() == 1);
	pool.step();
	CHECK(pool.visual().empty());
	CHECK(simtest::count_mat(grid, rock) == 40 * 40);
}

TEST_CASE("colliding visual particle dies on solid") {
	MaterialTable table = simtest::movement_table();
	const MatId rock = table.id_of("rock");
	WorldGrid grid(table, 1);
	arena(grid, table);
	simtest::paint(grid, {{0, 100, ARENA, ARENA, rock}});
	ParticlePool pool(grid, 100);

	VisualParticle v{};
	v.x = 60.5f;
	v.y = 10.5f;
	v.life = v.lifetime = 600;
	REQUIRE(pool.emit_visual(v));
	step_n(pool, 100);
	CHECK(pool.visual().empty());
	CHECK(simtest::count_mat(grid, rock) == ARENA * (ARENA - 100));
	CHECK(simtest::count_mat(grid, MAT_AIR) == ARENA * 100);
}

TEST_CASE("emit sanitises input") {
	MaterialTable table = simtest::movement_table();
	WorldGrid grid(table, 1);
	arena(grid, table);
	ParticlePool pool(grid, 100);

	REQUIRE(pool.emit(10.5f, 10.5f, std::numeric_limits<float>::quiet_NaN(), 1e9f, sand_cell(table)));
	REQUIRE(pool.material().size() == 1);
	CHECK(pool.material()[0].vx == 0.0f);
	CHECK(pool.material()[0].vy == MAX_PARTICLE_SPEED);
}

TEST_CASE("full pool writes back or drops") {
	MaterialTable table = simtest::movement_table();
	const MatId sand = table.id_of("sand");
	WorldGrid grid(table, 1);
	arena(grid, table);
	REQUIRE(grid.set(50, 50, table.id_of("rock")));
	ParticlePool pool(grid, 2);

	CHECK(pool.emit(10.5f, 10.5f, 0, 0, sand_cell(table)));
	CHECK(pool.emit(20.5f, 10.5f, 0, 0, sand_cell(table)));
	CHECK_FALSE(pool.emit(30.5f, 30.5f, 0, 0, sand_cell(table)));
	CHECK(simtest::mat_at(grid, 30, 30) == sand);
	CHECK_FALSE(pool.emit(50.5f, 50.5f, 0, 0, sand_cell(table)));
	CHECK(simtest::mat_at(grid, 50, 50) == table.id_of("rock"));
	CHECK(pool.stats().dropped == 1);
}

namespace {

int non_air_in_disc(WorldGrid& grid, int cx, int cy, int r) {
	int n = 0;
	for (int dy = -r; dy <= r; ++dy)
		for (int dx = -r; dx <= r; ++dx)
			if (dx * dx + dy * dy <= r * r && simtest::mat_at(grid, cx + dx, cy + dy) != MAT_AIR) ++n;
	return n;
}

void sand_block_in_rock(WorldGrid& grid, const MaterialTable& table) {
	arena(grid, table);
	simtest::paint(grid, {{0, 0, ARENA, ARENA, table.id_of("rock")}, {40, 40, 80, 80, table.id_of("sand")}});
}

}  // namespace

TEST_CASE("explode clears the radius and conserves the count") {
	MaterialTable table = simtest::movement_table();
	WorldGrid grid(table, 1);
	sand_block_in_rock(grid, table);
	ParticlePool pool(grid, DEFAULT_PARTICLE_CAPACITY);
	const int before = non_air_in_disc(grid, 60, 60, 10);

	const ExplodeResult r = explode(grid, pool, 60, 60, 10, 8.0f, 1);

	CHECK(non_air_in_disc(grid, 60, 60, 10) == 0);
	CHECK(r.removed == before);
	CHECK(r.removed == r.thrown + r.vanished);
	CHECK(r.thrown == (int)pool.material().size());
	CHECK(r.thrown > 0);
}

TEST_CASE("explode with a full pool still conserves") {
	MaterialTable table = simtest::movement_table();
	WorldGrid grid(table, 1);
	sand_block_in_rock(grid, table);
	ParticlePool pool(grid, 5);

	const ExplodeResult r = explode(grid, pool, 60, 60, 10, 8.0f, 1);

	CHECK(r.removed == r.thrown + r.vanished);
	CHECK(r.thrown <= 5);
	CHECK(r.thrown == (int)pool.material().size());
}

TEST_CASE("explode leaves body pixels and solids outside alone") {
	MaterialTable table = simtest::movement_table();
	const MatId rock = table.id_of("rock");
	WorldGrid grid(table, 1);
	sand_block_in_rock(grid, table);
	Cell body = spawn_cell(rock, table, 1, 62, 60);
	body.flags |= FLAG_BODY;
	REQUIRE(grid.put(62, 60, body));
	ParticlePool pool(grid, DEFAULT_PARTICLE_CAPACITY);

	explode(grid, pool, 60, 60, 10, 8.0f, 1);

	Cell out{};
	REQUIRE(grid.get(62, 60, out));
	CHECK(out.mat == rock);
	CHECK((out.flags & FLAG_BODY) != 0);
	CHECK(simtest::mat_at(grid, 71, 60) == table.id_of("sand"));  // radius + 1, inside the block
	CHECK(simtest::mat_at(grid, 60, 29) == rock);                  // well outside
}

TEST_CASE("explode radius zero is a no-op") {
	MaterialTable table = simtest::movement_table();
	WorldGrid grid(table, 1);
	sand_block_in_rock(grid, table);
	ParticlePool pool(grid, DEFAULT_PARTICLE_CAPACITY);
	const int sand = simtest::count_mat(grid, table.id_of("sand"));

	for (int radius : {0, -3}) {
		const ExplodeResult r = explode(grid, pool, 60, 60, radius, 8.0f, 1);
		CHECK(r.removed == 0);
		CHECK(r.thrown == 0);
		CHECK(r.vanished == 0);
	}
	CHECK(simtest::count_mat(grid, table.id_of("sand")) == sand);
	CHECK(pool.material().empty());
}

TEST_CASE("explode across the origin") {
	MaterialTable table = simtest::movement_table();
	WorldGrid grid(table, 1);
	simtest::load_arena(grid, table, -64, -64, 64, 64, table.id_of("rock"));
	simtest::paint(grid, {{-64, -64, 64, 64, table.id_of("sand")}});
	ParticlePool pool(grid, DEFAULT_PARTICLE_CAPACITY);

	explode(grid, pool, 0, 0, 8, 8.0f, 1);

	CHECK(simtest::mat_at(grid, -8, 0) == MAT_AIR);
	CHECK(simtest::mat_at(grid, 7, 0) == MAT_AIR);
	CHECK(simtest::mat_at(grid, 0, -8) == MAT_AIR);
	CHECK(simtest::mat_at(grid, 0, 7) == MAT_AIR);
	CHECK(simtest::mat_at(grid, 9, 0) == table.id_of("sand"));
}

TEST_CASE("a particle blocked inside gas lands in the gas") {
	MaterialTable table = simtest::movement_table();
	const MatId sand = table.id_of("sand");
	WorldGrid grid(table, 1);
	arena(grid, table);
	simtest::paint(grid, {{0, 60, ARENA, 100, table.id_of("steam")}, {0, 100, ARENA, ARENA, table.id_of("rock")}});
	ParticlePool pool(grid, 100);

	REQUIRE(pool.emit(60.5f, 10.5f, 0, 0, sand_cell(table)));
	step_n(pool, 120);

	CHECK(pool.stats().dropped == 0);
	CHECK(simtest::mat_at(grid, 60, 99) == sand);
}

TEST_CASE("ejecta of a blast inside rock all land in the crater") {
	MaterialTable table = simtest::movement_table();
	const MatId sand = table.id_of("sand");
	WorldGrid grid(table, 1);
	arena(grid, table);
	simtest::paint(grid, {{0, 0, ARENA, ARENA, table.id_of("rock")}});
	for (int dy = -14; dy <= 14; ++dy)
		for (int dx = -14; dx <= 14; ++dx)
			if (dx * dx + dy * dy <= 14 * 14) REQUIRE(grid.set(64 + dx, 64 + dy, sand));
	ParticlePool pool(grid, DEFAULT_PARTICLE_CAPACITY);

	const ExplodeResult r = explode(grid, pool, 64, 64, 14, 400.0f / 60.0f, 1);
	step_n(pool, 120);

	CHECK(pool.material().empty());
	CHECK(pool.stats().dropped == 0);
	CHECK(simtest::count_mat(grid, sand) == r.thrown);
}
