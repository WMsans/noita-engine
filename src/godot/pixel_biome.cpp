#include "pixel_biome.h"

#include <godot_cpp/core/class_db.hpp>

namespace godot {

namespace {

std::string to_std(const StringName &p_value) { return String(p_value).utf8().get_data(); }

} // namespace

void PixelBiome::_bind_methods() {
	BIND_ENUM_CONSTANT(CAVE_STYLE_CLASSIC);
	BIND_ENUM_CONSTANT(CAVE_STYLE_SLABS);

	ClassDB::bind_method(D_METHOD("set_biome_name", "value"), &PixelBiome::set_biome_name);
	ClassDB::bind_method(D_METHOD("get_biome_name"), &PixelBiome::get_biome_name);
	ClassDB::bind_method(D_METHOD("set_min_depth", "value"), &PixelBiome::set_min_depth);
	ClassDB::bind_method(D_METHOD("get_min_depth"), &PixelBiome::get_min_depth);
	ClassDB::bind_method(D_METHOD("set_max_depth", "value"), &PixelBiome::set_max_depth);
	ClassDB::bind_method(D_METHOD("get_max_depth"), &PixelBiome::get_max_depth);
	ClassDB::bind_method(D_METHOD("set_weight", "value"), &PixelBiome::set_weight);
	ClassDB::bind_method(D_METHOD("get_weight"), &PixelBiome::get_weight);
	ClassDB::bind_method(D_METHOD("set_surface_material", "value"), &PixelBiome::set_surface_material);
	ClassDB::bind_method(D_METHOD("get_surface_material"), &PixelBiome::get_surface_material);
	ClassDB::bind_method(D_METHOD("set_base_material", "value"), &PixelBiome::set_base_material);
	ClassDB::bind_method(D_METHOD("get_base_material"), &PixelBiome::get_base_material);
	ClassDB::bind_method(D_METHOD("set_pocket_material", "value"), &PixelBiome::set_pocket_material);
	ClassDB::bind_method(D_METHOD("get_pocket_material"), &PixelBiome::get_pocket_material);
	ClassDB::bind_method(D_METHOD("set_liquid_material", "value"), &PixelBiome::set_liquid_material);
	ClassDB::bind_method(D_METHOD("get_liquid_material"), &PixelBiome::get_liquid_material);
	ClassDB::bind_method(D_METHOD("set_surface_depth", "value"), &PixelBiome::set_surface_depth);
	ClassDB::bind_method(D_METHOD("get_surface_depth"), &PixelBiome::get_surface_depth);
	ClassDB::bind_method(D_METHOD("set_crust_material", "value"), &PixelBiome::set_crust_material);
	ClassDB::bind_method(D_METHOD("get_crust_material"), &PixelBiome::get_crust_material);
	ClassDB::bind_method(D_METHOD("set_crust_depth", "value"), &PixelBiome::set_crust_depth);
	ClassDB::bind_method(D_METHOD("get_crust_depth"), &PixelBiome::get_crust_depth);
	ClassDB::bind_method(D_METHOD("set_pocket_threshold", "value"), &PixelBiome::set_pocket_threshold);
	ClassDB::bind_method(D_METHOD("get_pocket_threshold"), &PixelBiome::get_pocket_threshold);
	ClassDB::bind_method(D_METHOD("set_tunnel_threshold", "value"), &PixelBiome::set_tunnel_threshold);
	ClassDB::bind_method(D_METHOD("get_tunnel_threshold"), &PixelBiome::get_tunnel_threshold);
	ClassDB::bind_method(D_METHOD("set_cavern_threshold", "value"), &PixelBiome::set_cavern_threshold);
	ClassDB::bind_method(D_METHOD("get_cavern_threshold"), &PixelBiome::get_cavern_threshold);
	ClassDB::bind_method(D_METHOD("set_liquid_threshold", "value"), &PixelBiome::set_liquid_threshold);
	ClassDB::bind_method(D_METHOD("get_liquid_threshold"), &PixelBiome::get_liquid_threshold);
	ClassDB::bind_method(D_METHOD("set_cave_style", "value"), &PixelBiome::set_cave_style);
	ClassDB::bind_method(D_METHOD("get_cave_style"), &PixelBiome::get_cave_style);
	ClassDB::bind_method(D_METHOD("set_slab_air", "value"), &PixelBiome::set_slab_air);
	ClassDB::bind_method(D_METHOD("get_slab_air"), &PixelBiome::get_slab_air);
	ClassDB::bind_method(D_METHOD("set_slab_holes", "value"), &PixelBiome::set_slab_holes);
	ClassDB::bind_method(D_METHOD("get_slab_holes"), &PixelBiome::get_slab_holes);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "name"), "set_biome_name", "get_biome_name");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "min_depth"), "set_min_depth", "get_min_depth");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_depth"), "set_max_depth", "get_max_depth");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "weight"), "set_weight", "get_weight");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "surface_material"), "set_surface_material", "get_surface_material");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "base_material"), "set_base_material", "get_base_material");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "pocket_material"), "set_pocket_material", "get_pocket_material");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "liquid_material"), "set_liquid_material", "get_liquid_material");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "surface_depth"), "set_surface_depth", "get_surface_depth");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "crust_material"), "set_crust_material", "get_crust_material");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "crust_depth"), "set_crust_depth", "get_crust_depth");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "pocket_threshold"), "set_pocket_threshold", "get_pocket_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tunnel_threshold"), "set_tunnel_threshold", "get_tunnel_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cavern_threshold"), "set_cavern_threshold", "get_cavern_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "liquid_threshold"), "set_liquid_threshold", "get_liquid_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "cave_style", PROPERTY_HINT_ENUM, "Classic:0,Slabs:1"), "set_cave_style", "get_cave_style");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "slab_air"), "set_slab_air", "get_slab_air");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "slab_holes"), "set_slab_holes", "get_slab_holes");
}

BiomeDef PixelBiome::to_def() const {
	BiomeDef def;
	def.name = to_std(biome_name);
	def.min_depth = min_depth;
	def.max_depth = max_depth;
	def.weight = weight;
	def.surface_material = to_std(surface_material);
	def.base_material = to_std(base_material);
	def.pocket_material = to_std(pocket_material);
	def.liquid_material = to_std(liquid_material);
	def.surface_depth = surface_depth;
	def.crust_material = to_std(crust_material);
	def.crust_depth = crust_depth;
	def.pocket_threshold = pocket_threshold;
	def.tunnel_threshold = tunnel_threshold;
	def.cavern_threshold = cavern_threshold;
	def.liquid_threshold = liquid_threshold;
	def.cave_style = cave_style == CAVE_STYLE_SLABS ? CaveStyle::Slabs : CaveStyle::Classic;
	def.slab_air = slab_air;
	def.slab_holes = slab_holes;
	return def;
}

} // namespace godot
