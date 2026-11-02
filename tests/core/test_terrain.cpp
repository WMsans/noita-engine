#include <algorithm>
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
#include "crust_pass.h"
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

// Pins the classic cave shape: these numbers were taken from the generator before cave styles
// existed, so any change to the classic path shows up here.
TEST_CASE("classic caves are unchanged") {
	MaterialTable table = terraintest::terrain_table();
	std::unique_ptr<TerrainGenerator> gen =
	    compile_or_fail(terraintest::starter_config(), table);

	uint64_t hash = 1469598103934665603ull;  // FNV-1a 64
	int caves = 0;
	for (int x = -512; x < 512; ++x) {
		const int s = gen->surface_height(x);
		for (int d = -8; d < 600; ++d) {
			const bool cave = gen->cave_at(x, s + d);
			caves += cave;
			hash = (hash ^ (uint64_t)cave) * 1099511628211ull;
		}
	}
	CHECK(hash == 0xc99e0ec9f0e4e29bull);
	CHECK(caves == 205590);
}

// FNV-1a 64 over mat, shade and flags of every cell of the chunks in [cx0, cx1) x [cy0, cy1).
uint64_t region_cell_hash(const TerrainGenerator& gen, int cx0, int cx1, int cy0, int cy1) {
	uint64_t hash = 1469598103934665603ull;
	auto mix = [&](uint64_t v) { hash = (hash ^ v) * 1099511628211ull; };
	for (int cy = cy0; cy < cy1; ++cy)
		for (int cx = cx0; cx < cx1; ++cx) {
			const std::unique_ptr<Chunk> chunk = make_chunk({cx, cy}, gen, test_seed());
			for (const Cell& c : chunk->cells) {
				mix(c.mat);
				mix(c.shade);
				mix(c.flags);
			}
		}
	return hash;
}

// The den from test_rooms.cpp (the helper is local there, not shared): a 100x80 room with a
// 3-thick rock border, an air interior, a keep patch and one marker-light pixel.
constexpr uint32_t PIN_C_ROCK = 0x776655FF;
constexpr uint32_t PIN_C_AIR = 0x102030FF;
constexpr uint32_t PIN_C_LIGHT = 0xFFFF00FF;
constexpr uint32_t PIN_C_KEEP = 0x00000000;

RoomDef pin_den_room() {
	RoomDef room;
	room.name = "den";
	room.width = 100;
	room.height = 80;
	room.rgba.assign((size_t)100 * 80, PIN_C_AIR);
	for (int y = 0; y < 80; ++y)
		for (int x = 0; x < 100; ++x)
			if (x < 3 || y < 3 || x >= 97 || y >= 77) room.rgba[(size_t)y * 100 + x] = PIN_C_ROCK;
	for (int y = 30; y < 40; ++y)
		for (int x = 0; x < 10; ++x) room.rgba[(size_t)y * 100 + x] = PIN_C_KEEP;
	room.rgba[(size_t)40 * 100 + 1] = PIN_C_LIGHT;
	return room;
}

TEST_CASE("generate output is unchanged without crust") {
	MaterialTable table = terraintest::terrain_table();
	std::unique_ptr<TerrainGenerator> gen =
	    compile_or_fail(terraintest::starter_config(), table);
	const uint64_t plain = region_cell_hash(*gen, -3, 3, -1, 5);

	TerrainConfig roomed = terraintest::starter_config();
	roomed.room_chance = 1.0f;
	roomed.rooms = {pin_den_room()};
	roomed.palette = {{PIN_C_ROCK, PaletteKind::Material, "rock"},
	                 {PIN_C_AIR, PaletteKind::Material, "air"},
	                 {PIN_C_LIGHT, PaletteKind::MarkerLight, ""}};
	std::unique_ptr<TerrainGenerator> room_gen = compile_or_fail(roomed, table);
	const uint64_t rooms = region_cell_hash(*room_gen, -3, 3, -1, 5);

	CHECK(plain != rooms);  // the room actually stamps something in the hashed range
	CHECK(plain == 0xb7929bdf53d3750aull);
	CHECK(rooms == 0x9038be10a6ae56d8ull);
}

