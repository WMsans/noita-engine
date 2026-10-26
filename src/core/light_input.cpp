#include "light_input.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "cell.h"
#include "chunk.h"
#include "coords.h"
#include "job_pool.h"
#include "world_grid.h"

namespace {

// Rows per job: each row walks chunk by chunk, so one find() per 64 cells.
constexpr int LIGHT_ROW_BAND = 16;

// One cell's light texel: its material's emission and opacity, plus the fire glow when it burns.
// Both the scale-1 row and the block path pack a cell through here, so they cannot drift apart.
static inline LightTexel cell_texel(const Cell& cell, const LightPalette& palette) {
	LightTexel t = cell.mat < palette.mats.size() ? palette.mats[cell.mat] : LightTexel{0, 0, 0, 0};
	if (cell.flags & FLAG_BURNING) {  // per-channel max of material and fire
		if (palette.fire.r > t.r) t.r = palette.fire.r;
		if (palette.fire.g > t.g) t.g = palette.fire.g;
		if (palette.fire.b > t.b) t.b = palette.fire.b;
	}
	return t;
}

uint8_t pack_emission(float e) {
	return (uint8_t)std::lround(std::min(std::max(e / EMISSION_RANGE, 0.0f), 1.0f) * 255.0f);
}

uint8_t pack_opacity(float a) {
	return (uint8_t)std::lround(std::min(std::max(a, 0.0f), 1.0f) * 255.0f);
}

void build_row(const WorldGrid& grid, const LightPalette& palette, const LightArea& area, int row,
               int tx0, int tx1, LightTexel* out) {
	const int wy = area.y0 + row;
	const int ly = floor_mod(wy, CHUNK);
	LightTexel* dst = out + (size_t)row * light_texel_w(area) + tx0;
	const int wx_end = area.x0 + tx1;
	for (int wx = area.x0 + tx0; wx < wx_end;) {
		Chunk* chunk = grid.find(chunk_of(wx, wy));  // once per 64 cells of the row
		const int lx = floor_mod(wx, CHUNK);
		const int n = std::min(CHUNK - lx, wx_end - wx);
		if (chunk) {
			const Cell* src = &chunk->cells[(size_t)ly * CHUNK + lx];
			for (int i = 0; i < n; ++i) dst[i] = cell_texel(src[i], palette);
		} else {
			const LightTexel dark{0, 0, 0, 255};  // unloaded chunks are opaque and dark
			for (int i = 0; i < n; ++i) dst[i] = dark;
		}
		dst += n;
		wx += n;
	}
}

void build_texel_row(const WorldGrid& grid, const LightPalette& palette, const LightArea& area,
                     int texel_row, int tx0, int tx1, LightTexel* out, uint32_t* opacity_sum) {
	const int scale = area.scale;
	const int texel_w = light_texel_w(area);
	const int y0 = area.y0 + texel_row * scale;
	const int y1 = std::min(y0 + scale, area.y0 + area.h);
	LightTexel* const dst = out + (size_t)texel_row * texel_w;
	for (int tx = tx0; tx < tx1; ++tx) dst[tx] = LightTexel{0, 0, 0, 0};
	std::fill(opacity_sum + tx0, opacity_sum + tx1, 0u);
	const int x_end = area.x0 + std::min(tx1 * scale, area.w);
	for (int wy = y0; wy < y1; ++wy) {
		const int ly = floor_mod(wy, CHUNK);
		// The texel column and the cells it still takes: the last column covers fewer cells.
		int tx = tx0;
		int left = std::min(scale, area.w - tx0 * scale);
		for (int wx = area.x0 + tx0 * scale; wx < x_end;) {
			Chunk* chunk = grid.find(chunk_of(wx, wy));  // once per 64 cells of the row
			const int lx = floor_mod(wx, CHUNK);
			const int n = std::min(CHUNK - lx, x_end - wx);
			const Cell* src = chunk ? &chunk->cells[(size_t)ly * CHUNK + lx] : nullptr;
			for (int i = 0; i < n; ++i) {
				// Unloaded chunks are opaque and dark.
				const LightTexel t = src ? cell_texel(src[i], palette) : LightTexel{0, 0, 0, 255};
				LightTexel& d = dst[tx];
				if (t.r > d.r) d.r = t.r;
				if (t.g > d.g) d.g = t.g;
				if (t.b > d.b) d.b = t.b;
				opacity_sum[tx] += t.a;
				if (--left == 0) {
					++tx;
					left = scale;
				}
			}
			wx += n;
		}
	}
	// Opacity is the mean over the cells the block covers, not over scale x scale: a partial block
	// at the area's right or bottom edge keeps a wall opaque instead of diluting it into a leak.
	const int rows = y1 - y0;
	for (int tx = tx0; tx < tx1; ++tx) {
		const int cols = std::min(scale, area.w - tx * scale);
		dst[tx].a = (uint8_t)std::lround((double)opacity_sum[tx] / (double)(cols * rows));
	}
}

// The spot's source disc in texel space: centre, radius and the texels it can write.
struct SpotDisc {
	float x, y;
	int sr;
	LightRect rect;
};

// False for a non-finite spot or one wholly outside the area, which write nothing.
bool spot_disc(const LightArea& area, const LightSpot& s, SpotDisc& out) {
	if (!std::isfinite(s.x) || !std::isfinite(s.y) || !std::isfinite(s.r) || !std::isfinite(s.g) ||
	    !std::isfinite(s.b) || !std::isfinite(s.energy))
		return false;
	// Texel space: the area's origin is texel 0, and the source disc shrinks with the scale.
	const float scale = (float)area.scale;
	const float x = (s.x - (float)area.x0) / scale;
	const float y = (s.y - (float)area.y0) / scale;
	const int sr = std::max(1, spot_source_radius(s.radius) / area.scale);
	// Clamp the disc into the area in floats before any int cast, so a huge coordinate
	// just falls out of range instead of overflowing.
	const float x_lo = std::max(x - (float)sr, 0.0f);
	const float x_hi = std::min(x + (float)sr, (float)(light_texel_w(area) - 1));
	const float y_lo = std::max(y - (float)sr, 0.0f);
	const float y_hi = std::min(y + (float)sr, (float)(light_texel_h(area) - 1));
	if (x_lo > x_hi || y_lo > y_hi) return false;  // wholly outside
	out.x = x;
	out.y = y;
	out.sr = sr;
	// Truncating the lower bound may start one texel early; the distance test drops it.
	out.rect = LightRect{(int)x_lo, (int)y_lo, (int)x_hi - (int)x_lo + 1, (int)y_hi - (int)y_lo + 1};
	return true;
}

}  // namespace

