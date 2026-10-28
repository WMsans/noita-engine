#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <vector>

#include "chunk.h"
#include "chunk_streamer.h"
#include "coords.h"
#include "doctest/doctest.h"
#include "generator.h"
#include "sim_test_util.h"
#include "simulator.h"
#include "world_grid.h"

namespace {

// Ticks until a tick simulates nothing, so the world has settled. A freshly loaded world is
// never quiet on the first tick: `next` covers the whole chunk.
void tick_until_quiet(Simulator& sim, int limit) {
	bool quiet = false;
	for (int i = 0; i < limit && !quiet; ++i) {
		sim.tick();
		quiet = sim.last_stats().chunks_simulated == 0;
	}
	REQUIRE(quiet);
}

int count_in_chunk(WorldGrid& grid, ChunkCoord coord, MatId mat) {
	Chunk* chunk = grid.find(coord);
	REQUIRE(chunk != nullptr);
	int n = 0;
	for (int ly = 0; ly < CHUNK; ++ly)
		for (int lx = 0; lx < CHUNK; ++lx)
			if (chunk->at(lx, ly).mat == mat) ++n;
	return n;
}

// Fills every chunk with air and stamps its coord into the first two cells' temperature,
// so a delivered chunk can be matched back to the coord it was generated for. Sleeps
// `sleep_ms` per chunk so shutdown can be observed with work in flight.
class RecordingGen : public ChunkGenerator {
public:
	explicit RecordingGen(int sleep_ms = 0) : sleep_ms_(sleep_ms) {}

	void generate(ChunkCoord coord, Chunk& out, uint32_t seed) const override {
		(void)seed;
		if (sleep_ms_ > 0) std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms_));
		for (int ly = 0; ly < CHUNK; ++ly)
			for (int lx = 0; lx < CHUNK; ++lx) out.at(lx, ly) = Cell{MAT_AIR, AMBIENT_TEMP, 0, 0, 0, 0};
		out.at(0, 0).temp = (int16_t)coord.x;
		out.at(1, 0).temp = (int16_t)coord.y;
		std::lock_guard<std::mutex> lock(mutex_);
		recorded_.push_back(coord);
	}

	std::vector<ChunkCoord> recorded() const {
		std::lock_guard<std::mutex> lock(mutex_);
		return recorded_;
	}

private:
	const int sleep_ms_;
	mutable std::mutex mutex_;
	mutable std::vector<ChunkCoord> recorded_;
};

std::vector<ChunkCoord> block(ChunkCoord centre, int rx, int ry) {
	std::vector<ChunkCoord> coords;
	coords.reserve((size_t)(2 * rx + 1) * (size_t)(2 * ry + 1));
	for (int y = centre.y - ry; y <= centre.y + ry; ++y)
		for (int x = centre.x - rx; x <= centre.x + rx; ++x) coords.push_back({x, y});
	return coords;
}

// Loads (0,0) alone, settles it, then inserts `added` and returns the new dirty rect of
// (0,0). Each case gets a fresh grid because a DirtyRect is one bounding box.
DirtyRect next_after_insert(ChunkCoord added) {
	MaterialTable table = simtest::movement_table();
	BoxGenerator gen(-4096, -4096, 4096, 4096, table.id_of("rock"), table);
	WorldGrid grid(table, 77);
	Chunk* centre = grid.load({0, 0}, gen);
	REQUIRE(centre != nullptr);

	Simulator sim(grid);
	tick_until_quiet(sim, 10);
	grid.insert(make_chunk(added, gen, grid.seed()));
	return centre->next;
}

}  // namespace

TEST_CASE("ChunkRect is inclusive and empty when inverted") {
	const ChunkRect r{0, 0, 2, 1};
	CHECK(r.contains({0, 0}));
	CHECK(r.contains({2, 1}));
	CHECK(r.contains({1, 0}));
	CHECK(!r.contains({-1, 0}));
	CHECK(!r.contains({0, 2}));

	const ChunkRect around = ChunkRect::around({5, -3}, 2, 1);
	CHECK(around.x0 == 3);
	CHECK(around.y0 == -4);
	CHECK(around.x1 == 7);
	CHECK(around.y1 == -2);

	// An empty rect (y1 < y0) holds nothing, even where the inverted span covers a coord.
	const ChunkRect empty{0, 0, 4, -1};
	CHECK(!empty.contains({0, 0}));
	CHECK(!empty.contains({2, -1}));
	CHECK(!empty.contains({-5, -5}));
}

