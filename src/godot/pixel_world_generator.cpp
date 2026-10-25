#include "pixel_world_generator.h"

#include <string>
#include <utility>

#include <godot_cpp/core/class_db.hpp>

namespace godot {

void PixelWorldGenerator::_bind_methods() {
	ClassDB::bind_method(D_METHOD("validate", "material_set"), &PixelWorldGenerator::validate);

	ClassDB::bind_method(D_METHOD("set_surface_y", "value"), &PixelWorldGenerator::set_surface_y);
	ClassDB::bind_method(D_METHOD("get_surface_y"), &PixelWorldGenerator::get_surface_y);
	ClassDB::bind_method(D_METHOD("set_surface_amplitude", "value"), &PixelWorldGenerator::set_surface_amplitude);
	ClassDB::bind_method(D_METHOD("get_surface_amplitude"), &PixelWorldGenerator::get_surface_amplitude);
	ClassDB::bind_method(D_METHOD("set_surface_frequency", "value"), &PixelWorldGenerator::set_surface_frequency);
	ClassDB::bind_method(D_METHOD("get_surface_frequency"), &PixelWorldGenerator::get_surface_frequency);
	ClassDB::bind_method(D_METHOD("set_band_warp", "value"), &PixelWorldGenerator::set_band_warp);
	ClassDB::bind_method(D_METHOD("get_band_warp"), &PixelWorldGenerator::get_band_warp);
	ClassDB::bind_method(D_METHOD("set_region_width", "value"), &PixelWorldGenerator::set_region_width);
	ClassDB::bind_method(D_METHOD("get_region_width"), &PixelWorldGenerator::get_region_width);
	ClassDB::bind_method(D_METHOD("set_region_warp", "value"), &PixelWorldGenerator::set_region_warp);
	ClassDB::bind_method(D_METHOD("get_region_warp"), &PixelWorldGenerator::get_region_warp);
	ClassDB::bind_method(D_METHOD("set_region_transition", "value"), &PixelWorldGenerator::set_region_transition);
	ClassDB::bind_method(D_METHOD("get_region_transition"), &PixelWorldGenerator::get_region_transition);
	ClassDB::bind_method(D_METHOD("set_band_transition", "value"), &PixelWorldGenerator::set_band_transition);
	ClassDB::bind_method(D_METHOD("get_band_transition"), &PixelWorldGenerator::get_band_transition);
	ClassDB::bind_method(D_METHOD("set_tunnel_frequency", "value"), &PixelWorldGenerator::set_tunnel_frequency);
	ClassDB::bind_method(D_METHOD("get_tunnel_frequency"), &PixelWorldGenerator::get_tunnel_frequency);
	ClassDB::bind_method(D_METHOD("set_cave_stretch", "value"), &PixelWorldGenerator::set_cave_stretch);
	ClassDB::bind_method(D_METHOD("get_cave_stretch"), &PixelWorldGenerator::get_cave_stretch);
	ClassDB::bind_method(D_METHOD("set_cavern_frequency", "value"), &PixelWorldGenerator::set_cavern_frequency);
	ClassDB::bind_method(D_METHOD("get_cavern_frequency"), &PixelWorldGenerator::get_cavern_frequency);
	ClassDB::bind_method(D_METHOD("set_pocket_frequency", "value"), &PixelWorldGenerator::set_pocket_frequency);
	ClassDB::bind_method(D_METHOD("get_pocket_frequency"), &PixelWorldGenerator::get_pocket_frequency);
	ClassDB::bind_method(D_METHOD("set_pool_frequency", "value"), &PixelWorldGenerator::set_pool_frequency);
	ClassDB::bind_method(D_METHOD("get_pool_frequency"), &PixelWorldGenerator::get_pool_frequency);
	ClassDB::bind_method(D_METHOD("set_cave_taper_depth", "value"), &PixelWorldGenerator::set_cave_taper_depth);
	ClassDB::bind_method(D_METHOD("get_cave_taper_depth"), &PixelWorldGenerator::get_cave_taper_depth);
	ClassDB::bind_method(D_METHOD("set_entrance_frequency", "value"), &PixelWorldGenerator::set_entrance_frequency);
	ClassDB::bind_method(D_METHOD("get_entrance_frequency"), &PixelWorldGenerator::get_entrance_frequency);
	ClassDB::bind_method(D_METHOD("set_entrance_threshold", "value"), &PixelWorldGenerator::set_entrance_threshold);
	ClassDB::bind_method(D_METHOD("get_entrance_threshold"), &PixelWorldGenerator::get_entrance_threshold);
	ClassDB::bind_method(D_METHOD("set_room_cell_size", "value"), &PixelWorldGenerator::set_room_cell_size);
	ClassDB::bind_method(D_METHOD("get_room_cell_size"), &PixelWorldGenerator::get_room_cell_size);
	ClassDB::bind_method(D_METHOD("set_room_chance", "value"), &PixelWorldGenerator::set_room_chance);
	ClassDB::bind_method(D_METHOD("get_room_chance"), &PixelWorldGenerator::get_room_chance);
	ClassDB::bind_method(D_METHOD("set_biomes", "value"), &PixelWorldGenerator::set_biomes);
	ClassDB::bind_method(D_METHOD("get_biomes"), &PixelWorldGenerator::get_biomes);
	ClassDB::bind_method(D_METHOD("set_rooms", "value"), &PixelWorldGenerator::set_rooms);
	ClassDB::bind_method(D_METHOD("get_rooms"), &PixelWorldGenerator::get_rooms);
	ClassDB::bind_method(D_METHOD("set_room_palette", "value"), &PixelWorldGenerator::set_room_palette);
	ClassDB::bind_method(D_METHOD("get_room_palette"), &PixelWorldGenerator::get_room_palette);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "surface_y"), "set_surface_y", "get_surface_y");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "surface_amplitude"), "set_surface_amplitude", "get_surface_amplitude");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "surface_frequency"), "set_surface_frequency", "get_surface_frequency");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "band_warp"), "set_band_warp", "get_band_warp");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "region_width"), "set_region_width", "get_region_width");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "region_warp"), "set_region_warp", "get_region_warp");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "region_transition"), "set_region_transition", "get_region_transition");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "band_transition"), "set_band_transition", "get_band_transition");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tunnel_frequency"), "set_tunnel_frequency", "get_tunnel_frequency");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cave_stretch"), "set_cave_stretch", "get_cave_stretch");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cavern_frequency"), "set_cavern_frequency", "get_cavern_frequency");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "pocket_frequency"), "set_pocket_frequency", "get_pocket_frequency");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "pool_frequency"), "set_pool_frequency", "get_pool_frequency");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "cave_taper_depth"), "set_cave_taper_depth", "get_cave_taper_depth");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "entrance_frequency"), "set_entrance_frequency", "get_entrance_frequency");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "entrance_threshold"), "set_entrance_threshold", "get_entrance_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "room_cell_size"), "set_room_cell_size", "get_room_cell_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "room_chance"), "set_room_chance", "get_room_chance");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "biomes", PROPERTY_HINT_ARRAY_TYPE, "PixelBiome"), "set_biomes", "get_biomes");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "rooms", PROPERTY_HINT_ARRAY_TYPE, "PixelRoom"), "set_rooms", "get_rooms");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "room_palette", PROPERTY_HINT_RESOURCE_TYPE, "PixelRoomPalette"), "set_room_palette", "get_room_palette");
}