LightArea light_area(float vis_x, float vis_y, float vis_w, float vis_h, int margin, int snap) {
	const float step = (float)snap;
	const float x0 = std::floor((vis_x - (float)margin) / step) * step;
	const float y0 = std::floor((vis_y - (float)margin) / step) * step;
	const float x1 = std::ceil((vis_x + vis_w + (float)margin) / step) * step;
	const float y1 = std::ceil((vis_y + vis_h + (float)margin) / step) * step;
	LightArea a;
	a.x0 = (int)x0;
	a.y0 = (int)y0;
	a.w = (int)(x1 - x0);
	a.h = (int)(y1 - y0);
	return a;
}

int light_texel_w(const LightArea& area) { return (area.w + area.scale - 1) / area.scale; }
int light_texel_h(const LightArea& area) { return (area.h + area.scale - 1) / area.scale; }

bool light_area_fits(const LightArea& area) {
	return area.w > 0 && area.h > 0 && area.w <= LIGHT_AREA_MAX_SIDE &&
	       area.h <= LIGHT_AREA_MAX_SIDE &&
	       (int64_t)area.w * area.h <= LIGHT_AREA_MAX_CELLS;
}

LightPalette LightPalette::build(const MaterialTable& table, float fire_r, float fire_g,
                                float fire_b, float fire_energy) {
	LightPalette pal;
	pal.mats.resize(table.size());
	for (size_t id = 0; id < table.size(); ++id) {
		const CompiledMaterial& m = table.get((MatId)id);
		pal.mats[id] = LightTexel{pack_emission(m.emission_r * m.emission_energy),
		                          pack_emission(m.emission_g * m.emission_energy),
		                          pack_emission(m.emission_b * m.emission_energy),
		                          pack_opacity(m.light_opacity)};
	}
	// The implicit air def keeps the 1.0 default opacity; air must not block light.
	pal.mats[MAT_AIR] = LightTexel{0, 0, 0, 0};
	pal.fire = LightTexel{pack_emission(fire_r * fire_energy), pack_emission(fire_g * fire_energy),
	                      pack_emission(fire_b * fire_energy), 0};
	return pal;
}

