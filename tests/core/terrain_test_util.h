#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "chunk.h"
#include "coords.h"
#include "doctest/doctest.h"
#include "generator.h"
#include "material.h"
#include "terrain.h"

// Helpers shared by the terrain and room tests: one material set, small BiomeDef builders,
// a starter-like five-biome config, and a chunk cache so a test can read world cells
// without building a WorldGrid.
namespace terraintest {

// air(implicit) + the seven materials the world generator paints with. Classes and
// densities match the starter set in materials/. Dirt and grass are Plan 2 additions.
inline MaterialTable terrain_table() {
	std::vector<MaterialDef> defs(7);
	defs[0].name = "rock";
	defs[0].cls = MatClass::Solid;
	defs[0].density = 3.0f;
	defs[0].collision_solid = true;
	defs[1].name = "dirt";
	defs[1].cls = MatClass::Solid;
	defs[1].density = 2.5f;
	defs[1].collision_solid = true;
	defs[2].name = "sand";
	defs[2].cls = MatClass::Powder;
	defs[2].density = 1.6f;
	defs[3].name = "grass";
	defs[3].cls = MatClass::Solid;
	defs[3].density = 2.4f;
	defs[3].collision_solid = true;
	defs[4].name = "water";
	defs[4].cls = MatClass::Liquid;
	defs[4].density = 1.0f;
	defs[4].dispersion = 5;
	defs[5].name = "oil";
	defs[5].cls = MatClass::Liquid;
	defs[5].density = 0.8f;
	defs[5].dispersion = 4;
	defs[6].name = "lava";
	defs[6].cls = MatClass::Liquid;
	defs[6].density = 2.5f;
	defs[6].fixed_temp = true;
	defs[6].base_temp = 1200;
	defs[6].dispersion = 2;

	MaterialTable table;
	std::string error;
	REQUIRE(MaterialTable::compile(defs, {}, table, error));
	return table;
}

inline uint32_t test_seed() { return 0x7e44a1u; }

inline BiomeDef biome(std::string name, int min_depth, int max_depth) {
	BiomeDef b;
	b.name = std::move(name);
	b.min_depth = min_depth;
	b.max_depth = max_depth;
	return b;
}

inline TerrainConfig config(std::vector<BiomeDef> biomes) {
	TerrainConfig c;
	c.biomes = std::move(biomes);
	return c;
}

// Five biomes in four bands: forest and desert share the surface band, then underground,
// caverns and an open deep band. Every amount is non-zero, so caves, pockets and pools all
// show up in the cells it generates.
inline TerrainConfig starter_config() {
	BiomeDef forest = biome("forest", 0, 64);
	forest.surface_material = "grass";
	forest.base_material = "dirt";
	forest.surface_depth = 4;
	forest.pocket_material = "sand";
	forest.pocket_threshold = 0.15f;
	forest.tunnel_threshold = 0.10f;
	forest.cavern_threshold = 0.12f;
	forest.liquid_material = "water";
	forest.liquid_threshold = 0.15f;

	BiomeDef desert = biome("desert", 0, 64);
	desert.surface_material = "sand";
	desert.base_material = "sand";
	desert.surface_depth = 6;
	desert.pocket_material = "rock";
	desert.pocket_threshold = 0.2f;
	desert.tunnel_threshold = 0.08f;
	desert.cavern_threshold = 0.10f;
	desert.liquid_material = "oil";
	desert.liquid_threshold = 0.05f;

	BiomeDef underground = biome("underground", 64, 300);
	underground.base_material = "dirt";
	underground.pocket_material = "sand";
	underground.pocket_threshold = 0.25f;
	underground.tunnel_threshold = 0.12f;
	underground.cavern_threshold = 0.18f;
	underground.liquid_material = "water";
	underground.liquid_threshold = 0.12f;

	BiomeDef caverns = biome("caverns", 300, 900);
	caverns.base_material = "rock";
	caverns.pocket_material = "sand";
	caverns.pocket_threshold = 0.1f;
	caverns.tunnel_threshold = 0.16f;
	caverns.cavern_threshold = 0.4f;
	caverns.liquid_material = "water";
	caverns.liquid_threshold = 0.2f;

	BiomeDef deep = biome("deep", 900, -1);
	deep.base_material = "rock";
	deep.pocket_material = "rock";
	deep.pocket_threshold = 0.1f;
	deep.tunnel_threshold = 0.1f;
	deep.cavern_threshold = 0.3f;
	deep.liquid_material = "lava";
	deep.liquid_threshold = 0.3f;

	return config({forest, desert, underground, caverns, deep});
}

// One open-band rock biome on the slabs cave style, with the generator's core slab defaults.
inline TerrainConfig slabs_config() {
	BiomeDef slabby = biome("slabby", 0, -1);
	slabby.base_material = "rock";
	slabby.cave_style = CaveStyle::Slabs;
	slabby.slab_air = 0.55f;
	slabby.slab_holes = 0.1f;
	return config({slabby});
}

// Generates chunks on demand, so a test can address any world cell without a WorldGrid.
class ChunkCache {
public:
	ChunkCache(const TerrainGenerator& gen, uint32_t seed) : gen_(gen), seed_(seed) {}

	Chunk& chunk(ChunkCoord coord) {
		auto it = chunks_.find(chunk_key(coord));
		if (it == chunks_.end()) {
			it = chunks_.emplace(chunk_key(coord), make_chunk(coord, gen_, seed_)).first;
		}
		return *it->second;
	}

	MatId at(int wx, int wy) {
		const Chunk& c = chunk(chunk_of(wx, wy));
		return c.cells[(size_t)floor_mod(wy, CHUNK) * CHUNK + floor_mod(wx, CHUNK)].mat;
	}

private:
	const TerrainGenerator& gen_;
	const uint32_t seed_;
	std::map<int64_t, std::unique_ptr<Chunk>> chunks_;
};

}  // namespace terraintest
