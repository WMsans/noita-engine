#pragma once
#include <cstdint>
#include <vector>

#include "material.h"

class JobPool;
class WorldGrid;

// Emission above this is clamped: the input texel is RGBA8, so 4.0 is the whole range.
constexpr float EMISSION_RANGE = 4.0f;
// Radiance cascades: cascade 0 is one probe per LIGHT_SNAP cells, each level up doubles it.
constexpr int LIGHT_CASCADES = 6;
constexpr int LIGHT_SNAP = 1 << (LIGHT_CASCADES - 1);

struct LightTexel {
	uint8_t r, g, b, a;  // emission, a = opacity
};

struct LightArea {
	int x0 = 0, y0 = 0, w = 0, h = 0;  // world cells
	int scale = 1;                     // world cells per light texel
};

// Texels covering the area: one per scale x scale block of cells, rounded up.
int light_texel_w(const LightArea& area);
int light_texel_h(const LightArea& area);

// The visible rect grown by `margin` on every side, then outward to multiples of `snap`.
LightArea light_area(float vis_x, float vis_y, float vis_w, float vis_h, int margin, int snap);

// The lit area is view + margin, so a far zoom-out asks for an unbounded buffer. 1440p with the
// default margin is 3040x1920 (5.8M cells), so this cap covers it.
// shortcut: 8M cells (32 MB RGBA8 input) / 8192 per side; raise these if a game needs 4K or a
// wider default margin.
constexpr int64_t LIGHT_AREA_MAX_CELLS = 8 * 1024 * 1024;
constexpr int LIGHT_AREA_MAX_SIDE = 8192;
bool light_area_fits(const LightArea& area);

struct LightPalette {
	std::vector<LightTexel> mats;  // index = MatId
	LightTexel fire;               // a unused
	static LightPalette build(const MaterialTable& table, float fire_r, float fire_g,
	                          float fire_b, float fire_energy);
};

// out has light_texel_w * light_texel_h texels, row-major from (x0, y0): each covers a
// scale x scale block of cells, emission is the block max and opacity the block mean. pool may be
// null.
void build_light_inputs(const WorldGrid& grid, const LightPalette& palette,
                        const LightArea& area, LightTexel* out, JobPool* pool);

struct LightSpot {
	float x, y, r, g, b, energy, radius;
};

int spot_source_radius(float radius);  // clamp(lround(radius / 6), 1, 32)

// Adds colour·energy (packed) to rgb of every texel within the source radius of the spot,
// saturating at 255; opacity untouched. The spot's position and source radius are in world cells
// and are divided by the area's scale, so the disc lands in texel space. Clipped to the area;
// non-finite spots skipped.
void add_light_spots(const LightArea& area, const std::vector<LightSpot>& spots, LightTexel* out);

// Appends one spot per marker in every loaded chunk overlapping the area, at the marker
// cell's centre, with colour/energy/radius from `defaults`.
void collect_marker_lights(const WorldGrid& grid, const LightArea& area,
                           const LightSpot& defaults, std::vector<LightSpot>& out);