TEST_CASE("crust is seamless across chunk borders") {
	MaterialTable table = terraintest::crust_table();
	std::unique_ptr<TerrainGenerator> gen =
	    compile_or_fail(terraintest::rocky_config(), table);
	const int margin = gen->region_margin();
	// One reference region covering the whole 4x4-chunk area plus the margin.
	const int x0 = -2 * CHUNK - margin, y0 = -margin;
	const int size = 4 * CHUNK + 2 * margin;
	GenRegion ref;
	gen->resolve_region(x0, y0, size, ref, nullptr);
	gen->apply_passes(ref);

	// Crust within 3 cells of each side of the centre borders (x = 0, y = 128).
	int vl = 0, vr = 0, ht = 0, hb = 0;
	for (int cy = 0; cy < 4; ++cy)
		for (int cx = -2; cx < 2; ++cx) {
			const std::unique_ptr<Chunk> chunk = make_chunk({cx, cy}, *gen, test_seed());
			for (int ly = 0; ly < CHUNK; ++ly)
				for (int lx = 0; lx < CHUNK; ++lx) {
					const int wx = cx * CHUNK + lx, wy = cy * CHUNK + ly;
					const bool chunk_crust = chunk->at(lx, ly).flags & FLAG_CRUST;
					const bool ref_crust = ref.flags_at(wx, wy) & FLAG_CRUST;
					CHECK(chunk_crust == ref_crust);
					if (!chunk_crust) continue;
					if (wx < 3) ++vl;
					if (wx > -3) ++vr;
					if (wy < 131) ++ht;
					if (wy > 125) ++hb;
				}
		}
	CHECK(vl > 0);
	CHECK(vr > 0);
	CHECK(ht > 0);
	CHECK(hb > 0);
}

TEST_CASE("crust is deterministic per seed") {
	MaterialTable table = terraintest::crust_table();
	TerrainConfig cfg = terraintest::starter_config();
	std::string error;
	std::unique_ptr<TerrainGenerator> a = TerrainGenerator::compile(cfg, table, test_seed(), error);
	REQUIRE_MESSAGE(a != nullptr, error);
	std::unique_ptr<TerrainGenerator> b = TerrainGenerator::compile(cfg, table, test_seed(), error);
	REQUIRE_MESSAGE(b != nullptr, error);
	std::unique_ptr<TerrainGenerator> c =
	    TerrainGenerator::compile(cfg, table, test_seed() + 1, error);
	REQUIRE_MESSAGE(c != nullptr, error);

	const auto ca = make_chunk({1, 2}, *a, test_seed());
	const auto cb = make_chunk({1, 2}, *b, test_seed());
	const auto cc = make_chunk({1, 2}, *c, test_seed());
	CHECK(std::memcmp(ca->cells.data(), cb->cells.data(), sizeof(ca->cells)) == 0);
	CHECK(std::memcmp(ca->cells.data(), cc->cells.data(), sizeof(ca->cells)) != 0);
}

