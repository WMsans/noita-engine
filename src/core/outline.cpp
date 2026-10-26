#include "outline.h"

#include <algorithm>
#include <cmath>

namespace {

struct Dir {
	int dx, dy;
	bool operator==(const Dir& o) const { return dx == o.dx && dy == o.dy; }
};

float cross(b2Vec2 o, b2Vec2 a, b2Vec2 b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }

// Drops points that sit on a straight line between their neighbours (cyclically).
Loop merge_collinear(const Loop& in) {
	Loop out;
	const size_t n = in.size();
	for (size_t i = 0; i < n; ++i) {
		const b2Vec2 prev = in[(i + n - 1) % n], p = in[i], next = in[(i + 1) % n];
		if (cross(prev, p, next) != 0.0f) out.push_back(p);
	}
	return out;
}

float segment_distance(b2Vec2 p, b2Vec2 a, b2Vec2 b) {
	const float dx = b.x - a.x, dy = b.y - a.y;
	const float len2 = dx * dx + dy * dy;
	float t = len2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0;
	t = std::clamp(t, 0.0f, 1.0f);
	return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

// Douglas-Peucker on the open run loop[i..j] (indices modulo n), marking points to keep.
void dp(const Loop& loop, size_t i, size_t j, float eps, std::vector<uint8_t>& keep) {
	const size_t n = loop.size();
	float best = -1;
	size_t best_k = i;
	for (size_t k = (i + 1) % n; k != j; k = (k + 1) % n) {
		const float d = segment_distance(loop[k], loop[i], loop[j]);
		if (d > best) {
			best = d;
			best_k = k;
		}
	}
	if (best > eps) {
		keep[best_k] = 1;
		dp(loop, i, best_k, eps, keep);
		dp(loop, best_k, j, eps, keep);
	}
}

}  // namespace

void chunk_solid_mask(const Chunk& chunk, const Chunk* below, const MaterialTable& table, Mask& out) {
	out.assign((size_t)CHUNK * CHUNK, 0);
	auto solid = [&](const Cell& c) { return table.get(c.mat).collision_solid && !(c.flags & FLAG_BODY); };
	for (int x = 0; x < CHUNK; ++x)
		for (int y = CHUNK - 1; y >= 0; --y) {  // bottom up, so the support below is known
			const size_t i = (size_t)y * CHUNK + x;
			const Cell& c = chunk.cells[i];
			if (!solid(c)) continue;
			if (table.get(c.mat).cls == MatClass::Powder) {
				const bool supported = y + 1 < CHUNK ? out[i + CHUNK] != 0 : (!below || solid(below->cells[(size_t)x]));
				if (!supported) continue;
			}
			out[i] = 1;
		}
}

std::vector<Loop> trace_loops(const Mask& mask, int w, int h) {
	auto solid = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && mask[(size_t)y * w + x]; };
	// Up to two outgoing edges per vertex (two only at a diagonal saddle).
	const int vw = w + 1;
	std::vector<Dir> out_dir((size_t)vw * (h + 1) * 2, Dir{0, 0});
	std::vector<uint8_t> out_count((size_t)vw * (h + 1), 0);
	auto add = [&](int x, int y, Dir d) {
		const size_t v = (size_t)y * vw + x;
		out_dir[v * 2 + out_count[v]++] = d;
	};
	size_t edges = 0;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			if (!solid(x, y)) continue;
			// Right normal (d.y, -d.x) must point at the empty neighbour.
			if (!solid(x, y - 1)) add(x, y, {1, 0}), ++edges;           // top, going +x
			if (!solid(x + 1, y)) add(x + 1, y, {0, 1}), ++edges;       // right, going +y
			if (!solid(x, y + 1)) add(x + 1, y + 1, {-1, 0}), ++edges;  // bottom, going -x
			if (!solid(x - 1, y)) add(x, y + 1, {0, -1}), ++edges;      // left, going -y
		}

	std::vector<Loop> loops;
	while (edges > 0) {
		// Any vertex with an unused outgoing edge starts a loop.
		size_t start = 0;
		while (out_count[start] == 0) ++start;
		int x = (int)(start % vw), y = (int)(start / vw);
		Loop loop;
		Dir in{0, 0};
		while (true) {
			const size_t v = (size_t)y * vw + x;
			if (out_count[v] == 0) break;  // back at the start
			// At a saddle take the right turn (-in.dy, in.dx), which keeps corner-touching cells
			// in separate loops.
			int pick = 0;
			if (out_count[v] == 2 && !(out_dir[v * 2] == Dir{-in.dy, in.dx})) pick = 1;
			const Dir d = out_dir[v * 2 + pick];
			out_dir[v * 2 + pick] = out_dir[v * 2 + out_count[v] - 1];
			--out_count[v];
			--edges;
			loop.push_back(b2Vec2{(float)x, (float)y});
			x += d.dx;
			y += d.dy;
			in = d;
		}
		loops.push_back(merge_collinear(loop));
	}
	return loops;
}

