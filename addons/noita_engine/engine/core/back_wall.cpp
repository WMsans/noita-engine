#include "back_wall.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "rng.h"
#include "terrain.h"

namespace {

// Fixed salts: which hash stream each field draws its seed and its picks from. Changing one
// changes every world, so treat them as part of the format. 0x51..0x64 belong to terrain.cpp.
constexpr uint32_t SALT_ROUGH = 0x65u;
constexpr uint32_t SALT_HOLLOW = 0x66u;
constexpr uint32_t SALT_AMP = 0x67u;
constexpr uint32_t SALT_TRI_DOWN = 0x68u;
constexpr uint32_t SALT_TRI_UP = 0x69u;
constexpr uint32_t SALT_CRACK = 0x6Au;
constexpr uint32_t SALT_PEBBLE = 0x6Bu;
constexpr uint32_t SALT_SHADE = 0x6Cu;

// Feature sizes the config has no field for.
constexpr float ROUGH_FREQUENCY = 0.05f;   // the top edge wanders over ~20 cells
constexpr float AMP_FREQUENCY = 0.02f;     // teeth come in patches ~50 cells long
constexpr float PEBBLE_FREQUENCY = 0.12f;  // pebbles are ~8-cell blobs

// How far into the wall (in hollow-field depth) the pebble band reaches. The hollow field
// moves by roughly this much over two to four cells at the default frequencies.
constexpr double PEBBLE_BAND = 0.12;
constexpr double PEBBLE_BRIGHTEN = 1.15;  // pebbles catch a little more light than the wall
constexpr double TOOTH_SHARPNESS = 1.6;
constexpr double AMP_BIAS = 1.5;

// The hollow quantile table: hollow_value sampled on a grid to find the level that leaves
// `hollow_amount` of it above, the same idea as TerrainGenerator::slab_cut.
constexpr int QUANTILE_GRID = 96;
constexpr int QUANTILE_SPACING = 41;

FastNoiseLite field_noise(uint32_t seed, uint32_t salt, float frequency, int octaves,
                          FastNoiseLite::FractalType fractal) {
	FastNoiseLite n;
	n.SetSeed((int)hash5(seed, 0, 0, 0, salt));
	n.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
	n.SetFrequency(frequency);
	n.SetFractalType(fractal);
	n.SetFractalOctaves(octaves);
	return n;
}

int channel(uint32_t rgba, int shift) { return (int)((rgba >> shift) & 0xFFu); }

uint32_t pack(int r, int g, int b) {
	return ((uint32_t)std::clamp(r, 0, 255) << 24) | ((uint32_t)std::clamp(g, 0, 255) << 16) |
	       ((uint32_t)std::clamp(b, 0, 255) << 8) | 255u;
}

}  // namespace

BackWall::BackWall(const BackWallConfig& cfg, uint32_t seed)
    : cfg_(cfg),
      seed_(seed),
      n_rough_(field_noise(seed, SALT_ROUGH, ROUGH_FREQUENCY, 2, FastNoiseLite::FractalType_FBm)),
      n_hollow_(field_noise(seed, SALT_HOLLOW, cfg.hollow_frequency, 2,
                            FastNoiseLite::FractalType_FBm)),
      n_amp_(field_noise(seed, SALT_AMP, AMP_FREQUENCY, 2, FastNoiseLite::FractalType_FBm)),
      n_crack_(field_noise(seed, SALT_CRACK, cfg.crack_frequency, 1,
                           FastNoiseLite::FractalType_None)) {
	// Cellular noise read as one hashed level per pebble-sized cell: above the level that
	// leaves `pebble_amount` of them, a cell is a pebble.
	n_pebble_.SetSeed((int)hash5(seed, 0, 0, 0, SALT_PEBBLE));
	n_pebble_.SetNoiseType(FastNoiseLite::NoiseType_Cellular);
	n_pebble_.SetCellularReturnType(FastNoiseLite::CellularReturnType_CellValue);
	n_pebble_.SetFrequency(PEBBLE_FREQUENCY);

	if (cfg_.hollow_amount <= 0.0f) {
		hollow_cut_ = HUGE_VAL;
		return;
	}
	if (cfg_.hollow_amount >= 1.0f) {
		hollow_cut_ = -HUGE_VAL;
		return;
	}
	std::vector<float> samples;
	samples.reserve((size_t)QUANTILE_GRID * QUANTILE_GRID);
	const int start = -QUANTILE_GRID / 2 * QUANTILE_SPACING;
	for (int j = 0; j < QUANTILE_GRID; ++j)
		for (int i = 0; i < QUANTILE_GRID; ++i)
			samples.push_back((float)hollow_value(start + i * QUANTILE_SPACING,
			                                      start + j * QUANTILE_SPACING));
	std::sort(samples.begin(), samples.end());
	const size_t last = samples.size() - 1;
	const size_t at = (size_t)((1.0 - (double)cfg_.hollow_amount) * (double)last);
	hollow_cut_ = (double)samples[std::min(at, last)];
}

