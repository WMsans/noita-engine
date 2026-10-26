#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "cell.h"
#include "chunk.h"
#include "coords.h"
#include "doctest/doctest.h"
#include "job_pool.h"
#include "light_input.h"
#include "material.h"
#include "world_grid.h"

namespace {

// air, rock (opaque), lava (emission 1/0.45/0.1 at energy 2, opacity 0.3), wood (opaque).
struct LightTable {
	MaterialTable table;
	MatId rock = MAT_AIR, lava = MAT_AIR, wood = MAT_AIR;

	LightTable() {
		std::vector<MaterialDef> defs(3);
		defs[0].name = "rock";
		defs[0].cls = MatClass::Solid;
		defs[1].name = "lava";
		defs[1].cls = MatClass::Liquid;
		defs[1].emission_r = 1.0f;
		defs[1].emission_g = 0.45f;
		defs[1].emission_b = 0.1f;
		defs[1].emission_energy = 2.0f;
		defs[1].light_opacity = 0.3f;
		defs[2].name = "wood";
		defs[2].cls = MatClass::Solid;
		std::string error;
		REQUIRE_MESSAGE(MaterialTable::compile(defs, {}, table, error), error);
		rock = table.id_of("rock");
		lava = table.id_of("lava");
		wood = table.id_of("wood");
	}
};

const LightTexel DARK{0, 0, 0, 255};
const LightTexel CLEAR{0, 0, 0, 0};

std::unique_ptr<Chunk> empty_chunk(ChunkCoord coord) {
	auto chunk = std::make_unique<Chunk>();
	chunk->coord = coord;
	for (Cell& c : chunk->cells) c = Cell{};
	return chunk;
}

// The fire glow the brief fixes for every palette in this file.
LightPalette fire_palette(const MaterialTable& table) {
	return LightPalette::build(table, 1.0f, 0.6f, 0.2f, 1.5f);
}

std::vector<LightTexel> build(const WorldGrid& grid, const LightPalette& pal, const LightArea& area,
                             JobPool* pool = nullptr) {
	std::vector<LightTexel> out((size_t)area.w * area.h);
	build_light_inputs(grid, pal, area, out.data(), pool);
	return out;
}

}  // namespace

TEST_CASE("light area covers view plus margin, snapped") {
	const LightArea a = light_area(10.5f, -3.2f, 480, 270, 240, 32);
	CHECK(a.x0 == -256);
	CHECK(a.y0 == -256);
	CHECK(a.w == 992);
	CHECK(a.h == 768);

	// Less than half a cell of camera drift must not move the lit area.
	const LightArea b = light_area(10.9f, -2.8f, 480, 270, 240, 32);
	CHECK(b.x0 == a.x0);
	CHECK(b.y0 == a.y0);
	CHECK(b.w == a.w);
	CHECK(b.h == a.h);
}

TEST_CASE("materials pack emission and opacity") {
	LightTable lt;
	WorldGrid grid(lt.table, 1);
	Chunk& chunk = *grid.insert(empty_chunk({0, 0}));
	chunk.at(1, 0).mat = lt.rock;
	chunk.at(2, 0).mat = lt.lava;

	const std::vector<LightTexel> out = build(grid, fire_palette(lt.table), {0, 0, 3, 1});
	REQUIRE(out.size() == 3);
	CHECK(std::memcmp(&out[0], &CLEAR, sizeof(LightTexel)) == 0);  // air
	const LightTexel rock{0, 0, 0, 255};
	const LightTexel lava{128, 57, 13, 77};
	CHECK(std::memcmp(&out[1], &rock, sizeof(LightTexel)) == 0);
	CHECK(std::memcmp(&out[2], &lava, sizeof(LightTexel)) == 0);
}