TEST_CASE("chunk outside the active rect sleeps and keeps its marks") {
	MaterialTable table = simtest::movement_table();
	const MatId sand = table.id_of("sand");
	WorldGrid grid(table, 8);
	simtest::load_arena(grid, table, 0, 0, 128, 64, table.id_of("rock"));

	Simulator sim(grid);
	simtest::tick_n(sim, 2);         // settle: both fresh chunks simulate once, then are quiet
	REQUIRE(grid.set(74, 10, sand));  // local (10,10) of chunk (1,0)

	sim.set_active_rect(ChunkRect{0, 0, 0, 0});
	simtest::tick_n(sim, 20);
	CHECK(simtest::mat_at(grid, 74, 10) == sand);
	CHECK(simtest::locate(grid, sand).y == 10);

	sim.set_active_rect(std::nullopt);
	simtest::tick_n(sim, 20);
	CHECK(simtest::mat_at(grid, 74, 10) != sand);
	CHECK(simtest::locate(grid, sand).y > 10);
}

TEST_CASE("active rect stats") {
	MaterialTable table = simtest::movement_table();
	WorldGrid grid(table, 9);
	simtest::load_arena(grid, table, 0, 0, 128, 128, table.id_of("rock"));  // 4 chunks

	Simulator sim(grid);
	sim.set_active_rect(ChunkRect{0, 0, 0, 0});
	sim.tick();
	CHECK(sim.last_stats().chunks_active == 1);

	sim.set_active_rect(std::nullopt);
	sim.tick();
	CHECK(sim.last_stats().chunks_active == 4);  // no rect: everything loaded is active
}

TEST_CASE("insert returns the existing chunk on a duplicate") {
	MaterialTable table = simtest::movement_table();
	BoxGenerator gen(-4096, -4096, 4096, 4096, table.id_of("rock"), table);
	WorldGrid grid(table, 10);
	CHECK(grid.size() == 0);

	Chunk* first = grid.insert(make_chunk({0, 0}, gen, grid.seed()));
	REQUIRE(first != nullptr);
	CHECK(first->render_dirty.load());
	first->render_dirty = false;
	first->next.reset();

	Chunk* second = grid.insert(make_chunk({0, 0}, gen, grid.seed()));
	CHECK(second == first);
	CHECK(grid.size() == 1);
	CHECK(first->next.empty());  // the existing chunk's dirty rects are left alone
	CHECK(!first->render_dirty.load());
}

TEST_CASE("insert wakes the touching border of loaded neighbours") {
	const DirtyRect right = next_after_insert({1, 0});
	CHECK(right.x0 == 63);
	CHECK(right.x1 == 63);
	CHECK(right.y0 == 0);
	CHECK(right.y1 == 63);

	const DirtyRect below = next_after_insert({0, 1});
	CHECK(below.x0 == 0);
	CHECK(below.x1 == 63);
	CHECK(below.y0 == 63);
	CHECK(below.y1 == 63);

	const DirtyRect diagonal = next_after_insert({1, 1});
	CHECK(diagonal.x0 == 63);
	CHECK(diagonal.x1 == 63);
	CHECK(diagonal.y0 == 63);
	CHECK(diagonal.y1 == 63);
}

TEST_CASE("streamer delivers every requested chunk") {
	RecordingGen gen;
	ChunkStreamer streamer(gen, 7, 4);
	const std::vector<ChunkCoord> wanted = block({0, 0}, 4, 4);  // 9x9
	REQUIRE(wanted.size() == 81);
	std::set<int64_t> wanted_keys;
	for (ChunkCoord c : wanted) wanted_keys.insert(chunk_key(c));

	streamer.request(wanted, {0, 0});
	streamer.wait_idle();
	CHECK(streamer.pending() == 0);

	std::vector<std::unique_ptr<Chunk>> got = streamer.take_ready(1000);
	REQUIRE(got.size() == 81);
	std::set<int64_t> seen;
	for (const std::unique_ptr<Chunk>& chunk : got) {
		REQUIRE(chunk != nullptr);
		CHECK(wanted_keys.count(chunk_key(chunk->coord)) == 1);
		CHECK((int)chunk->at(0, 0).temp == chunk->coord.x);
		CHECK((int)chunk->at(1, 0).temp == chunk->coord.y);
		seen.insert(chunk_key(chunk->coord));
	}
	CHECK(seen.size() == 81);  // no coord twice
}

TEST_CASE("streamer generates nearest first") {
	RecordingGen gen;
	ChunkStreamer streamer(gen, 3, 1);
	const ChunkCoord centre{100, -40};
	const std::vector<ChunkCoord> wanted = block(centre, 7, 7);  // 15x15
	REQUIRE(wanted.size() == 225);

	streamer.request(wanted, centre);
	streamer.wait_idle();

	const std::vector<ChunkCoord> order = gen.recorded();
	REQUIRE(order.size() == 225);
	int64_t previous = -1;
	for (ChunkCoord c : order) {
		const int64_t dx = c.x - centre.x;
		const int64_t dy = c.y - centre.y;
		const int64_t dist = dx * dx + dy * dy;
		CHECK(dist >= previous);
		previous = dist;
	}
}