std::vector<LightRect> light_disjoint_rects(const std::vector<LightRect>& rects) {
	std::vector<LightRect> rows;
	for (const LightRect& r : rects)
		if (r.w > 0 && r.h > 0) rows.push_back(r);
	std::sort(rows.begin(), rows.end(), [](const LightRect& a, const LightRect& b) {
		return a.y != b.y ? a.y < b.y : a.x < b.x;
	});
	std::vector<LightRect> out;
	for (const LightRect& r : rows) {
		if (!out.empty() && r.y < out.back().y + out.back().h) {
			LightRect& m = out.back();  // r.y >= m.y, so only the far edges grow
			const int x1 = std::max(m.x + m.w, r.x + r.w);
			m.x = std::min(m.x, r.x);
			m.w = x1 - m.x;
			m.h = std::max(m.y + m.h, r.y + r.h) - m.y;
		} else {
			out.push_back(r);
		}
	}
	return out;
}

// Rows banded across the pool: the whole-area build is this over every texel, so the incremental
// path cannot drift from it.
void build_light_rects(const WorldGrid& grid, const LightPalette& palette, const LightArea& area,
                       const std::vector<LightRect>& rects, LightTexel* out, JobPool* pool) {
	if (area.w <= 0 || area.h <= 0 || area.scale < 1) return;
	const int texel_w = light_texel_w(area);
	const std::vector<LightRect> jobs = light_disjoint_rects(rects);
	if (jobs.empty()) return;
	std::vector<int> first_band(jobs.size());  // the global band index each rect starts at
	int bands = 0;
	for (size_t i = 0; i < jobs.size(); ++i) {
		first_band[i] = bands;
		bands += (jobs[i].h + LIGHT_ROW_BAND - 1) / LIGHT_ROW_BAND;
	}
	// One row of opacity scratch per band, allocated once per build and only above scale 1: the
	// default path must not allocate per band every frame.
	std::vector<uint32_t> opacity_sum;
	if (area.scale > 1 && bands > 0) opacity_sum.resize((size_t)texel_w * bands);
	auto work = [&](int band) {
		size_t i = 0;  // the rects are few, so a linear scan per band is cheapest
		while (i + 1 < jobs.size() && band >= first_band[i + 1]) ++i;
		const LightRect& rect = jobs[i];
		const int r0 = rect.y + (band - first_band[i]) * LIGHT_ROW_BAND;
		const int r1 = std::min(r0 + LIGHT_ROW_BAND, rect.y + rect.h);
		uint32_t* const sum = opacity_sum.empty() ? nullptr : &opacity_sum[(size_t)band * texel_w];
		for (int r = r0; r < r1; ++r) {
			// Scale 1 is the default path and stays one texel per cell, cell for cell.
			if (area.scale == 1)
				build_row(grid, palette, area, r, rect.x, rect.x + rect.w, out);
			else
				build_texel_row(grid, palette, area, r, rect.x, rect.x + rect.w, out, sum);
		}
	};
	// A single band is a small region: one pool handshake costs more than the rows themselves.
	if (pool && bands > 1)
		pool->parallel_for(bands, work);
	else
		for (int band = 0; band < bands; ++band) work(band);
}

void build_light_inputs(const WorldGrid& grid, const LightPalette& palette,
                        const LightArea& area, LightTexel* out, JobPool* pool) {
	if (area.w <= 0 || area.h <= 0 || area.scale < 1) return;
	build_light_rects(grid, palette, area,
	                  {LightRect{0, 0, light_texel_w(area), light_texel_h(area)}}, out, pool);
}

LightRect light_region_rect(const LightArea& area, int cx0, int cy0, int cx1, int cy1) {
	LightRect r;
	if (area.w <= 0 || area.h <= 0 || area.scale < 1) return r;
	const int wx0 = std::max(cx0, area.x0);
	const int wy0 = std::max(cy0, area.y0);
	const int wx1 = std::min(cx1, area.x0 + area.w);
	const int wy1 = std::min(cy1, area.y0 + area.h);
	if (wx0 >= wx1 || wy0 >= wy1) return r;
	const int scale = area.scale;
	// Outward to the blocks the rect touches: the block holding wx0 may start left of it (and
	// outside the rect's chunk at scale 3, where a chunk is not a whole number of blocks).
	const int tx0 = (wx0 - area.x0) / scale;
	const int ty0 = (wy0 - area.y0) / scale;
	const int tx1 = (wx1 - area.x0 + scale - 1) / scale;
	const int ty1 = (wy1 - area.y0 + scale - 1) / scale;
	r.x = tx0;
	r.y = ty0;
	r.w = tx1 - tx0;
	r.h = ty1 - ty0;
	return r;
}

void build_light_region(const WorldGrid& grid, const LightPalette& palette, const LightArea& area,
                        int cx0, int cy0, int cx1, int cy1, LightTexel* out, JobPool* pool) {
	const LightRect rect = light_region_rect(area, cx0, cy0, cx1, cy1);
	if (rect.w <= 0 || rect.h <= 0) return;
	build_light_rects(grid, palette, area, {rect}, out, pool);
}

