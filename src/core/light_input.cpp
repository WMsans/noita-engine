#include "light_input.h"

#include <algorithm>
#include <cmath>

#include "cell.h"
#include "chunk.h"
#include "coords.h"
#include "job_pool.h"
#include "world_grid.h"

namespace {

// Rows per job: each row walks chunk by chunk, so one find() per 64 cells.
constexpr int LIGHT_ROW_BAND = 16;

uint8_t pack_emission(float e) {
	return (uint8_t)std::lround(std::min(std::max(e / EMISSION_RANGE, 0.0f), 1.0f) * 255.0f);
}

uint8_t pack_opacity(float a) {
	return (uint8_t)std::lround(std::min(std::max(a, 0.0f), 1.0f) * 255.0f);
}

void build_row(const WorldGrid& grid, const LightPalette& palette, const LightArea& area, int row,
               LightTexel* out) {
	const int wy = area.y0 + row;
	const int ly = floor_mod(wy, CHUNK);
	LightTexel* dst = out + (size_t)row * area.w;
	const int wx_end = area.x0 + area.w;
	for (int wx = area.x0; wx < wx_end;) {
		Chunk* chunk = grid.find(chunk_of(wx, wy));  // once per 64 cells of the row
		const int lx = floor_mod(wx, CHUNK);
		const int n = std::min(CHUNK - lx, wx_end - wx);
		if (chunk) {
			const Cell* src = &chunk->cells[(size_t)ly * CHUNK + lx];
			for (int i = 0; i < n; ++i) {
				const Cell& cell = src[i];
				LightTexel t = cell.mat < palette.mats.size() ? palette.mats[cell.mat]
				                                              : LightTexel{0, 0, 0, 0};
				if (cell.flags & FLAG_BURNING) {  // per-channel max of material and fire
					if (palette.fire.r > t.r) t.r = palette.fire.r;
					if (palette.fire.g > t.g) t.g = palette.fire.g;
					if (palette.fire.b > t.b) t.b = palette.fire.b;
				}
				dst[i] = t;
			}
		} else {
			const LightTexel dark{0, 0, 0, 255};  // unloaded chunks are opaque and dark
			for (int i = 0; i < n; ++i) dst[i] = dark;
		}
		dst += n;
		wx += n;
	}
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

void build_light_inputs(const WorldGrid& grid, const LightPalette& palette,
                        const LightArea& area, LightTexel* out, JobPool* pool) {
	if (area.w <= 0 || area.h <= 0) return;
	const int bands = (area.h + LIGHT_ROW_BAND - 1) / LIGHT_ROW_BAND;
	auto work = [&](int band) {
		const int r0 = band * LIGHT_ROW_BAND;
		const int r1 = std::min(r0 + LIGHT_ROW_BAND, area.h);
		for (int r = r0; r < r1; ++r) build_row(grid, palette, area, r, out);
	};
	if (pool)
		pool->parallel_for(bands, work);
	else
		for (int band = 0; band < bands; ++band) work(band);
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

void add_light_spots(const LightArea& area, const std::vector<LightSpot>& spots, LightTexel* out) {
	if (area.w <= 0 || area.h <= 0) return;
	const int right = area.x0 + area.w - 1;
	const int bottom = area.y0 + area.h - 1;
	for (const LightSpot& s : spots) {
		if (!std::isfinite(s.x) || !std::isfinite(s.y) || !std::isfinite(s.r) ||
		    !std::isfinite(s.g) || !std::isfinite(s.b) || !std::isfinite(s.energy))
			continue;
		const int sr = spot_source_radius(s.radius);
		// Clamp the disc into the area in floats before any int cast, so a huge coordinate
		// just falls out of range instead of overflowing.
		const float x_lo = std::max(s.x - (float)sr, (float)area.x0);
		const float x_hi = std::min(s.x + (float)sr, (float)right);
		const float y_lo = std::max(s.y - (float)sr, (float)area.y0);
		const float y_hi = std::min(s.y + (float)sr, (float)bottom);
		if (x_lo > x_hi || y_lo > y_hi) continue;  // wholly outside
		const LightTexel add{pack_emission(s.r * s.energy), pack_emission(s.g * s.energy),
		                     pack_emission(s.b * s.energy), 0};
		const float r2 = (float)(sr * sr);
		// Truncating the lower bound may start one texel early; the distance test drops it.
		for (int y = (int)y_lo; (float)y <= y_hi; ++y) {
			const float dy = (float)y - s.y;
			LightTexel* row = out + (size_t)(y - area.y0) * area.w;
			for (int x = (int)x_lo; (float)x <= x_hi; ++x) {
				const float dx = (float)x - s.x;
				if (dx * dx + dy * dy > r2) continue;
				LightTexel& t = row[x - area.x0];
				t.r = (uint8_t)std::min(255, t.r + add.r);
				t.g = (uint8_t)std::min(255, t.g + add.g);
				t.b = (uint8_t)std::min(255, t.b + add.b);
			}
		}
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
