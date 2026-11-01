#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

#include "FastNoiseLite/FastNoiseLite.h"
#include "coords.h"

class MaterialTable;
class TerrainGenerator;

// How the painted wall behind the underground looks. Defaults are the starting point for
// tuning by eye, not a settled look.
struct BackWallConfig {
	bool enabled = true;
	float darken = 0.54f;     // wall brightness against the biome's base material
	float desaturate = 0.5f;  // share of the way to grey
	int edge_rough = 4;       // cells the wall's top edge wanders below the surface
	float hollow_frequency = 0.008f;
	float hollow_stretch = 4.0f;   // hollows are wide horizontally and squat vertically
	float hollow_amount = 0.38f;   // share of wall cells that are hollow before the teeth
	float hollow_darken = 0.3f;    // hollow brightness against the wall
	int stalactite_length = 20;    // longest tooth hanging from a hollow's ceiling
	int stalagmite_length = 8;     // longest tooth rising from its floor
	int tooth_period = 7;          // cells between tooth peaks
	float crack_frequency = 0.04f;
	float crack_width = 0.04f;
	float crack_darken = 0.75f;
	float pebble_amount = 0.35f;  // share of pebble cells just inside a hollow's edge
	float shade_amount = 0.06f;   // per-cell brightness jitter
};

// Unpacks one wall pixel into the bytes a texture upload reads in order: R, G, B, A. Inverts
// the 0xRRGGBBAA packing fill() writes, so a straight copy of the words would come out swapped.
inline void back_wall_rgba8_bytes(uint32_t packed, uint8_t *out) {
	out[0] = (uint8_t)(packed >> 24);
	out[1] = (uint8_t)(packed >> 16);
	out[2] = (uint8_t)(packed >> 8);
	out[3] = (uint8_t)packed;
}

// The pixel write_chunk_image stores for one cell: the plain cell where it is opaque, the plain
// wall where it is transparent, and the source-over blend of the two in between, so a translucent
// cell hides nothing behind it. `cell_px` is in the chunk image's word order (memory bytes
// R, G, B, A, so alpha is its top byte); `wall_px` is fill()'s 0xRRGGBBAA packing. Returns the
// chunk image word order. A wall cell is opaque or absent by construction, so no wall leaves the
// cell untouched and a wall always makes the blend opaque. Allocation-free.
inline uint32_t back_wall_composite(uint32_t cell_px, uint32_t wall_px) {
	const uint32_t alpha = cell_px >> 24;
	if (alpha == 255u) return cell_px;
	// The wall in the chunk image word order, i.e. the packing byte-reversed.
	uint32_t wall = 0;
	for (unsigned i = 0; i < 4; ++i)
		wall |= ((wall_px >> (24u - 8u * i)) & 0xFFu) << (8u * i);
	if (alpha == 0u) return wall;
	if ((wall >> 24) == 0u) return cell_px;
	uint32_t out = 0xFF000000u;
	for (unsigned shift = 0; shift < 24; shift += 8) {
		const uint32_t cell = (cell_px >> shift) & 0xFFu, behind = (wall >> shift) & 0xFFu;
		out |= ((cell * alpha + behind * (255u - alpha) + 127u) / 255u) << shift;
	}
	return out;
}

// The back wall of one chunk: CHUNK x CHUNK RGBA8 packed 0xRRGGBBAA, alpha 0 where there is
// no wall (above the surface) and 255 everywhere below. Built from world coordinates and the
// seed alone, so neighbouring chunks agree at their shared edges.
class BackWall {
public:
	BackWall(const BackWallConfig&, uint32_t seed);

	// Fills `out` with the whole chunk. Byte-identical to colour_at at every cell. `biomes`
	// holds CHUNK x CHUNK biome indices in chunk row-major order: generate()'s own per-cell
	// pick, so the wall never re-derives it (that was over half of the wall's cost).
	void fill(const TerrainGenerator&, const MaterialTable&, ChunkCoord, const int* biomes,
	          std::vector<uint32_t>& out) const;
	// The slow per-cell reference fill is checked against.
	uint32_t colour_at(const TerrainGenerator&, const MaterialTable&, int wx, int wy) const;
	// Hollow after the teeth are applied; false where there is no wall at all.
	bool hollow_at(const TerrainGenerator&, int wx, int wy) const;

private:
	// One column's state over the rows [row0, row0 + count): where the wall starts, how far
	// its teeth reach, and the raw hollow field. fill resolves a whole chunk window at once,
	// colour_at the few rows one cell needs; both then decide identically.
	struct Column {
		int row0 = 0, count = 0;
		int wall_top = 0;  // first row with a wall
		int down = 0, up = 0;
		std::vector<uint8_t> raw_hollow;   // wall row whose hollow field is over the cut
		std::vector<uint8_t> near_hollow;  // wall row just under the cut: the pebble band
	};

	// Rows the teeth of the column at wx reach above (down) and below (up) a cell.
	void reach(int wx, int& down, int& up) const;
	// Fills `out` for the column at wx, from `row0` down `count` rows. fill() reuses one Column
	// across a chunk's 64 columns, so its two buffers are resized, not reallocated, per column.
	void column(const TerrainGenerator&, int wx, int row0, int count, int down, int up,
	            Column& out) const;
	bool teeth_row(const Column&, int wy) const;
	uint32_t colour(const TerrainGenerator&, const MaterialTable&, const Column&, int wx, int wy,
	                int biome) const;
	// Length of the tooth hanging (or rising) at wx, in rows.
	int tooth_length(int reach, int wx, uint32_t tri_salt) const;
	double tri(int wx, uint32_t salt) const;  // triangle wave in [0, 1]
	double amp(int wx) const;                 // 1-D noise in [0, 1], biased low
	double hollow_value(int wx, int wy) const;

	const BackWallConfig cfg_;
	const uint32_t seed_;
	double hollow_cut_ = 0.0;  // field level leaving cfg_.hollow_amount of it above
	FastNoiseLite n_rough_, n_hollow_, n_amp_, n_crack_, n_pebble_;
};