// ponytail: radius -> source disc is a tuning knob, not physics. In 2D radiance cascades a
// bigger source reaches farther; tuned at the Task 5 checkpoint against "a torch at the default
// radius (48) lights a cave room about 40 cells across". The 32 cap keeps radii above ~192
// meaningful instead of all collapsing onto one disc.
int spot_source_radius(float radius) {
	float r = radius / 6.0f;
	if (!(r >= 1.0f)) r = 1.0f;  // also catches NaN
	if (r > 32.0f) r = 32.0f;
	return (int)std::lround(r);
}

int light_surface_passes(int depth_cells, int scale) {
	if (scale < 1 || depth_cells <= 0) return 0;
	const int depth = std::min(depth_cells, LIGHT_SURFACE_MAX_DEPTH);
	return (depth + scale - 1) / scale;
}

float light_surface_falloff(int passes) {
	return passes <= 0 ? 1.0f : std::pow(LIGHT_SURFACE_FADE, 1.0f / (float)passes);
}

void add_light_spots(const LightArea& area, const std::vector<LightSpot>& spots, LightTexel* out) {
	if (area.w <= 0 || area.h <= 0 || area.scale < 1 || light_texel_w(area) <= 0) return;
	const int texel_w = light_texel_w(area);
	for (const LightSpot& s : spots) {
		SpotDisc disc;
		if (!spot_disc(area, s, disc)) continue;
		const LightTexel add{pack_emission(s.r * s.energy), pack_emission(s.g * s.energy),
		                     pack_emission(s.b * s.energy), 0};
		const float r2 = (float)(disc.sr * disc.sr);
		for (int ty = disc.rect.y; ty < disc.rect.y + disc.rect.h; ++ty) {
			const float dy = (float)ty - disc.y;
			LightTexel* row = out + (size_t)ty * texel_w;
			for (int tx = disc.rect.x; tx < disc.rect.x + disc.rect.w; ++tx) {
				const float dx = (float)tx - disc.x;
				if (dx * dx + dy * dy > r2) continue;
				LightTexel& t = row[tx];
				t.r = (uint8_t)std::min(255, t.r + add.r);
				t.g = (uint8_t)std::min(255, t.g + add.g);
				t.b = (uint8_t)std::min(255, t.b + add.b);
			}
		}
	}
}

std::vector<LightRect> spot_rects(const LightArea& area, const std::vector<LightSpot>& spots) {
	std::vector<LightRect> out;
	if (area.w <= 0 || area.h <= 0 || area.scale < 1 || light_texel_w(area) <= 0) return out;
	for (const LightSpot& s : spots) {
		SpotDisc disc;
		if (spot_disc(area, s, disc)) out.push_back(disc.rect);
	}
	return out;
}

void restore_light_rects(const LightArea& area, const std::vector<LightRect>& rects,
                         const LightTexel* base, LightTexel* out) {
	if (area.w <= 0 || area.h <= 0 || area.scale < 1) return;
	const int texel_w = light_texel_w(area);
	const int texel_h = light_texel_h(area);
	for (const LightRect& r : rects) {
		const int x0 = std::max(r.x, 0), x1 = std::min(r.x + r.w, texel_w);
		const int y0 = std::max(r.y, 0), y1 = std::min(r.y + r.h, texel_h);
		if (x0 >= x1) continue;
		for (int ty = y0; ty < y1; ++ty)
			memcpy(out + (size_t)ty * texel_w + x0, base + (size_t)ty * texel_w + x0,
			       (size_t)(x1 - x0) * sizeof(LightTexel));
	}
}

void collect_marker_lights(const WorldGrid& grid, const LightArea& area,
                           const LightSpot& defaults, std::vector<LightSpot>& out) {
	if (area.w <= 0 || area.h <= 0) return;
	const ChunkCoord c0 = chunk_of(area.x0, area.y0);
	const ChunkCoord c1 = chunk_of(area.x0 + area.w - 1, area.y0 + area.h - 1);
	for (int cy = c0.y; cy <= c1.y; ++cy)
		for (int cx = c0.x; cx <= c1.x; ++cx) {
			Chunk* chunk = grid.find({cx, cy});
			if (!chunk) continue;
			for (uint16_t marker : chunk->light_markers) {
				LightSpot s = defaults;
				s.x = (float)(cx * CHUNK + marker % CHUNK) + 0.5f;
				s.y = (float)(cy * CHUNK + marker / CHUNK) + 0.5f;
				out.push_back(s);
			}
		}
}
