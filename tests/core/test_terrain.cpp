#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "chunk.h"
#include "chunk_streamer.h"
#include "coords.h"
#include "doctest/doctest.h"
#include "generator.h"
#include "material.h"
#include "rng.h"
#include "terrain.h"
#include "terrain_test_util.h"

namespace {

using terraintest::biome;
using terraintest::config;
using terraintest::test_seed;

std::unique_ptr<TerrainGenerator> compile_or_fail(const TerrainConfig& cfg,
                                                  const MaterialTable& table) {
	std::string error;
	std::unique_ptr<TerrainGenerator> gen = TerrainGenerator::compile(cfg, table, test_seed(), error);
	REQUIRE_MESSAGE(gen != nullptr, error);
	return gen;
}

}  // namespace

TEST_CASE("surface splits air from ground") {
	MaterialTable table = terraintest::terrain_table();
	BiomeDef ground = biome("ground", 0, -1);
	ground.surface_material = "grass";
	ground.base_material = "dirt";
	ground.surface_depth = 3;
	std::unique_ptr<TerrainGenerator> gen = compile_or_fail(config({ground}), table);

	const MatId grass = table.id_of("grass");
	const MatId dirt = table.id_of("dirt");
	terraintest::ChunkCache cache(*gen, test_seed());
	int columns = 0;
	for (int x = -2000; x < 2000; x += 7) {
		const int h = gen->surface_height(x);
		REQUIRE_MESSAGE(cache.at(x, h - 1) == MAT_AIR, "air above the surface at x " << x);
		REQUIRE_MESSAGE(cache.at(x, h) == grass, "grass at h, x " << x);
		REQUIRE_MESSAGE(cache.at(x, h + 1) == grass, "grass at h+1, x " << x);
		REQUIRE_MESSAGE(cache.at(x, h + 2) == grass, "grass at h+2, x " << x);
		REQUIRE_MESSAGE(cache.at(x, h + 3) == dirt, "dirt at h+3, x " << x);
		++columns;
	}
	CHECK(columns > 500);
}

TEST_CASE("bands and regions pick the expected biome") {
	MaterialTable table = terraintest::terrain_table();
	BiomeDef a = biome("a", 0, 50);
	BiomeDef b1 = biome("b", 50, 200);
	BiomeDef b2 = biome("c", 50, 200);
	BiomeDef d = biome("d", 200, -1);
	for (BiomeDef* def : {&a, &b1, &b2, &d}) def->base_material = "rock";
	TerrainConfig cfg = config({a, b1, b2, d});
	cfg.band_warp = 0;
	cfg.region_warp = 0;
	cfg.region_transition = 0;
	cfg.band_transition = 0;
	std::unique_ptr<TerrainGenerator> gen = compile_or_fail(cfg, table);

	for (int x : {-5000, 0, 1234}) {
		const int h = gen->surface_height(x);
		CHECK_MESSAGE(gen->biome_at(x, h + 10) == 0, "depth 10 at x " << x);
		CHECK_MESSAGE(gen->biome_at(x, h + 1000) == 3, "depth 1000 at x " << x);
	}

	std::set<int> seen;
	for (int region = 0; region < 40; ++region) {
		const int x = region * cfg.region_width + cfg.region_width / 2;
		const int h = gen->surface_height(x);
		const int picked = gen->biome_at(x, h + 100);
		const bool in_band = picked == 1 || picked == 2;
		CHECK_MESSAGE(in_band, "depth 100 in region " << region);
		seen.insert(picked);
	}
	CHECK(seen == std::set<int>{1, 2});
}

TEST_CASE("weights sum to 1, at most 4 biomes") {
	MaterialTable table = terraintest::terrain_table();
	std::unique_ptr<TerrainGenerator> gen = compile_or_fail(terraintest::starter_config(), table);

	for (int i = 0; i < 20000; ++i) {
		const int x = (int)(hash5(test_seed(), 0, i, 0, 0xabcdu) % 1000001u) - 500000;
		const int depth = (int)(hash5(test_seed(), 0, i, 1, 0xabcdu) % 4096u);
		const int y = gen->surface_height(x) + depth;
		const BiomeWeights w = gen->weights_at(x, y);
		REQUIRE_MESSAGE(w.count >= 1, "no biome at x " << x << " depth " << depth);
		REQUIRE_MESSAGE(w.count <= 4, "too many biomes at x " << x << " depth " << depth);
		double sum = 0;
		for (int k = 0; k < w.count; ++k) sum += w.weight[k];
		REQUIRE_MESSAGE(std::fabs(sum - 1.0) < 1e-5,
		                "weights sum to " << sum << " at x " << x << " depth " << depth);
	}
}