Loop simplify_loop(const Loop& loop, float epsilon) {
	const size_t n = loop.size();
	if (n <= 3) return loop;
	// Anchor at point 0 and the point farthest from it, then simplify both halves.
	size_t far = 0;
	float best = -1;
	for (size_t k = 1; k < n; ++k) {
		const float d = std::hypot(loop[k].x - loop[0].x, loop[k].y - loop[0].y);
		if (d > best) {
			best = d;
			far = k;
		}
	}
	std::vector<uint8_t> keep(n, 0);
	keep[0] = keep[far] = 1;
	dp(loop, 0, far, epsilon, keep);
	dp(loop, far, 0, epsilon, keep);
	Loop out;
	for (size_t k = 0; k < n; ++k)
		if (keep[k]) out.push_back(loop[k]);
	if (out.size() < 3) {  // keep a triangle: add the point farthest from the 0-far segment
		size_t extra = 1;
		float d_best = -1;
		for (size_t k = 0; k < n; ++k) {
			const float d = segment_distance(loop[k], loop[0], loop[far]);
			if (d > d_best) {
				d_best = d;
				extra = k;
			}
		}
		keep[extra] = 1;
		out.clear();
		for (size_t k = 0; k < n; ++k)
			if (keep[k]) out.push_back(loop[k]);
	}
	return out;
}

Loop pixel_hull(const Mask& mask, int w, int h) {
	std::vector<b2Vec2> pts;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			if (mask[(size_t)y * w + x])
				for (int c = 0; c < 4; ++c) pts.push_back(b2Vec2{(float)(x + (c & 1)), (float)(y + (c >> 1))});
	if (pts.empty()) return {};
	std::sort(pts.begin(), pts.end(), [](b2Vec2 a, b2Vec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
	pts.erase(std::unique(pts.begin(), pts.end(), [](b2Vec2 a, b2Vec2 b) { return a.x == b.x && a.y == b.y; }),
	          pts.end());

	// Andrew's monotone chain.
	Loop hull(pts.size() * 2);
	size_t k = 0;
	for (size_t i = 0; i < pts.size(); ++i) {
		while (k >= 2 && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0) --k;
		hull[k++] = pts[i];
	}
	for (size_t i = pts.size() - 1, lower = k + 1; i-- > 0;) {
		while (k >= lower && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0) --k;
		hull[k++] = pts[i];
	}
	hull.resize(k - 1);

	// Drop the vertex whose triangle with its neighbours is smallest until it fits Box2D.
	while (hull.size() > B2_MAX_POLYGON_VERTICES) {
		const size_t n = hull.size();
		size_t drop = 0;
		float smallest = INFINITY;
		for (size_t i = 0; i < n; ++i) {
			const float a = std::fabs(cross(hull[(i + n - 1) % n], hull[i], hull[(i + 1) % n]));
			if (a < smallest) {
				smallest = a;
				drop = i;
			}
		}
		hull.erase(hull.begin() + (long)drop);
	}
	return hull;
}

std::vector<PixelRect> pixel_boxes(const Mask& mask, int w, int h) {
	std::vector<uint8_t> used((size_t)w * h, 0);
	auto free_solid = [&](int x, int y) { return mask[(size_t)y * w + x] && !used[(size_t)y * w + x]; };
	std::vector<PixelRect> boxes;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			if (!free_solid(x, y)) continue;
			int bw = 1;
			while (x + bw < w && free_solid(x + bw, y)) ++bw;
			int bh = 1;
			while (y + bh < h) {
				bool row = true;
				for (int i = 0; i < bw && row; ++i) row = free_solid(x + i, y + bh);
				if (!row) break;
				++bh;
			}
			for (int yy = y; yy < y + bh; ++yy)
				for (int xx = x; xx < x + bw; ++xx) used[(size_t)yy * w + xx] = 1;
			boxes.push_back({x, y, bw, bh});
		}
	return boxes;
}
