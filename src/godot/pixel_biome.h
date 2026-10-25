#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include "core/terrain.h"

namespace godot {

// Authoring-side mirror of BiomeDef. An empty *_material StringName means "none"; the core
// reads that as MAT_AIR. threshold fields are "amounts" in [0, 1]: 0 = never, larger = more.
class PixelBiome : public Resource {
	GDCLASS(PixelBiome, Resource)

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
	float pocket_threshold = 0.0f;
	float tunnel_threshold = 0.0f;
	float cavern_threshold = 0.0f;
	float liquid_threshold = 0.0f;

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

	void set_pocket_threshold(float p_value) { pocket_threshold = p_value; }
	float get_pocket_threshold() const { return pocket_threshold; }

	void set_tunnel_threshold(float p_value) { tunnel_threshold = p_value; }
	float get_tunnel_threshold() const { return tunnel_threshold; }

	void set_cavern_threshold(float p_value) { cavern_threshold = p_value; }
	float get_cavern_threshold() const { return cavern_threshold; }

	void set_liquid_threshold(float p_value) { liquid_threshold = p_value; }
	float get_liquid_threshold() const { return liquid_threshold; }
};

} // namespace godot
