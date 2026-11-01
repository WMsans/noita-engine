#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "back_wall.h"
#include "chunk.h"
#include "coords.h"
#include "doctest/doctest.h"
#include "generator.h"
#include "material.h"
#include "terrain.h"
#include "terrain_test_util.h"

namespace {

using terraintest::test_seed;

std::unique_ptr<TerrainGenerator> compile_or_fail(const TerrainConfig& cfg,
                                                  const MaterialTable& table, uint32_t seed) {
	std::string error;
	std::unique_ptr<TerrainGenerator> gen = TerrainGenerator::compile(cfg, table, seed, error);
	REQUIRE_MESSAGE(gen != nullptr, error);
	return gen;
}

uint8_t alpha_of(uint32_t rgba) { return (uint8_t)(rgba & 0xFFu); }

// A chunk-image word: memory bytes R, G, B, A, so alpha is the top byte (what write_chunk_image
// reads). Spelled with shifts so the test says the same thing on any byte order.
uint32_t rgba_word(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

int max_channel(uint32_t rgba) {
	return std::max({(int)((rgba >> 24) & 0xFFu), (int)((rgba >> 16) & 0xFFu),
	                 (int)((rgba >> 8) & 0xFFu)});
}

// generate() hands the wall its per-cell biome pick; a direct fill() call builds the same row.
std::vector<int> biome_row(const TerrainGenerator& gen, ChunkCoord coord) {
	std::vector<int> biomes((size_t)CHUNK * CHUNK, -1);
	for (int ly = 0; ly < CHUNK; ++ly)
		for (int lx = 0; lx < CHUNK; ++lx)
			biomes[(size_t)ly * CHUNK + lx] =
			    gen.biome_at(coord.x * CHUNK + lx, coord.y * CHUNK + ly);
	return biomes;
}

}  // namespace

TEST_CASE("back wall rgba8 bytes follow the packed order") {
	// A texture upload reads bytes in R, G, B, A order, while the wall packs 0xRRGGBBAA.
	uint8_t px[4] = {0, 0, 0, 0};
	back_wall_rgba8_bytes(0x11223344u, px);
	CHECK(px[0] == 0x11);
	CHECK(px[1] == 0x22);
	CHECK(px[2] == 0x33);
	CHECK(px[3] == 0x44);

	// Alpha is the low byte, so a wall-less cell (alpha 0) stays transparent.
	back_wall_rgba8_bytes(0xAABBCC00u, px);
	CHECK(px[0] == 0xAA);
	CHECK(px[3] == 0);

	// And a filled wall cell is opaque.
	back_wall_rgba8_bytes(0x102030FFu, px);
	CHECK(px[3] == 255);
}

TEST_CASE("back wall composite keeps the wall under a see-through cell") {
	// A wall cell in fill()'s 0xRRGGBBAA packing, fully opaque: R 16, G 32, B 48.
	const uint32_t wall = 0x102030FFu;

	// A fully transparent cell shows the wall as it is.
	const uint32_t clear_cell = rgba_word(0, 0, 0, 0);
	CHECK(back_wall_composite(clear_cell, wall) == rgba_word(16, 32, 48, 255));
	// ... and above the surface there is no wall to show.
	CHECK(back_wall_composite(clear_cell, 0u) == rgba_word(0, 0, 0, 0));

	// A fully opaque cell hides the wall, byte for byte.
	const uint32_t solid_cell = rgba_word(0x44, 0x55, 0x66, 255);
	CHECK(back_wall_composite(solid_cell, wall) == solid_cell);

	// Alpha 128: (cell * 128 + wall * 127) / 255 per channel, rounded. Worked out by hand:
	// R (200*128 + 16*127)/255 = 108.36 -> 108, G (100*128 + 32*127)/255 = 66.13 -> 66,
	// B (50*128 + 48*127)/255 = 49.00 -> 49. The wall is opaque, so the blend is too.
	const uint32_t half_cell = rgba_word(200, 100, 50, 128);
	CHECK(back_wall_composite(half_cell, wall) == rgba_word(108, 66, 49, 255));

	// A see-through cell with no wall behind it is left exactly as it was.
	CHECK(back_wall_composite(half_cell, 0u) == half_cell);
}

TEST_CASE("back wall absent above the surface") {
	MaterialTable table = terraintest::terrain_table();
	const TerrainConfig cfg = terraintest::starter_config();
	std::unique_ptr<TerrainGenerator> gen = compile_or_fail(cfg, table, test_seed());
	const BackWall* wall = gen->back_wall();
	REQUIRE(wall != nullptr);
	const int rough = cfg.back_wall.edge_rough;

	int columns = 0;
	for (int x = -900; x <= 900; x += 17) {
		const int h = gen->surface_height(x);
		for (int wy = h - rough - 8; wy < h - rough; ++wy)
			REQUIRE_MESSAGE(alpha_of(wall->colour_at(*gen, table, x, wy)) == 0,
			                "no wall at x " << x << " y " << wy << ", surface " << h);
		for (int wy = h + rough; wy < h + rough + 40; ++wy)
			REQUIRE_MESSAGE(alpha_of(wall->colour_at(*gen, table, x, wy)) == 255,
			                "wall at x " << x << " y " << wy << ", surface " << h);
		++columns;
	}
	CHECK(columns > 100);
}

TEST_CASE("back wall chunk matches per-cell reference") {
	MaterialTable table = terraintest::terrain_table();
	const TerrainConfig cfg = terraintest::starter_config();
	std::unique_ptr<TerrainGenerator> gen = compile_or_fail(cfg, table, test_seed());
	const BackWall* wall = gen->back_wall();
	REQUIRE(wall != nullptr);

	const ChunkCoord coords[] = {{0, 2}, {-1, 3}, {-3, -1}, {5, 4}};
	for (ChunkCoord coord : coords) {
		std::vector<uint32_t> out;
		const std::vector<int> biomes = biome_row(*gen, coord);
		wall->fill(*gen, table, coord, biomes.data(), out);
		REQUIRE_MESSAGE(out.size() == (size_t)CHUNK * CHUNK,
		                "fill sizes a chunk at " << coord.x << "," << coord.y);

		int mismatches = 0;
		std::string first;
		for (int ly = 0; ly < CHUNK; ++ly) {
			for (int lx = 0; lx < CHUNK; ++lx) {
				const int wx = coord.x * CHUNK + lx, wy = coord.y * CHUNK + ly;
				const uint32_t expected = wall->colour_at(*gen, table, wx, wy);
				const uint32_t found = out[(size_t)ly * CHUNK + lx];
				if (found == expected) continue;
				if (mismatches == 0) {
					char buf[128];
					snprintf(buf, sizeof buf, "at (%d, %d): fill %08x, reference %08x", wx, wy,
					         found, expected);
					first = buf;
				}
				++mismatches;
			}
		}
		CHECK_MESSAGE(mismatches == 0, "chunk (" << coord.x << "," << coord.y << ") " << first);

		// generate() must wire the same image onto the chunk.
		std::unique_ptr<Chunk> chunk = make_chunk(coord, *gen, test_seed());
		REQUIRE(chunk->back_wall.size() == out.size());
		CHECK(std::memcmp(chunk->back_wall.data(), out.data(),
		                  out.size() * sizeof(uint32_t)) == 0);
	}
}

TEST_CASE("back wall deterministic") {
	MaterialTable table = terraintest::terrain_table();
	const TerrainConfig cfg = terraintest::starter_config();
	std::unique_ptr<TerrainGenerator> a = compile_or_fail(cfg, table, test_seed());
	std::unique_ptr<TerrainGenerator> b = compile_or_fail(cfg, table, test_seed());
	std::unique_ptr<TerrainGenerator> other = compile_or_fail(cfg, table, test_seed() + 1);

	const std::vector<int> biomes_a = biome_row(*a, {2, 3});
	const std::vector<int> biomes_other = biome_row(*other, {2, 3});
	std::vector<uint32_t> out_a, out_b, out_other;
	a->back_wall()->fill(*a, table, {2, 3}, biomes_a.data(), out_a);
	b->back_wall()->fill(*b, table, {2, 3}, biomes_a.data(), out_b);
	other->back_wall()->fill(*other, table, {2, 3}, biomes_other.data(), out_other);
	REQUIRE(out_a.size() == (size_t)CHUNK * CHUNK);
	CHECK(std::memcmp(out_a.data(), out_b.data(), out_a.size() * sizeof(uint32_t)) == 0);
	CHECK(std::memcmp(out_a.data(), out_other.data(), out_a.size() * sizeof(uint32_t)) != 0);
}

TEST_CASE("back wall hollow share") {
	MaterialTable table = terraintest::terrain_table();
	const TerrainConfig cfg = terraintest::starter_config();
	std::unique_ptr<TerrainGenerator> gen = compile_or_fail(cfg, table, test_seed());
	const BackWall* wall = gen->back_wall();
	REQUIRE(wall != nullptr);

	int hollow = 0, walls = 0;
	for (int cx = 0; cx < 4; ++cx) {
		for (int cy = 3; cy < 7; ++cy) {
			const ChunkCoord coord{cx, cy};
			std::vector<uint32_t> out;
			const std::vector<int> biomes = biome_row(*gen, coord);
			wall->fill(*gen, table, coord, biomes.data(), out);
			// Per biome in this chunk: the brightest hollow and the darkest wall cell.
			std::map<int, std::pair<int, int>> extremes;  // biome -> (hollow max, wall min)
			for (int ly = 0; ly < CHUNK; ++ly) {
				for (int lx = 0; lx < CHUNK; ++lx) {
					const int wx = cx * CHUNK + lx, wy = cy * CHUNK + ly;
					const uint32_t rgba = out[(size_t)ly * CHUNK + lx];
					if (alpha_of(rgba) == 0) continue;
					++walls;
					const bool is_hollow = wall->hollow_at(*gen, wx, wy);
					if (is_hollow) ++hollow;
					const int channel = max_channel(rgba);
					const int biome = gen->biome_at(wx, wy);
					auto it = extremes.find(biome);
					if (it == extremes.end())
						it = extremes.emplace(biome, std::make_pair(-1, 255)).first;
					int& hi = it->second.first;
					int& lo = it->second.second;
					if (is_hollow)
						hi = std::max(hi, channel);
					else
						lo = std::min(lo, channel);
				}
			}
			for (const auto& entry : extremes) {
				if (entry.second.first < 0 || entry.second.second > 255) continue;
				CHECK_MESSAGE(entry.second.first < entry.second.second,
				              "biome " << entry.first << " in chunk (" << cx << "," << cy
				                       << "): hollow at " << entry.second.first << ", wall at "
				                       << entry.second.second);
			}
		}
	}
	REQUIRE(walls == 16 * CHUNK * CHUNK);
	const double share = (double)hollow / (double)walls;
	CHECK_MESSAGE(share >= 0.15, "hollow share " << share << " is under a sixth");
	CHECK_MESSAGE(share <= 0.55, "hollow share " << share << " is over half");
}

TEST_CASE("back wall teeth reduce the hollow area") {
	MaterialTable table = terraintest::terrain_table();
	const TerrainConfig cfg = terraintest::starter_config();
	TerrainConfig bare = cfg;  // same hollow field, teeth turned off
	bare.back_wall.stalactite_length = 0;
	bare.back_wall.stalagmite_length = 0;
	std::unique_ptr<TerrainGenerator> toothed = compile_or_fail(cfg, table, test_seed());
	std::unique_ptr<TerrainGenerator> plain = compile_or_fail(bare, table, test_seed());
	REQUIRE(toothed->back_wall() != nullptr);
	REQUIRE(plain->back_wall() != nullptr);

	int toothed_hollow = 0, plain_hollow = 0, walls = 0;
	for (int cx = 0; cx < 4; ++cx) {
		for (int cy = 3; cy < 7; ++cy) {
			const ChunkCoord coord{cx, cy};
			std::vector<uint32_t> out;
			const std::vector<int> biomes = biome_row(*toothed, coord);
			toothed->back_wall()->fill(*toothed, table, coord, biomes.data(), out);
			for (int ly = 0; ly < CHUNK; ++ly) {
				for (int lx = 0; lx < CHUNK; ++lx) {
					if (alpha_of(out[(size_t)ly * CHUNK + lx]) == 0) continue;
					const int wx = cx * CHUNK + lx, wy = cy * CHUNK + ly;
					++walls;
					if (toothed->back_wall()->hollow_at(*toothed, wx, wy)) ++toothed_hollow;
					if (plain->back_wall()->hollow_at(*plain, wx, wy)) ++plain_hollow;
				}
			}
		}
	}
	REQUIRE(walls == 16 * CHUNK * CHUNK);
	const double toothed_share = (double)toothed_hollow / (double)walls;
	const double plain_share = (double)plain_hollow / (double)walls;
	CHECK_MESSAGE(toothed_share < plain_share,
	              "teeth left " << toothed_share << " of the wall hollow, bare field "
	                             << plain_share);
	CHECK_MESSAGE(plain_share - toothed_share >= 0.02,
	              "teeth removed only " << (plain_share - toothed_share)
	                                     << " of the hollow area");
}

TEST_CASE("back wall darker than terrain") {
	MaterialTable table = terraintest::terrain_table();
	const TerrainConfig cfg = terraintest::starter_config();
	std::unique_ptr<TerrainGenerator> gen = compile_or_fail(cfg, table, test_seed());
	const BackWall* wall = gen->back_wall();
	REQUIRE(wall != nullptr);
	// What the look requires, independent of the config: every wall cell is at least a quarter
	// darker than the terrain material of its own biome. A bound in terms of `darken` cannot
	// work: shade and pebbles multiply a wall cell by up to 1.1845, so `darken + 0.1` would cap
	// the tunable `darken` itself at about 0.54 instead of judging the picture.
	constexpr double WALL_MAX_RATIO = 0.75;

	const ChunkCoord coords[] = {{0, 3}, {1, 3}, {-1, 3}, {0, 4}};
	for (ChunkCoord coord : coords) {
		std::vector<uint32_t> out;
		const std::vector<int> biomes = biome_row(*gen, coord);
		wall->fill(*gen, table, coord, biomes.data(), out);
		for (int ly = 0; ly < CHUNK; ++ly) {
			for (int lx = 0; lx < CHUNK; ++lx) {
				const int wx = coord.x * CHUNK + lx, wy = coord.y * CHUNK + ly;
				const uint32_t rgba = out[(size_t)ly * CHUNK + lx];
				if (alpha_of(rgba) == 0) continue;
				const uint32_t base =
				    table.get(gen->biome_base(gen->biome_at(wx, wy))).color_rgba;
				const int terrain = max_channel(base), cell = max_channel(rgba);
				if (terrain <= 0) {  // a black base cannot be darkened further
					CHECK_MESSAGE(cell == 0, "wall " << cell << " at (" << wx << ", " << wy
					                               << ") over a black terrain base");
					continue;
				}
				const double ratio = (double)cell / (double)terrain;
				REQUIRE_MESSAGE(ratio <= WALL_MAX_RATIO,
				                "wall " << cell << " at (" << wx << ", " << wy << ") is " << ratio
				                        << " of its terrain's " << terrain << ", which is over "
				                        << WALL_MAX_RATIO);
			}
		}
	}
}

TEST_CASE("back wall disabled") {
	MaterialTable table = terraintest::terrain_table();
	TerrainConfig cfg = terraintest::starter_config();
	cfg.back_wall.enabled = false;
	std::unique_ptr<TerrainGenerator> gen = compile_or_fail(cfg, table, test_seed());
	CHECK(gen->back_wall() == nullptr);

	std::unique_ptr<Chunk> chunk = make_chunk({0, 3}, *gen, test_seed());
	CHECK(chunk->back_wall.empty());
}
