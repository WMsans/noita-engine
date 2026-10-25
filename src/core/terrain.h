#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "FastNoiseLite/FastNoiseLite.h"
#include "generator.h"
#include "material.h"

struct BiomeDef {
	std::string name;
	int min_depth = 0, max_depth = -1;  // [min, max) cells below the local surface; -1 = open
	float weight = 1.0f;                // pick weight among its band's biomes
	std::string surface_material, base_material, pocket_material,
	    liquid_material;  // "" = none; base required
	int surface_depth = 0;
	// "Amounts" in [0, 1]: 0 = never, larger = more. Blended by biome weight per cell.
	float pocket_threshold = 0, tunnel_threshold = 0, cavern_threshold = 0, liquid_threshold = 0;
};

enum class PaletteKind : uint8_t { Material, Keep, MarkerLight, MarkerBody };

struct PaletteEntry {
	uint32_t rgba;
	PaletteKind kind;
	std::string material;
};  // rgba = 0xRRGGBBAA

enum class RoomAnchor : uint8_t { Surface, Embedded, Cave };

struct RoomDef {
	std::string name;
	int width = 0, height = 0;
	std::vector<uint32_t> rgba;  // row-major 0xRRGGBBAA
	float weight = 1.0f;
	RoomAnchor anchor = RoomAnchor::Embedded;
	std::vector<std::string> biomes;  // empty = any
};

struct RoomPlacement {
	int room;
	int x, y;  // world top-left
};

struct TerrainConfig {
	int surface_y = 0;
	float surface_amplitude = 48, surface_frequency = 0.004f;
	int band_warp = 24, region_width = 768, region_warp = 96, region_transition = 96,
	    band_transition = 32;
	float tunnel_frequency = 0.012f, cave_stretch = 2.5f, cavern_frequency = 0.006f;
	float pocket_frequency = 0.03f, pool_frequency = 0.02f;
	int cave_taper_depth = 40;
	float entrance_frequency = 0.002f, entrance_threshold = 0.75f;  // > 1 disables entrances
	int room_cell_size = 256;
	float room_chance = 0.25f;
	std::vector<BiomeDef> biomes;
	std::vector<PaletteEntry> palette;
	std::vector<RoomDef> rooms;
};

struct BiomeWeights {
	int count = 0;
	int biome[4];
	float weight[4];
};  // indices into config.biomes; sum 1

class TerrainGenerator : public ChunkGenerator {
public:
	// nullptr + `error` naming the culprit on any config problem. `seed` seeds every noise
	// field (via hash5 with a fixed salt per field); generate() must get the same seed.
	static std::unique_ptr<TerrainGenerator> compile(const TerrainConfig&, const MaterialTable&,
	                                                uint32_t seed, std::string& error);
	void generate(ChunkCoord coord, Chunk& out, uint32_t seed) const override;
	int surface_height(int wx) const;               // h(wx): first ground row
	BiomeWeights weights_at(int wx, int wy) const;  // count 0 above the surface
	int biome_at(int wx, int wy) const;             // dithered pick; -1 above the surface
	bool cave_at(int wx, int wy) const;  // tunnel || cavern, thresholds blended, taper applied
	// The room the placement cell (px, py) holds, if any. Cells are room_cell_size square.
	std::optional<RoomPlacement> room_in_cell(int px, int py) const;

private:
	// Biomes grouped by an identical depth range, ordered by min_depth.
	struct Band {
		int min_depth = 0, max_depth = -1;
		std::vector<int> biomes;
	};
	struct BiomeIds {
		MatId surface = MAT_AIR, base = MAT_AIR, pocket = MAT_AIR, liquid = MAT_AIR;
	};
	// Per-cell thresholds: the weights blended "amounts" the fields are tested against.
	struct Blended {
		double tunnel = 0, cavern = 0, pocket = 0, liquid = 0, surface_depth = 0;
	};

	TerrainGenerator(const TerrainConfig& cfg, const MaterialTable& table, uint32_t seed)
	    : table_(table), seed_(seed), config_(cfg) {}

	double region_x(int wx) const;
	int band_index(double warped_depth) const;
	int pick_biome(int band, int region) const;
	// `xw` is the region-warped x, which generate() shares across a whole column.
	BiomeWeights weights_col(int wx, double xw, int wy, int depth) const;
	Blended blend(const BiomeWeights&) const;
	int pick_from(const BiomeWeights&, int wx, int wy) const;
	double taper_at(int wx, int depth) const;
	bool cave_for(int wx, int wy, int depth, const Blended&) const;

	const MaterialTable& table_;  // must outlive the generator
	const uint32_t seed_;
	const TerrainConfig config_;
	// Each room's pixels resolved through the palette: a MatId, or MAT_KEEP for keep.
	std::vector<std::vector<MatId>> room_pixels_;  // parallel to config_.rooms
	std::vector<Band> bands_;
	std::vector<int> region_offset_;  // per band, from a hash of the seed
	std::vector<BiomeIds> ids_;
	// One field per noise source, built once. GetNoise is const, so delegates may share them.
	FastNoiseLite n_surface_, n_band_, n_region_, n_tunnel_, n_cavern_, n_pocket_, n_pool_,
	    n_entrance_;
};
