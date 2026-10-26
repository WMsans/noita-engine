#include <cmath>
#include <string>
#include <vector>

#include "chunk.h"
#include "doctest/doctest.h"
#include "material.h"
#include "outline.h"
#include "rng.h"

namespace {

Mask make_mask(int w, int h, const std::vector<std::pair<int, int>>& cells) {
	Mask m((size_t)w * h, 0);
	for (auto [x, y] : cells) m[(size_t)y * w + x] = 1;
	return m;
}

Mask random_mask(int w, int h, uint32_t seed) {
	Mask m((size_t)w * h, 0);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) m[(size_t)y * w + x] = hash_unit(hash5(seed, 0, x, y, 0)) < 0.5f;
	return m;
}

bool solid_at(const Mask& m, int w, int h, float fx, float fy) {
	const int x = (int)std::floor(fx), y = (int)std::floor(fy);
	if (x < 0 || y < 0 || x >= w || y >= h) return false;
	return m[(size_t)y * w + x] != 0;
}

// Every edge: half a cell along its right normal is empty, half a cell against it is solid.
void check_winding(const Mask& m, int w, int h) {
	for (const Loop& loop : trace_loops(m, w, h))
		for (size_t i = 0; i < loop.size(); ++i) {
			const b2Vec2 a = loop[i], b = loop[(i + 1) % loop.size()];
			const float len = std::hypot(b.x - a.x, b.y - a.y);
			REQUIRE(len > 0);
			const float nx = (b.y - a.y) / len, ny = -(b.x - a.x) / len;
			// Sample half a cell from the edge's first unit step, so long merged edges are
			// checked next to a real cell rather than at a midpoint between corners.
			const float mx = a.x + (b.x - a.x) / len * 0.5f, my = a.y + (b.y - a.y) / len * 0.5f;
			CHECK_FALSE(solid_at(m, w, h, mx + 0.5f * nx, my + 0.5f * ny));
			CHECK(solid_at(m, w, h, mx - 0.5f * nx, my - 0.5f * ny));
		}
}

float signed_area(const Loop& loop) {
	float a = 0;
	for (size_t i = 0; i < loop.size(); ++i) {
		const b2Vec2 p = loop[i], q = loop[(i + 1) % loop.size()];
		a += p.x * q.y - q.x * p.y;
	}
	return a * 0.5f;
}

float point_segment_distance(b2Vec2 p, b2Vec2 a, b2Vec2 b) {
	const float dx = b.x - a.x, dy = b.y - a.y;
	const float len2 = dx * dx + dy * dy;
	float t = len2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0;
	t = std::fmax(0.0f, std::fmin(1.0f, t));
	return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

}  // namespace

