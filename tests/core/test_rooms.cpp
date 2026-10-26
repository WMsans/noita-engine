#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "coords.h"
#include "doctest/doctest.h"
#include "generator.h"
#include "material.h"
#include "terrain.h"
#include "terrain_test_util.h"

namespace {

using terraintest::biome;
using terraintest::config;
using terraintest::test_seed;

constexpr uint32_t C_ROCK = 0x776655FF;
constexpr uint32_t C_AIR = 0x102030FF;
constexpr uint32_t C_LIGHT = 0xFFFF00FF;
constexpr uint32_t C_KEEP = 0x00000000;  // alpha 0: keep the generated cell

std::unique_ptr<TerrainGenerator> compile_or_fail(const TerrainConfig& cfg,
                                                  const MaterialTable& table) {
	std::string error;
	std::unique_ptr<TerrainGenerator> gen = TerrainGenerator::compile(cfg, table, test_seed(), error);
	REQUIRE_MESSAGE(gen != nullptr, error);
	return gen;
}

std::vector<PaletteEntry> den_palette() {
	return {{C_ROCK, PaletteKind::Material, "rock"},
	        {C_AIR, PaletteKind::Material, "air"},
	        {C_LIGHT, PaletteKind::MarkerLight, ""}};
}

// One solid biome of dirt with no caves, so a room's pixels are the only thing to check.
TerrainConfig ground_config() {
	BiomeDef ground = biome("ground", 0, -1);
	ground.base_material = "dirt";
	return config({ground});
}

RoomDef solid_room(const std::string& name, int w, int h, RoomAnchor anchor) {
	RoomDef room;
	room.name = name;
	room.width = w;
	room.height = h;
	room.rgba.assign((size_t)w * h, C_ROCK);
	room.anchor = anchor;
	return room;
}

// 100x80: a 3-thick rock border, an air interior, a keep patch straddling the border and
// one marker-light pixel in the border, where it must read as air.
RoomDef den_room() {
	RoomDef room = solid_room("den", 100, 80, RoomAnchor::Embedded);
	room.rgba.assign((size_t)100 * 80, C_AIR);
	for (int y = 0; y < 80; ++y)
		for (int x = 0; x < 100; ++x)
			if (x < 3 || y < 3 || x >= 97 || y >= 77)
				room.rgba[(size_t)y * 100 + x] = C_ROCK;
	for (int y = 30; y < 40; ++y)
		for (int x = 0; x < 10; ++x) room.rgba[(size_t)y * 100 + x] = C_KEEP;
	room.rgba[(size_t)40 * 100 + 1] = C_LIGHT;
	return room;
}

// Generates the chunks the room in cell (px, py) covers, in raster and in reverse order, and
// checks the stamped cells agree and carry the palette's materials.
void check_cell_stamp(int px, int py) {
	MaterialTable table = terraintest::terrain_table();
	TerrainConfig cfg = ground_config();
	cfg.room_chance = 1.0f;
	cfg.rooms = {den_room()};
	cfg.palette = den_palette();
	std::unique_ptr<TerrainGenerator> gen = compile_or_fail(cfg, table);

	std::optional<RoomPlacement> placed = gen->room_in_cell(px, py);
	REQUIRE_MESSAGE(placed.has_value(), "no room in cell (" << px << ", " << py << ")");
	const RoomDef& room = cfg.rooms[placed->room];
	REQUIRE(room.name == "den");

	const ChunkCoord c0 = chunk_of(placed->x, placed->y);
	const ChunkCoord c1 = chunk_of(placed->x + room.width - 1, placed->y + room.height - 1);
	std::vector<ChunkCoord> raster;
	for (int cy = c0.y; cy <= c1.y; ++cy)
		for (int cx = c0.x; cx <= c1.x; ++cx) raster.push_back({cx, cy});
	std::vector<ChunkCoord> reverse(raster.rbegin(), raster.rend());

	terraintest::ChunkCache a(*gen, test_seed()), b(*gen, test_seed());
	for (const ChunkCoord& c : raster) (void)a.chunk(c);
	for (const ChunkCoord& c : reverse) (void)b.chunk(c);

	TerrainConfig bare = cfg;
	bare.room_chance = 0;
	std::unique_ptr<TerrainGenerator> plain = compile_or_fail(bare, table);
	terraintest::ChunkCache clean(*plain, test_seed());

	const MatId rock = table.id_of("rock");
	int material = 0, kept = 0, kept_on_ground = 0, markers = 0;
	for (int y = 0; y < room.height; ++y)
		for (int x = 0; x < room.width; ++x) {
			const int wx = placed->x + x, wy = placed->y + y;
			const uint32_t c = room.rgba[(size_t)y * room.width + x];
			REQUIRE_MESSAGE(a.at(wx, wy) == b.at(wx, wy), "load order changed " << wx << "," << wy);
			if (c == C_ROCK) {
				CHECK_MESSAGE(a.at(wx, wy) == rock, "rock at " << wx << "," << wy);
				++material;
			} else if (c == C_LIGHT) {
				CHECK_MESSAGE(a.at(wx, wy) == MAT_AIR, "marker air at " << wx << "," << wy);
				const ChunkCoord cc = chunk_of(wx, wy);
				const uint16_t local =
				    (uint16_t)((wy - cc.y * CHUNK) * CHUNK + (wx - cc.x * CHUNK));
				const std::vector<uint16_t>& ma = a.chunk(cc).light_markers;
				const std::vector<uint16_t>& mb = b.chunk(cc).light_markers;
				CHECK_MESSAGE(std::find(ma.begin(), ma.end(), local) != ma.end(),
				              "marker light in raster at " << wx << "," << wy);
				CHECK_MESSAGE(std::find(mb.begin(), mb.end(), local) != mb.end(),
				              "marker light in reverse at " << wx << "," << wy);
				++markers;
			} else if (c == C_AIR) {
				CHECK_MESSAGE(a.at(wx, wy) == MAT_AIR, "room air at " << wx << "," << wy);
			} else {
				REQUIRE(c == C_KEEP);
				CHECK_MESSAGE(a.at(wx, wy) == clean.at(wx, wy), "keep at " << wx << "," << wy);
				++kept;
				if (clean.at(wx, wy) != MAT_AIR) ++kept_on_ground;
			}
		}
	int total_markers = 0;
	for (const ChunkCoord& c : raster) total_markers += (int)a.chunk(c).light_markers.size();
	CHECK(total_markers == 1);
	CHECK(material > 0);
	CHECK(markers == 1);
	CHECK(kept == 100);
	CHECK(kept_on_ground > 0);  // the keep patch really covers generated ground
}

}  // namespace