TEST_CASE("burning cells glow with the fire colour") {
	LightTable lt;
	WorldGrid grid(lt.table, 1);
	Chunk& chunk = *grid.insert(empty_chunk({0, 0}));
	chunk.at(0, 0).mat = lt.wood;
	chunk.at(0, 0).flags |= FLAG_BURNING;
	chunk.at(1, 0).mat = lt.lava;  // emission g/b below the fire glow, r above it
	chunk.at(1, 0).flags |= FLAG_BURNING;
	chunk.at(2, 0).mat = lt.wood;  // not burning: no glow

	const std::vector<LightTexel> out = build(grid, fire_palette(lt.table), {0, 0, 3, 1});
	REQUIRE(out.size() == 3);
	const LightTexel fire{96, 57, 19, 255};
	CHECK(std::memcmp(&out[0], &fire, sizeof(LightTexel)) == 0);
	const LightTexel both{128, 57, 19, 77};  // per-channel max of lava and fire
	CHECK(std::memcmp(&out[1], &both, sizeof(LightTexel)) == 0);
	CHECK(std::memcmp(&out[2], &DARK, sizeof(LightTexel)) == 0);
}

TEST_CASE("unloaded cells are opaque and dark") {
	LightTable lt;
	WorldGrid grid(lt.table, 1);
	const std::vector<LightTexel> out = build(grid, fire_palette(lt.table), {0, 0, 8, 8});
	REQUIRE(out.size() == 64);
	for (const LightTexel& t : out) CHECK(std::memcmp(&t, &DARK, sizeof(LightTexel)) == 0);
}

TEST_CASE("negative coordinates") {
	LightTable lt;
	WorldGrid grid(lt.table, 1);
	Chunk& chunk = *grid.insert(empty_chunk({-2, -1}));
	chunk.at(28, 14).mat = lt.lava;  // world (-100, -50)

	const LightArea area{-128, -64, 64, 64};
	const std::vector<LightTexel> out = build(grid, fire_palette(lt.table), area);
	REQUIRE(out.size() == 64 * 64);
	const LightTexel lava{128, 57, 13, 77};
	int hits = 0;
	for (const LightTexel& t : out) hits += std::memcmp(&t, &lava, sizeof(LightTexel)) == 0;
	CHECK(hits == 1);
	CHECK(std::memcmp(&out[(size_t)14 * 64 + 28], &lava, sizeof(LightTexel)) == 0);
}

TEST_CASE("parallel build matches serial") {
	LightTable lt;
	WorldGrid grid(lt.table, 7);
	const MatId cycle[3] = {lt.rock, lt.lava, lt.wood};
	for (int cy = -1; cy <= 1; ++cy)
		for (int cx = -1; cx <= 1; ++cx) {
			Chunk& chunk = *grid.insert(empty_chunk({cx, cy}));
			for (int ly = 0; ly < CHUNK; ++ly)
				for (int lx = 0; lx < CHUNK; ++lx) {
					const uint32_t h = (uint32_t)(lx * 31 + ly * 17 + cx * 5 + cy * 3);
					chunk.at(lx, ly).mat = cycle[h % 3];
					if (h % 11 == 0) chunk.at(lx, ly).flags |= FLAG_BURNING;
				}
		}

	const LightPalette pal = fire_palette(lt.table);
	const LightArea area{-32, -32, 192, 192};  // reaches past the loaded chunks
	JobPool pool(4);
	const std::vector<LightTexel> par = build(grid, pal, area, &pool);
	const std::vector<LightTexel> ser = build(grid, pal, area, nullptr);
	REQUIRE(par.size() == ser.size());
	CHECK(std::memcmp(par.data(), ser.data(), par.size() * sizeof(LightTexel)) == 0);
}

TEST_CASE("spots add a disc and saturate") {
	const LightArea area{0, 0, 64, 64};
	std::vector<LightTexel> out((size_t)area.w * area.h, LightTexel{0, 0, 0, 9});
	const LightSpot spot{40.5f, 40.5f, 1.0f, 1.0f, 1.0f, 2.0f, 16.0f};
	add_light_spots(area, {spot}, out.data());

	const LightTexel& centre = out[(size_t)40 * area.w + 40];
	CHECK(centre.r > 0);
	CHECK(centre.g > 0);
	CHECK(centre.b > 0);
	CHECK(centre.a == 9);  // opacity untouched
	const LightTexel unchanged{0, 0, 0, 9};
	const LightTexel outside = out[(size_t)40 * area.w + 44];
	CHECK(std::memcmp(&outside, &unchanged, sizeof(LightTexel)) == 0);

	// A huge energy saturates instead of wrapping around.
	std::vector<LightTexel> hot((size_t)area.w * area.h, LightTexel{0, 0, 0, 0});
	add_light_spots(area, {LightSpot{40.5f, 40.5f, 1.0f, 1.0f, 1.0f, 1000.0f, 16.0f}},
	                hot.data());
	const LightTexel& peak = hot[(size_t)40 * area.w + 40];
	CHECK(peak.r == 255);
	CHECK(peak.g == 255);
	CHECK(peak.b == 255);
}