TEST_CASE("single cell is one square loop") {
	const std::vector<Loop> loops = trace_loops(make_mask(1, 1, {{0, 0}}), 1, 1);
	REQUIRE(loops.size() == 1);
	REQUIRE(loops[0].size() == 4);
	// Rotate so the loop starts at (0,0), then check the order.
	size_t start = 0;
	for (size_t i = 0; i < 4; ++i)
		if (loops[0][i].x == 0 && loops[0][i].y == 0) start = i;
	const float want[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
	for (size_t i = 0; i < 4; ++i) {
		CHECK(loops[0][(start + i) % 4].x == want[i][0]);
		CHECK(loops[0][(start + i) % 4].y == want[i][1]);
	}
}

TEST_CASE("every edge's right normal points into empty space") {
	check_winding(make_mask(3, 3, {{0, 0}, {0, 1}, {0, 2}, {1, 2}, {2, 2}}), 3, 3);  // L
	Mask ring = make_mask(3, 3, {{0, 0}, {1, 0}, {2, 0}, {0, 1}, {2, 1}, {0, 2}, {1, 2}, {2, 2}});
	check_winding(ring, 3, 3);
	for (uint32_t seed = 1; seed <= 20; ++seed) check_winding(random_mask(16, 16, seed), 16, 16);
}

TEST_CASE("ring gives outer and hole loops") {
	Mask m((size_t)5 * 5, 1);
	for (int y = 1; y <= 3; ++y)
		for (int x = 1; x <= 3; ++x) m[(size_t)y * 5 + x] = 0;
	const std::vector<Loop> loops = trace_loops(m, 5, 5);
	REQUIRE(loops.size() == 2);
	CHECK(signed_area(loops[0]) * signed_area(loops[1]) < 0);
}

TEST_CASE("diagonal neighbours are separate loops") {
	const std::vector<Loop> loops = trace_loops(make_mask(2, 2, {{0, 0}, {1, 1}}), 2, 2);
	REQUIRE(loops.size() == 2);
	CHECK(loops[0].size() == 4);
	CHECK(loops[1].size() == 4);
}

TEST_CASE("collinear points merge") {
	Mask bar((size_t)10, 1);
	const std::vector<Loop> loops = trace_loops(bar, 10, 1);
	REQUIRE(loops.size() == 1);
	CHECK(loops[0].size() == 4);
}

TEST_CASE("simplify keeps shape within epsilon") {
	const int n = 20;
	Mask stairs((size_t)n * n, 0);
	for (int y = 0; y < n; ++y)
		for (int x = 0; x <= y; ++x) stairs[(size_t)y * n + x] = 1;
	const std::vector<Loop> loops = trace_loops(stairs, n, n);
	REQUIRE(loops.size() == 1);
	const Loop simple = simplify_loop(loops[0], 1.0f);
	CHECK(simple.size() < loops[0].size());
	CHECK(simple.size() >= 3);
	for (const b2Vec2& p : loops[0]) {
		float best = 1e9f;
		for (size_t i = 0; i < simple.size(); ++i)
			best = std::fmin(best, point_segment_distance(p, simple[i], simple[(i + 1) % simple.size()]));
		CHECK(best <= 1.0f + 1e-4f);
	}
	const Loop square = simplify_loop(trace_loops(make_mask(1, 1, {{0, 0}}), 1, 1)[0], 5.0f);
	CHECK(square.size() >= 3);
}

TEST_CASE("pixel hull") {
	Mask full((size_t)8 * 8, 1);
	const Loop square = pixel_hull(full, 8, 8);
	CHECK(square.size() == 4);
	CHECK(std::fabs(std::fabs(signed_area(square)) - 64.0f) < 1e-3f);

	const int d = 17;
	Mask disc((size_t)d * d, 0);
	int pixels = 0;
	for (int y = 0; y < d; ++y)
		for (int x = 0; x < d; ++x) {
			const float dx = x + 0.5f - d / 2.0f, dy = y + 0.5f - d / 2.0f;
			if (dx * dx + dy * dy <= 8.0f * 8.0f) {
				disc[(size_t)y * d + x] = 1;
				++pixels;
			}
		}
	const Loop hull = pixel_hull(disc, d, d);
	CHECK(hull.size() <= B2_MAX_POLYGON_VERTICES);
	CHECK(std::fabs(std::fabs(signed_area(hull)) - pixels) <= 0.1f * pixels);

	CHECK(pixel_hull(Mask((size_t)4 * 4, 0), 4, 4).empty());
}

TEST_CASE("pixel boxes cover exactly the pixels") {
	auto check = [](const Mask& m, int w, int h) {
		std::vector<int> cover((size_t)w * h, 0);
		for (const PixelRect& r : pixel_boxes(m, w, h))
			for (int y = r.y; y < r.y + r.h; ++y)
				for (int x = r.x; x < r.x + r.w; ++x) ++cover[(size_t)y * w + x];
		for (size_t i = 0; i < cover.size(); ++i) CHECK(cover[i] == (int)m[i]);
	};
	check(make_mask(3, 3, {{0, 0}, {0, 1}, {0, 2}, {1, 2}, {2, 2}}), 3, 3);
	check(random_mask(12, 12, 7), 12, 12);
}

TEST_CASE("chunk mask skips body pixels and non-solids") {
	std::vector<MaterialDef> defs(3);
	defs[0].name = "rock";
	defs[0].cls = MatClass::Solid;
	defs[0].collision_solid = true;
	defs[1].name = "sand";
	defs[1].cls = MatClass::Powder;
	defs[1].collision_solid = true;
	defs[2].name = "water";
	defs[2].cls = MatClass::Liquid;
	MaterialTable table;
	std::string error;
	REQUIRE(MaterialTable::compile(defs, {}, table, error));

	Chunk chunk;
	for (Cell& c : chunk.cells) c = Cell{};
	chunk.at(0, 0).mat = table.id_of("rock");
	chunk.at(1, 0).mat = table.id_of("sand");
	chunk.at(2, 0).mat = table.id_of("water");
	chunk.at(3, 0).mat = table.id_of("rock");
	chunk.at(3, 0).flags |= FLAG_BODY;

	Mask m;
	chunk_solid_mask(chunk, table, m);
	REQUIRE(m.size() == (size_t)CHUNK * CHUNK);
	CHECK(m[0] == 1);
	CHECK(m[1] == 1);
	CHECK(m[2] == 0);
	CHECK(m[3] == 0);
	CHECK(m[4] == 0);
}