double BackWall::hollow_value(int wx, int wy) const {
	return (double)n_hollow_.GetNoise((double)wx, (double)wy * (double)cfg_.hollow_stretch);
}

double BackWall::tri(int wx, uint32_t salt) const {
	const int period = std::max(cfg_.tooth_period, 1);
	const int segment = floor_div(wx, period);
	const int local = floor_mod(wx, period);
	// The period wobbles by a couple of cells per segment, so teeth differ in width.
	const int p = std::max(1, period - 2 + (int)(hash5(seed_, 0, segment, 0, salt) % 5u));
	const double phase = std::fmod((double)local / (double)p, 1.0);
	return phase < 0.5 ? phase * 2.0 : (1.0 - phase) * 2.0;
}

double BackWall::amp(int wx) const {
	const double n = 0.5 * ((double)n_amp_.GetNoise((double)wx, 0.0) + 1.0);
	return std::pow(std::clamp(n, 0.0, 1.0), AMP_BIAS);
}

int BackWall::tooth_length(int reach, int wx, uint32_t tri_salt) const {
	if (reach <= 0) return 0;
	const double length =
	    (double)reach * std::pow(tri(wx, tri_salt), TOOTH_SHARPNESS) * amp(wx);
	return std::clamp((int)std::floor(length), 0, reach);
}

void BackWall::reach(int wx, int& down, int& up) const {
	down = tooth_length(cfg_.stalactite_length, wx, SALT_TRI_DOWN);
	up = tooth_length(cfg_.stalagmite_length, wx, SALT_TRI_UP);
}

void BackWall::column(const TerrainGenerator& gen, int wx, int row0, int count, int down, int up,
                      Column& out) const {
	out.row0 = row0;
	out.count = std::max(count, 0);
	const double edge = 0.5 * ((double)n_rough_.GetNoise((double)wx, 0.0) + 1.0);
	const double rough = (double)cfg_.edge_rough * std::clamp(edge, 0.0, 1.0);
	out.wall_top = gen.surface_height(wx) + (int)std::ceil(rough);
	out.down = down;
	out.up = up;
	out.raw_hollow.resize((size_t)out.count);
	out.near_hollow.resize((size_t)out.count);
	for (int i = 0; i < out.count; ++i) {
		const int wy = out.row0 + i;
		if (wy < out.wall_top) continue;
		const double value = hollow_value(wx, wy);
		out.raw_hollow[i] = value > hollow_cut_;
		out.near_hollow[i] = !out.raw_hollow[i] && value > hollow_cut_ - PEBBLE_BAND;
	}
}

// A raw hollow becomes wall when a wall row sits within the tooth reach above or below it:
// stalactites hang from a hollow's ceiling, stalagmites rise from its floor.
bool BackWall::teeth_row(const Column& c, int wy) const {
	const int i = wy - c.row0;
	for (int k = 1; k <= c.down; ++k) {
		const int j = i - k;
		if (j < 0) break;
		if (c.row0 + j >= c.wall_top && c.raw_hollow[(size_t)j] == 0) return true;
	}
	for (int k = 1; k <= c.up; ++k) {
		const int j = i + k;
		if (j >= c.count) break;
		if (c.row0 + j >= c.wall_top && c.raw_hollow[(size_t)j] == 0) return true;
	}
	return false;
}