TEST_CASE("crust only on crust materials next to open space") {
	MaterialTable table = terraintest::crust_table();
	std::unique_ptr<TerrainGenerator> gen =
	    compile_or_fail(terraintest::rocky_config(), table);
	const MatId rock = table.id_of("rock");
	const MatId water = table.id_of("water");
	const int reach = (int)std::ceil(6 * 1.3f + 2.0f);  // the crust radius bound

	// The 4x4-chunk area (world x in [-128, 128), y in [0, 256)) in one grid.
	std::vector<Cell> cells((size_t)4 * CHUNK * 4 * CHUNK);
	for (int cy = 0; cy < 4; ++cy)
		for (int cx = -2; cx < 2; ++cx) {
			const std::unique_ptr<Chunk> chunk = make_chunk({cx, cy}, *gen, test_seed());
			for (int ly = 0; ly < CHUNK; ++ly)
				for (int lx = 0; lx < CHUNK; ++lx)
					cells[(size_t)(cy * CHUNK + ly) * 4 * CHUNK + (cx + 2) * CHUNK + lx] =
					    chunk->at(lx, ly);
		}
	auto mat_at = [&](int wx, int wy) -> MatId {
		if (wx < -2 * CHUNK || wx >= 2 * CHUNK || wy < 0 || wy >= 4 * CHUNK) return MAT_AIR;
		return cells[(size_t)wy * 4 * CHUNK + (wx + 2 * CHUNK)].mat;
	};
	auto open_class = [&](MatId m) {
		const MatClass cls = table.get(m).cls;
		return cls == MatClass::Empty || cls == MatClass::Liquid || cls == MatClass::Gas;
	};
	auto is_edge_rock = [&](int wx, int wy) {
		if (mat_at(wx, wy) != rock) return false;
		return open_class(mat_at(wx + 1, wy)) || open_class(mat_at(wx - 1, wy)) ||
		       open_class(mat_at(wx, wy + 1)) || open_class(mat_at(wx, wy - 1));
	};

	int crust_cells = 0, water_facing = 0, sky_facing = 0;
	for (int wy = 0; wy < 4 * CHUNK; ++wy)
		for (int wx = -2 * CHUNK; wx < 2 * CHUNK; ++wx) {
			const Cell& c = cells[(size_t)wy * 4 * CHUNK + (wx + 2 * CHUNK)];
			if (!(c.flags & FLAG_CRUST)) continue;
			CHECK(c.mat == rock);  // rock is the only crust material in crust_table()
			++crust_cells;
			bool near_edge = false;
			for (int dy = -reach; dy <= reach && !near_edge; ++dy)
				for (int dx = -reach; dx <= reach && !near_edge; ++dx)
					if (std::hypot((double)dx, (double)dy) <= reach &&
					    is_edge_rock(wx + dx, wy + dy))
						near_edge = true;
			CHECK_MESSAGE(near_edge, "crust at (" << wx << ", " << wy
			                                      << ") is more than "
			                                      << reach << " from any edge rock cell");
			if (mat_at(wx + 1, wy) == water || mat_at(wx - 1, wy) == water ||
			    mat_at(wx, wy + 1) == water || mat_at(wx, wy - 1) == water)
				++water_facing;
			const int h = gen->surface_height(wx);
			if (mat_at(wx, wy - 1) == MAT_AIR && wy - 1 < h) ++sky_facing;
		}
	CHECK(crust_cells > 0);
	CHECK(water_facing > 0);
	CHECK(sky_facing > 0);

	// A rock cell whose only open neighbours are sand is not an edge.
	{
		const MatId sand = table.id_of("sand");
		GenRegion region;
		region.x0 = 0;
		region.y0 = 0;
		region.size = 6;
		region.mat.assign((size_t)6 * 6, sand);
		for (int y = 2; y < 4; ++y)
			for (int x = 2; x < 4; ++x) region.mat[(size_t)y * 6 + x] = rock;
		region.flags.assign((size_t)6 * 6, 0);
		CrustPass pass(table, test_seed(), 1.0f, 6);
		pass.apply(region);
		for (int y = 0; y < 6; ++y)
			for (int x = 0; x < 6; ++x)
				CHECK((region.flags[(size_t)y * 6 + x] & FLAG_CRUST) == 0);

		// The same patch next to air is an edge and gets crust.
		GenRegion open_region = region;
		open_region.flags.assign((size_t)6 * 6, 0);
		open_region.mat[(size_t)3 * 6 + 3] = MAT_AIR;
		pass.apply(open_region);
		CHECK((open_region.flags[(size_t)3 * 6 + 2] & FLAG_CRUST) != 0);
	}
}

// A blob stamped from an edge cell next to the region border must stop at the border: past
// it, the row-major index wraps onto the far side of the region (or off either end of the
// vectors, corrupting the heap).
TEST_CASE("crust blobs stay inside the region") {
	MaterialTable table = terraintest::crust_table();
	const MatId rock = table.id_of("rock");
	const int n = 32, src_x = 1, src_y = n / 2;
	const int reach = (int)std::ceil(6 * 1.3f + 2.0f);
	GenRegion region;
	region.size = n;
	region.mat.assign((size_t)n * n, rock);
	region.mat[(size_t)src_y * n + 0] = MAT_AIR;  // makes (1, 16) the only edge rock cell
	region.flags.assign((size_t)n * n, 0);
	CrustPass pass(table, test_seed(), 1.0f, 6);
	pass.apply(region);

	int crust_cells = 0;
	for (int y = 0; y < n; ++y)
		for (int x = 0; x < n; ++x) {
			if (!(region.flags[(size_t)y * n + x] & FLAG_CRUST)) continue;
			++crust_cells;
			CHECK_MESSAGE(std::hypot((double)(x - src_x), (double)(y - src_y)) <= reach,
			              "crust at (" << x << ", " << y << ") is out of the source's reach");
		}
	CHECK(crust_cells > 0);
}