TEST_CASE("embedded room across chunk borders is order-independent") {
	check_cell_stamp(0, 1);
}

TEST_CASE("negative placement cell works the same") {
	check_cell_stamp(-1, 2);
}

TEST_CASE("surface room stands on the ground") {
	MaterialTable table = terraintest::terrain_table();
	TerrainConfig cfg = ground_config();
	cfg.room_chance = 1.0f;
	cfg.rooms = {solid_room("hut", 40, 24, RoomAnchor::Surface)};
	cfg.palette = den_palette();
	std::unique_ptr<TerrainGenerator> gen = compile_or_fail(cfg, table);

	int found = 0;
	for (int py = -1; py <= 0; ++py)
		for (int px = -20; px <= 20; ++px) {
			std::optional<RoomPlacement> p = gen->room_in_cell(px, py);
			if (!p) continue;
			++found;
			CHECK_MESSAGE(p->y + 23 == gen->surface_height(p->x + 20),
			              "cell (" << px << ", " << py << ")");
			CHECK(p->y >= py * 256);
			CHECK(p->y + 24 <= (py + 1) * 256);
			CHECK(p->x >= px * 256);
			CHECK(p->x + 40 <= (px + 1) * 256);
		}
	CHECK(found >= 5);
}

TEST_CASE("cave room only where the cave field is open") {
	MaterialTable table = terraintest::terrain_table();
	BiomeDef ground = biome("ground", 0, -1);
	ground.base_material = "dirt";
	ground.cavern_threshold = 0.45f;
	TerrainConfig cfg = config({ground});
	cfg.room_chance = 1.0f;
	cfg.rooms = {solid_room("grotto", 48, 32, RoomAnchor::Cave)};
	cfg.palette = den_palette();
	std::unique_ptr<TerrainGenerator> gen = compile_or_fail(cfg, table);

	int checked = 0;
	for (int py = 1; py <= 20 && checked < 200; ++py)
		for (int px = 0; px < 30 && checked < 200; ++px) {
			std::optional<RoomPlacement> p = gen->room_in_cell(px, py);
			if (!p) continue;
			CHECK_MESSAGE(gen->cave_at(p->x + 24, p->y + 16),
			              "cell (" << px << ", " << py << ")");
			++checked;
		}
	CHECK(checked == 200);
}