uint32_t BackWall::colour(const TerrainGenerator& gen, const MaterialTable& table, const Column& c,
                          int wx, int wy, int biome) const {
	if (wy < c.wall_top) return 0u;  // sky: no wall at all
	const int i = wy - c.row0;
	if (i < 0 || i >= c.count) return 0u;
	if (biome < 0) return 0u;

	const uint32_t base = table.get(gen.biome_base(biome)).color_rgba;
	const double r = (double)channel(base, 24), g = (double)channel(base, 16),
	             b = (double)channel(base, 8);
	const double luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;
	const double desat = (double)cfg_.desaturate;
	const double darken = (double)cfg_.darken;
	const double shade =
	    1.0 + (double)cfg_.shade_amount * ((double)hash_unit(hash5(seed_, 0, wx, wy, SALT_SHADE)) -
	                                       0.5);
	double rgb[3] = {(r + (luma - r) * desat) * darken * shade,
	                 (g + (luma - g) * desat) * darken * shade,
	                 (b + (luma - b) * desat) * darken * shade};

	if (c.raw_hollow[(size_t)i] != 0 && !teeth_row(c, wy)) {
		for (double& v : rgb) v *= (double)cfg_.hollow_darken;
	} else {
		// Zero crossings of the crack field read as a net of thin seams.
		if (std::fabs((double)n_crack_.GetNoise((double)wx, (double)wy)) <
		    (double)cfg_.crack_width)
			for (double& v : rgb) v *= (double)cfg_.crack_darken;
		if (c.near_hollow[(size_t)i] != 0 &&
		    (double)n_pebble_.GetNoise((double)wx, (double)wy) >
		        1.0 - (double)cfg_.pebble_amount)
			for (double& v : rgb) v *= PEBBLE_BRIGHTEN;
	}
	return pack((int)std::lround(rgb[0]), (int)std::lround(rgb[1]), (int)std::lround(rgb[2]));
}

void BackWall::fill(const TerrainGenerator& gen, const MaterialTable& table, ChunkCoord coord,
                    const int* biomes, std::vector<uint32_t>& out) const {
	const int base_x = coord.x * CHUNK, base_y = coord.y * CHUNK;
	// One hollow field window per column covers the chunk plus the rows the teeth read.
	out.assign((size_t)CHUNK * CHUNK, 0u);
	Column c;  // one column's buffers, reused for all 64: no per-column allocation
	for (int lx = 0; lx < CHUNK; ++lx) {
		const int wx = base_x + lx;
		int down = 0, up = 0;
		reach(wx, down, up);
		// The window is this column's own reach, not the config's maximum: the hollow field is
		// 25 rows shorter per column on average and nothing outside it is ever read.
		column(gen, wx, base_y - down, CHUNK + down + up, down, up, c);
		for (int ly = 0; ly < CHUNK; ++ly)
			out[(size_t)ly * CHUNK + lx] =
			    colour(gen, table, c, wx, base_y + ly, biomes[(size_t)ly * CHUNK + lx]);
	}
}

uint32_t BackWall::colour_at(const TerrainGenerator& gen, const MaterialTable& table, int wx,
                             int wy) const {
	int down = 0, up = 0;
	reach(wx, down, up);
	Column c;
	column(gen, wx, wy - down, down + up + 1, down, up, c);
	return colour(gen, table, c, wx, wy, gen.biome_at(wx, wy));
}

bool BackWall::hollow_at(const TerrainGenerator& gen, int wx, int wy) const {
	int down = 0, up = 0;
	reach(wx, down, up);
	Column c;
	column(gen, wx, wy - down, down + up + 1, down, up, c);
	if (wy < c.wall_top) return false;
	const int i = wy - c.row0;
	return c.raw_hollow[(size_t)i] != 0 && !teeth_row(c, wy);
}
