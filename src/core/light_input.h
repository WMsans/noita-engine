#pragma once
#include <cstdint>
#include <vector>

#include "material.h"

class JobPool;
class WorldGrid;

// Emission above this is clamped: the input texel is RGBA8, so 4.0 is the whole range.
constexpr float EMISSION_RANGE = 4.0f;
// Reach: how far in world cells an emitter's light carries before it fades to zero. It rides next
// to the texels as one byte of code, so 255 codes is 1020 cells; 0 means "not an emitter".
constexpr float LIGHT_REACH_UNIT = 4.0f;
uint8_t pack_reach(float reach_cells);  // ceil(reach / UNIT) in 1..255; non-finite or <= 0 -> 1
// Radiance cascades: cascade 0 is one probe per LIGHT_SNAP cells, each level up doubles it.
constexpr int LIGHT_CASCADES = 6;
constexpr int LIGHT_SNAP = 1 << (LIGHT_CASCADES - 1);
// Levels needed by the largest reach in texels: those whose interval starts at (4^L - 1) / 3 and
// is below max_reach contribute. At least 1, at most LIGHT_CASCADES.
int light_cascade_count(float max_reach_texels);

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
	std::vector<uint8_t> reach;    // index = MatId, 0 = not an emitter
	LightTexel fire;               // a unused
	uint8_t fire_reach = 0;
	static LightPalette build(const MaterialTable& table, float fire_r, float fire_g,
	                          float fire_b, float fire_energy, float emission_reach);
};

// out has light_texel_w * light_texel_h texels, row-major from (x0, y0): each covers a
// scale x scale block of cells, emission is the block max and opacity the block mean. Opacity is
// the mean over the cells the block actually covers, not over scale x scale, so a partial block at
// the area's right or bottom edge stays opaque instead of diluting an edge wall into a light leak.
// reach_out has one byte per texel, same layout: the block's reach max. pool may be null.
void build_light_inputs(const WorldGrid& grid, const LightPalette& palette,
                        const LightArea& area, LightTexel* out, uint8_t* reach_out,
                        JobPool* pool);

struct LightRect {
	int x = 0, y = 0, w = 0, h = 0;  // texels, area-relative
};

// Texels covering the world-cell rect [cx0, cx1) x [cy0, cy1), expanded outward to whole texel
// blocks and clipped to the area. Empty when no cell of the rect is inside the area.
LightRect light_region_rect(const LightArea& area, int cx0, int cy0, int cx1, int cy1);

// Rebuilds only the texels covering world rect [cx0, cx1) x [cy0, cy1), expanded outward to
// whole texel blocks and clipped to the area. Same bytes as a full build of the area.
void build_light_region(const WorldGrid& grid, const LightPalette& palette,
                        const LightArea& area, int cx0, int cy0, int cx1, int cy1,
                        LightTexel* out, uint8_t* reach_out, JobPool* pool);

// Rects that share no texel row: rects overlapping in y are unioned into their bounding box, so
// two pool workers never write the same row at once. The union stays byte-identical because every
// texel in it is rebuilt from the grid. Empty rects are dropped.
std::vector<LightRect> light_disjoint_rects(const std::vector<LightRect>& rects);

// Rebuilds the texels of every rect in one pool dispatch, so a frame that repaints many chunks
// pays one handshake instead of one per chunk. The rects are made row-disjoint first.
void build_light_rects(const WorldGrid& grid, const LightPalette& palette, const LightArea& area,
                       const std::vector<LightRect>& rects, LightTexel* out, uint8_t* reach_out,
                       JobPool* pool);

struct LightSpot {
	float x, y, r, g, b, energy, radius;
};

int spot_source_radius(float radius);  // clamp(lround(radius / 6), 1, 32)

// Solid surfaces: the light pass spreads light this many cells into solids, at most.
constexpr int LIGHT_SURFACE_MAX_DEPTH = 32;
// ponytail: one fade for every solid, tuned by eye; per-material depth would replace it.
// The light left after a full skin depth.
constexpr float LIGHT_SURFACE_FADE = 0.05f;
int light_surface_passes(int depth_cells, int scale);  // ceil(clamp(depth, 0, MAX) / scale)
float light_surface_falloff(int passes);  // per-step factor: FADE after `passes` steps; 1 at 0

// Adds colour·energy (packed) to rgb of every texel within the source radius of the spot,
// saturating at 255; opacity untouched. The spot's position and source radius are in world cells
// and are divided by the area's scale, so the disc lands in texel space. Clipped to the area;
// non-finite spots skipped. Every texel of the disc takes max(reach, pack_reach(radius)): the
// reach stays in cells at any scale, so the GPU divides by the area's scale.
void add_light_spots(const LightArea& area, const std::vector<LightSpot>& spots, LightTexel* out,
                     uint8_t* reach_out);

// The texels each spot's source disc can write, clipped to the area: the rects of a spot pass,
// so the next frame can restore them from a base with no spots in it.
std::vector<LightRect> spot_rects(const LightArea& area, const std::vector<LightSpot>& spots);

// Copies the texels and reach codes of each rect from `base` / `reach_base` into `out` /
// `reach_out`: undoes the spots a previous frame added.
void restore_light_rects(const LightArea& area, const std::vector<LightRect>& rects,
                         const LightTexel* base, const uint8_t* reach_base, LightTexel* out,
                         uint8_t* reach_out);

// Appends one spot per marker in every loaded chunk overlapping the area, at the marker
// cell's centre, with colour/energy/radius from `defaults`.
void collect_marker_lights(const WorldGrid& grid, const LightArea& area,
                           const LightSpot& defaults, std::vector<LightSpot>& out);