TEST_CASE("no passes without a crust material") {
	MaterialTable table = terraintest::terrain_table();  // no textures at all
	std::unique_ptr<TerrainGenerator> gen =
	    compile_or_fail(terraintest::starter_config(), table);
	CHECK(gen->region_margin() == 0);

	MaterialTable crusty = terraintest::crust_table();
	TerrainConfig off = terraintest::starter_config();
	off.crust_chance = 0;
	std::unique_ptr<TerrainGenerator> gen_off = compile_or_fail(off, crusty);
	CHECK(gen_off->region_margin() == 0);

	std::unique_ptr<TerrainGenerator> gen_on =
	    compile_or_fail(terraintest::starter_config(), crusty);
	CHECK(gen_on->region_margin() == 11);  // reach 10 + 1 for radius 6
}

TEST_CASE("crust settings are validated") {
	MaterialTable table = terraintest::terrain_table();
	std::string error;
	auto fails = [&](const TerrainConfig& cfg) {
		error.clear();
		return TerrainGenerator::compile(cfg, table, test_seed(), error) == nullptr;
	};

	TerrainConfig low = terraintest::starter_config();
	low.crust_chance = -0.1f;
	CHECK(fails(low));
	CHECK(error.find("crust_chance") != std::string::npos);

	TerrainConfig high = terraintest::starter_config();
	high.crust_chance = 1.1f;
	CHECK(fails(high));
	CHECK(error.find("crust_chance") != std::string::npos);

	TerrainConfig small = terraintest::starter_config();
	small.crust_radius = 0;
	CHECK(fails(small));
	CHECK(error.find("crust_radius") != std::string::npos);

	TerrainConfig large = terraintest::starter_config();
	large.crust_radius = 33;
	CHECK(fails(large));
	CHECK(error.find("crust_radius") != std::string::npos);
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

	std::unique_ptr<TerrainGenerator> slabs = compile_or_fail(terraintest::slabs_config(), table);
	bool slab_cave = false, slab_solid = false;
	for (int wx = coord.x * CHUNK; wx < coord.x * CHUNK + CHUNK; ++wx)
		for (int wy = coord.y * CHUNK; wy < coord.y * CHUNK + CHUNK; ++wy)
			(slabs->cave_at(wx, wy) ? slab_cave : slab_solid) = true;
	CHECK(slab_cave);
	CHECK(slab_solid);
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
	{
		TerrainConfig cfg = terraintest::slabs_config();
		cfg.slab_stretch = 0.0f;
		CHECK(fails(cfg));
		CHECK(error.find("slab_stretch") != std::string::npos);
	}
	{
		TerrainConfig cfg = terraintest::slabs_config();
		cfg.slab_frequency = -1.0f;
		CHECK(fails(cfg));
		CHECK(error.find("slab_frequency") != std::string::npos);
	}
	{
		TerrainConfig cfg = terraintest::crust_config();
		cfg.biomes[0].crust_material = "granite";
		CHECK(fails(cfg));
		CHECK(error.find("crust_material") != std::string::npos);
	}
}

namespace {

// cave_at over a w x h window from (x0, y0), row-major. The slabs config's surface stays within
// 48 cells of y 0 and its taper ends 40 below, so windows from y 100 are below the taper.
std::vector<uint8_t> cave_grid(const TerrainGenerator& gen, int x0, int y0, int w, int h) {
	std::vector<uint8_t> out((size_t)w * h);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) out[(size_t)y * w + x] = gen.cave_at(x0 + x, y0 + y) ? 1 : 0;
	return out;
}

double air_share(const std::vector<uint8_t>& grid) {
	size_t air = 0;
	for (uint8_t c : grid) air += c;
	return (double)air / (double)grid.size();
}

}  // namespace

TEST_CASE("slabs air share follows slab_air") {
	MaterialTable table = terraintest::terrain_table();
	TerrainConfig cfg = terraintest::slabs_config();
	const double share = air_share(cave_grid(*compile_or_fail(cfg, table), 0, 100, 1024, 1024));
	CHECK(share >= 0.50);
	CHECK(share <= 0.62);

	cfg.biomes[0].slab_air = 0.3f;
	const double less = air_share(cave_grid(*compile_or_fail(cfg, table), 0, 100, 1024, 1024));
	CHECK(less >= 0.25);
	CHECK(less <= 0.40);
}

TEST_CASE("slabs are wide and flat") {
	MaterialTable table = terraintest::terrain_table();
	const int w = 1024, h = 1024;
	const std::vector<uint8_t> grid =
	    cave_grid(*compile_or_fail(terraintest::slabs_config(), table), 0, 100, w, h);
	// Rock/air boundaries crossed per step down a column against per step along a row: for
	// shapes W wide and H tall this is about W / H.
	long across = 0, down = 0;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			const uint8_t c = grid[(size_t)y * w + x];
			if (x + 1 < w) across += c != grid[(size_t)y * w + x + 1];
			if (y + 1 < h) down += c != grid[(size_t)(y + 1) * w + x];
		}
	REQUIRE(across > 0);
	const double ratio = (double)down / (double)across;
	CHECK_MESSAGE(ratio >= 2.2, "slabs are only " << ratio << "x wider than tall");
}