TEST_CASE("transitions are gradual") {
	MaterialTable table = terraintest::terrain_table();
	BiomeDef sand = biome("sandy", 0, -1);
	sand.base_material = "sand";
	BiomeDef dirt = biome("dirty", 0, -1);
	dirt.base_material = "dirt";
	TerrainConfig cfg = config({sand, dirt});
	cfg.band_warp = 0;
	cfg.band_transition = 0;
	cfg.region_warp = 0;
	cfg.region_transition = 128;
	std::unique_ptr<TerrainGenerator> gen = compile_or_fail(cfg, table);

	const int sand_biome = 0;
	int first_blend = -1, last_blend = -1;
	for (int x = -4096; x < 8192; ++x) {
		const int h = gen->surface_height(x);
		const bool blending = gen->weights_at(x, h + 100).count == 2;
		if (blending && first_blend < 0) first_blend = x;
		if (first_blend >= 0 && !blending) break;  // the blend at this border ends here
		if (blending) last_blend = x;
	}
	REQUIRE(first_blend >= 0);
	CHECK(last_blend - first_blend >= cfg.region_transition - 16);
	CHECK(last_blend - first_blend <= cfg.region_transition);

	// Eight-wide slices across the blend, each over depths 10..200. The slices just outside
	// the blend are a single biome, so their sand fraction is exactly 1 or 0.
	auto measure = [&](int x0) {
		int sands = 0, cells = 0;
		for (int x = x0; x < x0 + 8; ++x) {
			const int h = gen->surface_height(x);
			for (int depth = 10; depth < 200; ++depth) {
				++cells;
				if (gen->biome_at(x, h + depth) == sand_biome) ++sands;
			}
		}
		return (double)sands / (double)cells;
	};
	std::vector<double> fraction;
	fraction.push_back(measure(first_blend - 8));
	for (int x0 = first_blend; x0 + 8 <= last_blend + 1; x0 += 8) fraction.push_back(measure(x0));
	fraction.push_back(measure(last_blend + 1));

	REQUIRE(fraction.size() >= 8);
	REQUIRE((fraction.front() == 0.0 || fraction.front() == 1.0));
	CHECK(fraction.back() == 1.0 - fraction.front());
	for (size_t i = 1; i < fraction.size(); ++i) {
		CHECK_MESSAGE(std::fabs(fraction[i] - fraction[i - 1]) <= 0.25,
		              "slice " << i << " jumps from " << fraction[i - 1] << " to " << fraction[i]);
	}
}

TEST_CASE("biomes do not own the shape") {
	MaterialTable table = terraintest::terrain_table();
	BiomeDef solo = biome("solo", 0, -1);
	solo.base_material = "rock";
	solo.tunnel_threshold = 0.12f;
	solo.cavern_threshold = 0.3f;
	BiomeDef sandy = biome("sandy", 0, -1);
	sandy.base_material = "sand";
	sandy.tunnel_threshold = 0.12f;
	sandy.cavern_threshold = 0.3f;
	BiomeDef dirty = biome("dirty", 0, -1);
	dirty.base_material = "dirt";
	dirty.tunnel_threshold = 0.12f;
	dirty.cavern_threshold = 0.3f;
	std::unique_ptr<TerrainGenerator> one = compile_or_fail(config({solo}), table);
	std::unique_ptr<TerrainGenerator> two = compile_or_fail(config({sandy, dirty}), table);

	int mismatches = 0, first_x = 0, first_depth = 0;
	for (int x = -1024; x < 1024; ++x) {
		const int h = one->surface_height(x);
		REQUIRE(h == two->surface_height(x));
		for (int depth = 0; depth < 512; ++depth) {
			if (one->cave_at(x, h + depth) != two->cave_at(x, h + depth)) {
				if (mismatches == 0) {
					first_x = x;
					first_depth = depth;
				}
				++mismatches;
			}
		}
	}
	CHECK_MESSAGE(mismatches == 0, "first difference at x " << first_x << " depth " << first_depth);
}

TEST_CASE("no caves at the surface row") {
	MaterialTable table = terraintest::terrain_table();
	TerrainConfig cfg = terraintest::starter_config();
	cfg.entrance_threshold = 2.0f;  // above every noise value: entrances off
	std::unique_ptr<TerrainGenerator> gen = compile_or_fail(cfg, table);

	for (int x = -4000; x < 4000; ++x)
		REQUIRE_MESSAGE(!gen->cave_at(x, gen->surface_height(x)), "cave at the surface at x " << x);
}