TEST_CASE("room biome eligibility") {
	MaterialTable table = terraintest::terrain_table();
	BiomeDef forest = biome("forest", 0, -1);
	forest.base_material = "dirt";
	BiomeDef desert = biome("desert", 0, -1);
	desert.base_material = "sand";
	TerrainConfig cfg = config({forest, desert});
	cfg.room_chance = 1.0f;
	RoomDef tomb = solid_room("tomb", 40, 24, RoomAnchor::Embedded);
	tomb.biomes = {"desert"};
	cfg.rooms = {tomb};
	cfg.palette = den_palette();
	std::unique_ptr<TerrainGenerator> gen = compile_or_fail(cfg, table);

	int found = 0;
	for (int py = 1; py <= 30; ++py)
		for (int px = 0; px < 10; ++px) {
			std::optional<RoomPlacement> p = gen->room_in_cell(px, py);
			if (!p) continue;
			++found;
			const BiomeWeights w = gen->weights_at(px * 256 + 128, py * 256 + 128);
			REQUIRE(w.count > 0);
			int top = 0;
			for (int i = 1; i < w.count; ++i)
				if (w.weight[i] > w.weight[top]) top = i;
			CHECK_MESSAGE(cfg.biomes[w.biome[top]].name == "desert",
			              "cell (" << px << ", " << py << ")");
		}
	CHECK(found > 20);
}

TEST_CASE("placement is deterministic") {
	MaterialTable table = terraintest::terrain_table();
	TerrainConfig cfg = ground_config();
	cfg.room_chance = 0.5f;
	RoomDef hut = solid_room("hut", 32, 32, RoomAnchor::Surface);
	hut.weight = 2.0f;
	cfg.rooms = {solid_room("den", 40, 24, RoomAnchor::Embedded), hut};
	cfg.palette = den_palette();
	std::unique_ptr<TerrainGenerator> g1 = compile_or_fail(cfg, table);
	std::unique_ptr<TerrainGenerator> g2 = compile_or_fail(cfg, table);

	int found = 0;
	for (int i = 0; i < 500; ++i) {
		const int px = i % 50 - 25, py = i / 50 - 2;
		std::optional<RoomPlacement> p1 = g1->room_in_cell(px, py);
		std::optional<RoomPlacement> p2 = g2->room_in_cell(px, py);
		REQUIRE(p1.has_value() == p2.has_value());
		if (p1) {
			++found;
			CHECK(p1->room == p2->room);
			CHECK(p1->x == p2->x);
			CHECK(p1->y == p2->y);
		}
	}
	CHECK(found > 50);
}

TEST_CASE("room config errors") {
	MaterialTable table = terraintest::terrain_table();
	auto error_for = [&](const TerrainConfig& cfg) {
		std::string error;
		CHECK(TerrainGenerator::compile(cfg, table, test_seed(), error) == nullptr);
		return error;
	};
	TerrainConfig base = ground_config();
	base.palette = den_palette();

	SUBCASE("room colour missing from the palette") {
		TerrainConfig cfg = base;
		RoomDef room = solid_room("vault", 10, 10, RoomAnchor::Embedded);
		room.rgba[(size_t)4 * 10 + 3] = 0x12345678;
		cfg.rooms = {room};
		const std::string e = error_for(cfg);
		CHECK(e.find("vault") != std::string::npos);
		CHECK(e.find("(3, 4)") != std::string::npos);
		CHECK(e.find("#12345678") != std::string::npos);
	}

	SUBCASE("oversized room") {
		TerrainConfig cfg = base;
		cfg.rooms = {solid_room("hall", 257, 10, RoomAnchor::Embedded)};
		const std::string e = error_for(cfg);
		CHECK(e.find("hall") != std::string::npos);
	}

	SUBCASE("room_cell_size not a multiple of 64") {
		TerrainConfig cfg = base;
		cfg.room_cell_size = 100;
		const std::string e = error_for(cfg);
		CHECK(e.find("room_cell_size") != std::string::npos);
	}

	SUBCASE("unknown room biome") {
		TerrainConfig cfg = base;
		RoomDef room = solid_room("vault", 10, 10, RoomAnchor::Embedded);
		room.biomes = {"swamp"};
		cfg.rooms = {room};
		const std::string e = error_for(cfg);
		CHECK(e.find("vault") != std::string::npos);
		CHECK(e.find("swamp") != std::string::npos);
	}

	SUBCASE("unknown palette material") {
		TerrainConfig cfg = base;
		cfg.palette.push_back({0x00FF00FF, PaletteKind::Material, "unobtanium"});
		const std::string e = error_for(cfg);
		CHECK(e.find("#00ff00ff") != std::string::npos);
	}
}