TEST_CASE("slabs open space is connected") {
	MaterialTable table = terraintest::terrain_table();
	const int w = 1024, h = 512;
	const std::vector<uint8_t> grid =
	    cave_grid(*compile_or_fail(terraintest::slabs_config(), table), 0, 100, w, h);
	std::vector<int> label(grid.size(), -1);
	size_t air = 0, largest = 0;
	std::vector<size_t> stack;
	for (size_t start = 0; start < grid.size(); ++start) {
		if (grid[start] == 0 || label[start] >= 0) continue;
		size_t size = 0;
		label[start] = 1;
		stack.push_back(start);
		while (!stack.empty()) {
			const size_t i = stack.back();
			stack.pop_back();
			++size;
			const int x = (int)(i % w), y = (int)(i / w);
			const int nx[4] = {x - 1, x + 1, x, x}, ny[4] = {y, y, y - 1, y + 1};
			for (int k = 0; k < 4; ++k) {
				if (nx[k] < 0 || ny[k] < 0 || nx[k] >= w || ny[k] >= h) continue;
				const size_t j = (size_t)ny[k] * w + nx[k];
				if (grid[j] == 0 || label[j] >= 0) continue;
				label[j] = 1;
				stack.push_back(j);
			}
		}
		air += size;
		largest = std::max(largest, size);
	}
	REQUIRE(air > 0);
	CHECK_MESSAGE((double)largest >= 0.9 * (double)air, largest << " of " << air << " air cells");
}

TEST_CASE("slabs cave_at agrees with generate") {
	MaterialTable table = terraintest::terrain_table();
	std::unique_ptr<TerrainGenerator> gen = compile_or_fail(terraintest::slabs_config(), table);
	int mismatches = 0;
	for (int cy = 2; cy <= 4; ++cy)
		for (int cx = 0; cx <= 2; ++cx) {
			const std::unique_ptr<Chunk> chunk = make_chunk({cx, cy}, *gen, test_seed());
			for (int ly = 0; ly < CHUNK; ++ly)
				for (int lx = 0; lx < CHUNK; ++lx) {
					const int wx = cx * CHUNK + lx, wy = cy * CHUNK + ly;
					if ((chunk->at(lx, ly).mat == MAT_AIR) != gen->cave_at(wx, wy)) ++mismatches;
				}
		}
	CHECK(mismatches == 0);
}

TEST_CASE("slabs are deterministic per seed") {
	MaterialTable table = terraintest::terrain_table();
	const TerrainConfig cfg = terraintest::slabs_config();
	std::string error;
	const std::vector<uint8_t> one = cave_grid(*compile_or_fail(cfg, table), 0, 100, 512, 512);
	const std::vector<uint8_t> two = cave_grid(*compile_or_fail(cfg, table), 0, 100, 512, 512);
	CHECK(one == two);
	std::unique_ptr<TerrainGenerator> other =
	    TerrainGenerator::compile(cfg, table, test_seed() + 1, error);
	REQUIRE_MESSAGE(other != nullptr, error);
	const std::vector<uint8_t> three = cave_grid(*other, 0, 100, 512, 512);
	size_t differ = 0;
	for (size_t i = 0; i < one.size(); ++i) differ += one[i] != three[i];
	CHECK((double)differ > 0.1 * (double)one.size());
}

TEST_CASE("slabs amounts are clamped") {
	MaterialTable table = terraintest::terrain_table();
	TerrainConfig full = terraintest::slabs_config();
	full.biomes[0].slab_air = 1.0f;
	TerrainConfig over = full;
	over.biomes[0].slab_air = 1.7f;
	CHECK(cave_grid(*compile_or_fail(full, table), 0, 100, 256, 256) ==
	      cave_grid(*compile_or_fail(over, table), 0, 100, 256, 256));

	TerrainConfig under = terraintest::slabs_config();
	under.biomes[0].slab_air = -0.5f;
	under.biomes[0].slab_holes = 0.0f;
	CHECK(air_share(cave_grid(*compile_or_fail(under, table), 0, 100, 256, 256)) == 0.0);
}