TEST_CASE("chunk is independent of load order and threads") {
	MaterialTable table = terraintest::terrain_table();
	std::unique_ptr<TerrainGenerator> gen =
	    compile_or_fail(terraintest::starter_config(), table);

	std::vector<ChunkCoord> coords;
	for (int cy = 0; cy < 6; ++cy)
		for (int cx = 0; cx < 6; ++cx) coords.push_back({cx, cy});  // includes (3, 2)
	REQUIRE(coords.size() == 36);

	using Cells = std::array<Cell, CHUNK * CHUNK>;
	auto grab = [&](const std::vector<ChunkCoord>& order) {
		std::vector<Cells> out;
		out.reserve(order.size());
		for (ChunkCoord c : order) out.push_back(make_chunk(c, *gen, test_seed())->cells);
		return out;
	};

	const std::vector<Cells> solo = grab(coords);

	// Thirty unrelated chunks first, then the same six by six in reverse order.
	for (int i = 0; i < 30; ++i)
		make_chunk({(i * 37) % 200 - 100, (i * 53) % 200 - 100}, *gen, test_seed());
	const std::vector<ChunkCoord> reversed(coords.rbegin(), coords.rend());
	const std::vector<Cells> after = grab(reversed);
	for (size_t i = 0; i < coords.size(); ++i) {
		REQUIRE_MESSAGE(std::memcmp(solo[i].data(), after[coords.size() - 1 - i].data(),
		                            sizeof(Cells)) == 0,
		                "chunk " << coords[i].x << "," << coords[i].y << " differs after scatter");
	}

	for (int threads : {1, 4}) {
		ChunkStreamer streamer(*gen, test_seed(), threads);
		streamer.request(coords, {3, 2});
		streamer.wait_idle();
		const std::vector<std::unique_ptr<Chunk>> ready = streamer.take_ready((int)coords.size());
		REQUIRE_MESSAGE(ready.size() == coords.size(), threads << " threads");
		for (const std::unique_ptr<Chunk>& chunk : ready) {
			size_t idx = 0;
			while (idx < coords.size() && !(coords[idx] == chunk->coord)) ++idx;
			REQUIRE(idx < coords.size());
			REQUIRE_MESSAGE(std::memcmp(solo[idx].data(), chunk->cells.data(), sizeof(Cells)) == 0,
			                threads << " threads changed chunk " << chunk->coord.x << ","
			                        << chunk->coord.y);
		}
	}
}

TEST_CASE("far from the origin terrain still varies") {
	MaterialTable table = terraintest::terrain_table();
	std::unique_ptr<TerrainGenerator> gen =
	    compile_or_fail(terraintest::starter_config(), table);

	const ChunkCoord coord{156250, 3};  // x = 10 000 000
	const std::unique_ptr<Chunk> chunk = make_chunk(coord, *gen, test_seed());
	std::set<MatId> materials;
	for (const Cell& cell : chunk->cells) materials.insert(cell.mat);
	CHECK(materials.size() > 1);

	bool cave = false, solid = false;
	for (int wx = coord.x * CHUNK; wx < coord.x * CHUNK + CHUNK; ++wx)
		for (int wy = coord.y * CHUNK; wy < coord.y * CHUNK + CHUNK; ++wy)
			(gen->cave_at(wx, wy) ? cave : solid) = true;
	CHECK(cave);
	CHECK(solid);
}

TEST_CASE("config errors name the culprit") {
	MaterialTable table = terraintest::terrain_table();
	std::string error;
	auto fails = [&](const TerrainConfig& cfg) {
		error.clear();
		return TerrainGenerator::compile(cfg, table, test_seed(), error) == nullptr;
	};

	{
		BiomeDef def = biome("mystery", 0, -1);
		def.base_material = "unobtanium";
		CHECK(fails(config({def})));
		CHECK(error.find("mystery") != std::string::npos);
		CHECK(error.find("unobtanium") != std::string::npos);
	}
	{
		BiomeDef def = biome("mystery", 0, -1);
		CHECK(fails(config({def})));
		CHECK(error.find("mystery") != std::string::npos);
	}
	{
		BiomeDef a = biome("a", 0, 100);
		a.base_material = "rock";
		BiomeDef b = biome("b", 150, -1);
		b.base_material = "rock";
		CHECK(fails(config({a, b})));
		CHECK(error.find("100") != std::string::npos);
		CHECK(error.find("150") != std::string::npos);
	}
	{
		BiomeDef a = biome("a", 10, 100);
		a.base_material = "rock";
		BiomeDef b = biome("b", 100, -1);
		b.base_material = "rock";
		CHECK(fails(config({a, b})));
		CHECK(error.find("10") != std::string::npos);
	}
	{
		BiomeDef a = biome("a", 0, 100);
		a.base_material = "rock";
		BiomeDef b = biome("b", 100, 200);
		b.base_material = "rock";
		CHECK(fails(config({a, b})));
		CHECK(error.find("open") != std::string::npos);
	}
	{
		BiomeDef a = biome("a", 0, -1);
		a.base_material = "rock";
		TerrainConfig cfg = config({a});
		cfg.region_width = 128;
		cfg.region_transition = 96;
		CHECK(fails(cfg));
		CHECK(error.find("region_transition") != std::string::npos);
	}
	{
		BiomeDef a = biome("a", 0, 40);
		a.base_material = "rock";
		BiomeDef b = biome("b", 40, -1);
		b.base_material = "rock";
		TerrainConfig cfg = config({a, b});
		cfg.band_transition = 32;
		CHECK(fails(cfg));
		CHECK(error.find("band_transition") != std::string::npos);
	}
	{
		TerrainConfig cfg;
		CHECK(fails(cfg));
		CHECK(error.find("biome") != std::string::npos);
	}
}