bool PixelWorldGenerator::to_config(TerrainConfig &out, String &error) const {
	TerrainConfig config;
	config.surface_y = surface_y;
	config.surface_amplitude = surface_amplitude;
	config.surface_frequency = surface_frequency;
	config.band_warp = band_warp;
	config.region_width = region_width;
	config.region_warp = region_warp;
	config.region_transition = region_transition;
	config.band_transition = band_transition;
	config.tunnel_frequency = tunnel_frequency;
	config.cave_stretch = cave_stretch;
	config.cavern_frequency = cavern_frequency;
	config.pocket_frequency = pocket_frequency;
	config.pool_frequency = pool_frequency;
	config.cave_taper_depth = cave_taper_depth;
	config.entrance_frequency = entrance_frequency;
	config.entrance_threshold = entrance_threshold;
	config.room_cell_size = room_cell_size;
	config.room_chance = room_chance;

	config.biomes.reserve((size_t)biomes.size());
	for (int i = 0; i < biomes.size(); ++i) {
		Ref<PixelBiome> biome = biomes[i];
		// A null slot becomes an unnamed def, so compile() reports it by name like any other.
		config.biomes.push_back(biome.is_valid() ? biome->to_def() : BiomeDef{});
	}
	if (room_palette.is_valid()) config.palette = room_palette->to_entries();

	config.rooms.reserve((size_t)rooms.size());
	for (int i = 0; i < rooms.size(); ++i) {
		Ref<PixelRoom> room = rooms[i];
		if (room.is_null()) {
			error = "room " + itos(i) + " has no image";
			return false;
		}
		RoomDef def;
		if (!room->to_def(i, def, error)) return false;
		config.rooms.push_back(std::move(def));
	}

	out = std::move(config);
	return true;
}

std::unique_ptr<TerrainGenerator> PixelWorldGenerator::build(const MaterialTable &p_table,
                                                             uint32_t p_seed,
                                                             String &r_error) const {
	TerrainConfig config;
	if (!to_config(config, r_error)) return nullptr;
	std::string compile_error;
	std::unique_ptr<TerrainGenerator> generator =
	    TerrainGenerator::compile(config, p_table, p_seed, compile_error);
	r_error = compile_error.c_str();
	return generator;
}

String PixelWorldGenerator::validate(const Ref<PixelMaterialSet> &p_material_set) const {
	if (!p_material_set.is_valid()) {
		return "world generator: material_set is not set";
	}
	MaterialTable table;
	String error;
	if (!p_material_set->build_table(table, error)) {
		return error;
	}
	build(table, 0, error);
	return error;
}

} // namespace godot