TEST_CASE("no seam where cave styles meet") {
	MaterialTable table = terraintest::terrain_table();
	BiomeDef old_style = biome("old", 0, -1);
	old_style.base_material = "rock";
	old_style.tunnel_threshold = 0.12f;
	old_style.cavern_threshold = 0.3f;
	BiomeDef new_style = biome("new", 0, -1);
	new_style.base_material = "rock";
	new_style.cave_style = CaveStyle::Slabs;
	new_style.slab_air = 0.55f;
	TerrainConfig cfg = config({old_style, new_style});
	cfg.band_warp = 0;
	cfg.region_warp = 0;
	cfg.region_width = 512;
	cfg.region_transition = 128;
	std::unique_ptr<TerrainGenerator> gen = compile_or_fail(cfg, table);

	// The first border whose two regions picked different biomes.
	int first_blend = -1, last_blend = -1;
	for (int x = -8192; x < 8192; ++x) {
		const bool blending = gen->weights_at(x, gen->surface_height(x) + 100).count == 2;
		if (blending && first_blend < 0) first_blend = x;
		if (first_blend >= 0 && !blending) break;
		if (blending) last_blend = x;
	}
	REQUIRE(first_blend >= 0);

	// Rows where a cell and its right neighbour disagree, over 512 rows below the taper.
	auto flips = [&](int x) {
		int count = 0;
		for (int y = 100; y < 612; ++y) count += gen->cave_at(x, y) != gen->cave_at(x + 1, y);
		return count;
	};
	double outside = 0;
	for (int i = 1; i <= 64; ++i) outside += flips(first_blend - i) + flips(last_blend + i);
	outside /= 128.0;
	REQUIRE(outside > 0);

	int caves = 0;
	for (int x = first_blend; x <= last_blend; ++x) {
		CHECK_MESSAGE(flips(x) <= 3.0 * outside,
		              "column " << x << " flips " << flips(x) << " rows, around it " << outside);
		for (int y = 100; y < 612; ++y) caves += gen->cave_at(x, y);
	}
	CHECK(caves > 0);
}

TEST_CASE("crust holds under the caves") {
	MaterialTable table = terraintest::terrain_table();
	TerrainConfig cfg = terraintest::crust_config();
	cfg.entrance_threshold = 2.0f;  // entrances off
	std::unique_ptr<TerrainGenerator> gen = compile_or_fail(cfg, table);
	terraintest::ChunkCache cache(*gen, test_seed());
	const MatId sandstone = table.id_of("sandstone");

	int under_cave = 0;  // crust cells the cave field would have carved
	for (int x = -2000; x < 2000; ++x) {
		const int s = gen->surface_height(x);
		for (int d = 6; d < 12; ++d) {
			REQUIRE_MESSAGE(cache.at(x, s + d) == sandstone, "no crust at x " << x << ", depth " << d);
			under_cave += gen->cave_at(x, s + d);
		}
	}
	CHECK(under_cave > 0);
}

TEST_CASE("entrances cut the crust") {
	MaterialTable table = terraintest::terrain_table();
	std::unique_ptr<TerrainGenerator> gen = compile_or_fail(terraintest::crust_config(), table);
	terraintest::ChunkCache cache(*gen, test_seed());
	const MatId sandstone = table.id_of("sandstone");

	int cut = 0;
	for (int x = -20000; x < 20000; ++x) {
		const int s = gen->surface_height(x);
		for (int d = 6; d < 12; ++d) {
			if (cache.at(x, s + d) == sandstone) continue;
			REQUIRE_MESSAGE(gen->entrance_at(x), "crust cut off an entrance at x " << x);
			++cut;
		}
	}
	CHECK(cut > 0);
}

TEST_CASE("no crust when crust_depth is not positive") {
	MaterialTable table = terraintest::terrain_table();
	const MatId sandstone = table.id_of("sandstone");
	for (int depth : {0, -3}) {
		TerrainConfig cfg = terraintest::crust_config();
		cfg.biomes[0].crust_depth = depth;
		std::unique_ptr<TerrainGenerator> gen = compile_or_fail(cfg, table);
		terraintest::ChunkCache cache(*gen, test_seed());
		for (int x = -500; x < 500; ++x) {
			const int s = gen->surface_height(x);
			for (int d = 0; d < 40; ++d)
				REQUIRE_MESSAGE(cache.at(x, s + d) != sandstone,
				                "sandstone at x " << x << ", depth " << d << " with crust_depth " << depth);
		}
	}
}