TEST_CASE("streamer drops chunks no longer requested") {
	RecordingGen gen;
	ChunkStreamer streamer(gen, 11, 2);
	const std::vector<ChunkCoord> a = block({0, 0}, 1, 1);
	const std::vector<ChunkCoord> b = block({100, 100}, 1, 1);
	std::set<int64_t> b_keys;
	for (ChunkCoord c : b) b_keys.insert(chunk_key(c));

	streamer.request(a, {0, 0});
	streamer.wait_idle();
	streamer.request(b, {100, 100});
	streamer.wait_idle();

	std::vector<std::unique_ptr<Chunk>> got = streamer.take_ready(1000);
	REQUIRE(got.size() == b.size());
	for (const std::unique_ptr<Chunk>& chunk : got) {
		REQUIRE(chunk != nullptr);
		CHECK(b_keys.count(chunk_key(chunk->coord)) == 1);
	}
}

TEST_CASE("streamer does not regenerate a delivered chunk") {
	RecordingGen gen;
	ChunkStreamer streamer(gen, 5, 2);

	streamer.request({{0, 0}}, {0, 0});
	streamer.wait_idle();
	CHECK(streamer.take_ready(10).size() == 1);

	streamer.request({{0, 0}}, {0, 0});
	streamer.wait_idle();
	CHECK(streamer.pending() == 0);
	CHECK(streamer.take_ready(10).empty());

	int calls = 0;
	for (ChunkCoord c : gen.recorded())
		if (c == ChunkCoord{0, 0}) ++calls;
	CHECK(calls == 1);
}

// The world drops a delivered chunk whose coord left the loaded rect; without the forget that
// coord stays delivered and is never re-queued, so the chunk is unloaded forever.
TEST_CASE("streamer regenerates a forgotten coord") {
	RecordingGen gen;
	ChunkStreamer streamer(gen, 19, 2);

	streamer.request({{0, 0}}, {0, 0});
	streamer.wait_idle();
	REQUIRE(streamer.take_ready(10).size() == 1);

	streamer.forget({0, 0});
	streamer.request({{0, 0}}, {0, 0});
	streamer.wait_idle();
	CHECK(streamer.pending() == 0);
	CHECK(streamer.take_ready(10).size() == 1);

	int calls = 0;
	for (ChunkCoord c : gen.recorded())
		if (c == ChunkCoord{0, 0}) ++calls;
	CHECK(calls == 2);
}

TEST_CASE("streamer shuts down with work in flight") {
	RecordingGen gen(5);  // 5 ms per chunk
	const std::vector<ChunkCoord> all = block({0, 0}, 9, 9);
	const std::vector<ChunkCoord> many(all.begin(), all.begin() + 200);
	REQUIRE(many.size() == 200);

	const auto start = std::chrono::steady_clock::now();
	{
		ChunkStreamer streamer(gen, 13, 2);
		streamer.request(many, {0, 0});
	}  // destroyed with work still queued and in flight
	const double ms =
	    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	CHECK(ms < 1000.0);
}

TEST_CASE("take_stats counts generated chunks") {
	RecordingGen gen;
	ChunkStreamer streamer(gen, 17, 4);
	streamer.request(block({0, 0}, 4, 4), {0, 0});
	streamer.wait_idle();

	const StreamStats stats = streamer.take_stats();
	CHECK(stats.generated == 81);
	CHECK(stats.gen_ms_total > 0.0);

	const StreamStats again = streamer.take_stats();
	CHECK(again.generated == 0);
	CHECK(again.gen_ms_total == 0.0);
}

TEST_CASE("water held by an unloaded neighbour flows in once it loads") {
	MaterialTable table = simtest::movement_table();
	const MatId water = table.id_of("water");
	// Chunks (0,0) and (1,0) are air inside the arena, x [0,128) and y [0,64).
	BoxGenerator gen(0, 0, 128, 64, table.id_of("rock"), table);
	WorldGrid grid(table, 4242);
	REQUIRE(grid.load({0, 0}, gen) != nullptr);

	int placed = 0;
	for (int y = 40; y < CHUNK; ++y)
		if (grid.set(63, y, water)) ++placed;
	REQUIRE(placed == 24);

	Simulator sim(grid);
	// A free water surface never goes quiet (a surface cell always has somewhere to spread),
	// so settle with a fixed count instead of the usual until-quiet loop.
	simtest::tick_n(sim, 200);
	CHECK(grid.find({1, 0}) == nullptr);
	CHECK(simtest::count_mat(grid, water) == 24);
	CHECK(count_in_chunk(grid, {0, 0}, water) == 24);

	// (1,0) acts solid until it is loaded, so nothing crossed the border yet; inserting it
	// wakes the border column of (0,0) and the water spills over.
	grid.insert(make_chunk({1, 0}, gen, grid.seed()));
	simtest::tick_n(sim, 60);
	CHECK(count_in_chunk(grid, {1, 0}, water) > 0);
	CHECK(simtest::count_mat(grid, water) == 24);
}