TEST_CASE("spots at the edge or nonsense are safe") {
	const LightArea area{0, 0, 64, 32};
	const size_t n = (size_t)area.w * area.h;
	const LightTexel SENTINEL{17, 34, 51, 68};
	std::vector<LightTexel> buf(n + 128, SENTINEL);
	LightTexel* const out = buf.data() + 64;
	for (size_t i = 0; i < n; ++i) out[i] = LightTexel{0, 0, 0, 0};

	CHECK(spot_source_radius(16.0f) == 3);
	CHECK(spot_source_radius(0.0f) == 1);
	CHECK(spot_source_radius(1e6f) == 8);
	CHECK(spot_source_radius(-4.0f) == 1);

	// Half outside: only the part inside the area is lit.
	add_light_spots(area, {LightSpot{-1.0f, 20.5f, 1, 1, 1, 2.0f, 16.0f}}, out);
	CHECK(out[(size_t)20 * area.w + 0].r > 0);

	std::vector<LightTexel> snapshot(out, out + n);
	// Wholly outside: nothing changes.
	add_light_spots(area, {LightSpot{(float)area.x0 + area.w + 100.0f, 20.5f, 1, 1, 1, 2.0f, 16.0f}},
	                out);
	add_light_spots(area, {LightSpot{20.5f, (float)area.y0 - 100.0f, 1, 1, 1, 2.0f, 16.0f}}, out);
	CHECK(std::memcmp(out, snapshot.data(), n * sizeof(LightTexel)) == 0);

	// A radius of 10^6 clamps to a source disc of 8 cells.
	add_light_spots(area, {LightSpot{32.5f, 16.5f, 1, 1, 1, 2.0f, 1e6f}}, out);
	CHECK(out[(size_t)16 * area.w + 32].r > 0);
	CHECK(out[(size_t)16 * area.w + 20].r == 0);  // 12 cells away, outside the 8 cell disc

	// Non-finite positions write nothing.
	snapshot.assign(out, out + n);
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float inf = std::numeric_limits<float>::infinity();
	add_light_spots(area, {LightSpot{nan, 16.5f, 1, 1, 1, 2.0f, 16.0f}}, out);
	add_light_spots(area, {LightSpot{32.5f, inf, 1, 1, 1, 2.0f, 16.0f}}, out);
	add_light_spots(area, {LightSpot{-inf, 16.5f, 1, 1, 1, 2.0f, 16.0f}}, out);
	CHECK(std::memcmp(out, snapshot.data(), n * sizeof(LightTexel)) == 0);

	for (size_t i = 0; i < 64; ++i) {
		CHECK(std::memcmp(&buf[i], &SENTINEL, sizeof(LightTexel)) == 0);
		CHECK(std::memcmp(&buf[n + 64 + i], &SENTINEL, sizeof(LightTexel)) == 0);
	}
}

TEST_CASE("marker lights come from chunks in the area") {
	LightTable lt;
	WorldGrid grid(lt.table, 1);
	Chunk& chunk = *grid.insert(empty_chunk({2, -1}));
	chunk.light_markers = {5 * CHUNK + 3};

	const LightSpot defaults{0, 0, 1.0f, 0.75f, 0.4f, 1.5f, 48.0f};
	std::vector<LightSpot> out;
	collect_marker_lights(grid, {2 * CHUNK, -CHUNK, 64, 64}, defaults, out);
	REQUIRE(out.size() == 1);
	CHECK(out[0].x == doctest::Approx(2.0 * CHUNK + 3.5));
	CHECK(out[0].y == doctest::Approx(-1.0 * CHUNK + 5.5));
	CHECK(out[0].r == defaults.r);
	CHECK(out[0].g == defaults.g);
	CHECK(out[0].b == defaults.b);
	CHECK(out[0].energy == defaults.energy);
	CHECK(out[0].radius == defaults.radius);

	out.clear();
	collect_marker_lights(grid, {-512, -512, 64, 64}, defaults, out);
	CHECK(out.empty());
}
