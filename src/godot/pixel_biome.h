#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include "core/terrain.h"

namespace godot {

// Authoring-side mirror of BiomeDef. An empty *_material StringName means "none"; the core
// reads that as MAT_AIR. threshold fields are "amounts" in [0, 1]: 0 = never, larger = more.
class PixelBiome : public Resource {
	GDCLASS(PixelBiome, Resource)

public:
	// Values match CaveStyle; to_def() falls back to Classic for anything out of range.
	enum CaveStyleKind {
		CAVE_STYLE_CLASSIC = 0,
		CAVE_STYLE_SLABS = 1,
	};

private:
	// "name" is a Resource method, so the member carries the biome_ prefix (as PixelMaterial).
	StringName biome_name;
	int min_depth = 0;
	int max_depth = -1;  // -1 = open
	float weight = 1.0f;
	StringName surface_material;
	StringName base_material;
	StringName pocket_material;
	StringName liquid_material;
	int surface_depth = 0;
	StringName crust_material;  // "" = no crust
	int crust_depth = 0;
	float pocket_threshold = 0.0f;
	float tunnel_threshold = 0.0f;
	float cavern_threshold = 0.0f;
	float liquid_threshold = 0.0f;
	int cave_style = CAVE_STYLE_CLASSIC;
	float slab_air = 0.0f;
	float slab_holes = 0.0f;

protected:
	static void _bind_methods();

public:
	BiomeDef to_def() const;

	void set_biome_name(const StringName &p_value) { biome_name = p_value; }
	StringName get_biome_name() const { return biome_name; }

	void set_min_depth(int p_value) { min_depth = p_value; }
	int get_min_depth() const { return min_depth; }

	void set_max_depth(int p_value) { max_depth = p_value; }
	int get_max_depth() const { return max_depth; }

	void set_weight(float p_value) { weight = p_value; }
	float get_weight() const { return weight; }

	void set_surface_material(const StringName &p_value) { surface_material = p_value; }
	StringName get_surface_material() const { return surface_material; }

	void set_base_material(const StringName &p_value) { base_material = p_value; }
	StringName get_base_material() const { return base_material; }

	void set_pocket_material(const StringName &p_value) { pocket_material = p_value; }
	StringName get_pocket_material() const { return pocket_material; }

	void set_liquid_material(const StringName &p_value) { liquid_material = p_value; }
	StringName get_liquid_material() const { return liquid_material; }

	void set_surface_depth(int p_value) { surface_depth = p_value; }
	int get_surface_depth() const { return surface_depth; }

	void set_crust_material(const StringName &p_value) { crust_material = p_value; }
	StringName get_crust_material() const { return crust_material; }

	void set_crust_depth(int p_value) { crust_depth = p_value; }
	int get_crust_depth() const { return crust_depth; }

	void set_pocket_threshold(float p_value) { pocket_threshold = p_value; }
	float get_pocket_threshold() const { return pocket_threshold; }

	void set_tunnel_threshold(float p_value) { tunnel_threshold = p_value; }
	float get_tunnel_threshold() const { return tunnel_threshold; }

	void set_cavern_threshold(float p_value) { cavern_threshold = p_value; }
	float get_cavern_threshold() const { return cavern_threshold; }

	void set_liquid_threshold(float p_value) { liquid_threshold = p_value; }
	float get_liquid_threshold() const { return liquid_threshold; }

	void set_cave_style(int p_value) { cave_style = p_value; }
	int get_cave_style() const { return cave_style; }

	void set_slab_air(float p_value) { slab_air = p_value; }
	float get_slab_air() const { return slab_air; }

	void set_slab_holes(float p_value) { slab_holes = p_value; }
	float get_slab_holes() const { return slab_holes; }
};

} // namespace godot

VARIANT_ENUM_CAST(godot::PixelBiome::CaveStyleKind);
