#pragma once

#include <cstdint>
#include <memory>

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include "core/material.h"
#include "core/terrain.h"
#include "pixel_biome.h"
#include "pixel_material_set.h"
#include "pixel_room.h"
#include "pixel_room_palette.h"

namespace godot {

// A whole authored world: every numeric TerrainConfig field plus the biome, room and palette
// resources. build() compiles it into a TerrainGenerator; validate() is the GDScript face.
class PixelWorldGenerator : public Resource {
	GDCLASS(PixelWorldGenerator, Resource)

	int surface_y = 0;
	float surface_amplitude = 48.0f;
	float surface_frequency = 0.004f;
	int band_warp = 24;
	int region_width = 768;
	int region_warp = 96;
	int region_transition = 96;
	int band_transition = 32;
	float tunnel_frequency = 0.012f;
	float cave_stretch = 2.5f;
	float cavern_frequency = 0.006f;
	float pocket_frequency = 0.03f;
	float pool_frequency = 0.02f;
	float slab_frequency = 0.004f;
	float slab_stretch = 4.0f;
	float slab_warp = 24.0f;
	float slab_rough_frequency = 0.06f;
	float slab_rough = 0.12f;
	float slab_hole_frequency = 0.02f;
	float slab_seam = 1.2f;
	int cave_taper_depth = 40;
	float entrance_frequency = 0.002f;
	float entrance_threshold = 0.75f;  // > 1 disables entrances
	int room_cell_size = 256;
	float room_chance = 0.25f;
	float crust_chance = 0.3f;  // CrustPass: per-edge-cell probability
	int crust_radius = 6;  // blob radius in cells
	// BackWallConfig, field for field.
	bool back_wall_enabled = true;
	float back_wall_darken = 0.54f;
	float back_wall_desaturate = 0.5f;
	int back_wall_edge_rough = 4;
	float back_wall_hollow_frequency = 0.008f;
	float back_wall_hollow_stretch = 4.0f;
	float back_wall_hollow_amount = 0.38f;
	float back_wall_hollow_darken = 0.3f;
	int back_wall_stalactite_length = 20;
	int back_wall_stalagmite_length = 8;
	int back_wall_tooth_period = 7;
	float back_wall_crack_frequency = 0.04f;
	float back_wall_crack_width = 0.04f;
	float back_wall_crack_darken = 0.75f;
	float back_wall_pebble_amount = 0.35f;
	float back_wall_shade_amount = 0.06f;
	TypedArray<PixelBiome> biomes;
	TypedArray<PixelRoom> rooms;
	Ref<PixelRoomPalette> room_palette;

	bool to_config(TerrainConfig &out, String &error) const;

protected:
	static void _bind_methods();

public:
	// nullptr + `error` on any material or terrain compile problem; the message is core's.
	std::unique_ptr<TerrainGenerator> build(const MaterialTable &table, uint32_t seed,
	                                        String &error) const;
	// "" when the generator compiles, otherwise the material-set or compile error verbatim.
	String validate(const Ref<PixelMaterialSet> &material_set) const;

	void set_surface_y(int p_value) { surface_y = p_value; }
	int get_surface_y() const { return surface_y; }

	void set_surface_amplitude(float p_value) { surface_amplitude = p_value; }
	float get_surface_amplitude() const { return surface_amplitude; }

	void set_surface_frequency(float p_value) { surface_frequency = p_value; }
	float get_surface_frequency() const { return surface_frequency; }

	void set_band_warp(int p_value) { band_warp = p_value; }
	int get_band_warp() const { return band_warp; }

	void set_region_width(int p_value) { region_width = p_value; }
	int get_region_width() const { return region_width; }

	void set_region_warp(int p_value) { region_warp = p_value; }
	int get_region_warp() const { return region_warp; }

	void set_region_transition(int p_value) { region_transition = p_value; }
	int get_region_transition() const { return region_transition; }

	void set_band_transition(int p_value) { band_transition = p_value; }
	int get_band_transition() const { return band_transition; }

	void set_tunnel_frequency(float p_value) { tunnel_frequency = p_value; }
	float get_tunnel_frequency() const { return tunnel_frequency; }

	void set_cave_stretch(float p_value) { cave_stretch = p_value; }
	float get_cave_stretch() const { return cave_stretch; }

	void set_cavern_frequency(float p_value) { cavern_frequency = p_value; }
	float get_cavern_frequency() const { return cavern_frequency; }

	void set_pocket_frequency(float p_value) { pocket_frequency = p_value; }
	float get_pocket_frequency() const { return pocket_frequency; }

	void set_pool_frequency(float p_value) { pool_frequency = p_value; }
	float get_pool_frequency() const { return pool_frequency; }

	void set_slab_frequency(float p_value) { slab_frequency = p_value; }
	float get_slab_frequency() const { return slab_frequency; }

	void set_slab_stretch(float p_value) { slab_stretch = p_value; }
	float get_slab_stretch() const { return slab_stretch; }

	void set_slab_warp(float p_value) { slab_warp = p_value; }
	float get_slab_warp() const { return slab_warp; }

	void set_slab_rough_frequency(float p_value) { slab_rough_frequency = p_value; }
	float get_slab_rough_frequency() const { return slab_rough_frequency; }

	void set_slab_rough(float p_value) { slab_rough = p_value; }
	float get_slab_rough() const { return slab_rough; }

	void set_slab_hole_frequency(float p_value) { slab_hole_frequency = p_value; }
	float get_slab_hole_frequency() const { return slab_hole_frequency; }

	void set_slab_seam(float p_value) { slab_seam = p_value; }
	float get_slab_seam() const { return slab_seam; }

	void set_cave_taper_depth(int p_value) { cave_taper_depth = p_value; }
	int get_cave_taper_depth() const { return cave_taper_depth; }

	void set_entrance_frequency(float p_value) { entrance_frequency = p_value; }
	float get_entrance_frequency() const { return entrance_frequency; }

	void set_entrance_threshold(float p_value) { entrance_threshold = p_value; }
	float get_entrance_threshold() const { return entrance_threshold; }

	void set_room_cell_size(int p_value) { room_cell_size = p_value; }
	int get_room_cell_size() const { return room_cell_size; }

	void set_room_chance(float p_value) { room_chance = p_value; }
	float get_room_chance() const { return room_chance; }

	void set_crust_chance(float p_value) { crust_chance = p_value; }
	float get_crust_chance() const { return crust_chance; }

	void set_crust_radius(int p_value) { crust_radius = p_value; }
	int get_crust_radius() const { return crust_radius; }

	void set_back_wall_enabled(bool p_value) { back_wall_enabled = p_value; }
	bool get_back_wall_enabled() const { return back_wall_enabled; }

	void set_back_wall_darken(float p_value) { back_wall_darken = p_value; }
	float get_back_wall_darken() const { return back_wall_darken; }

	void set_back_wall_desaturate(float p_value) { back_wall_desaturate = p_value; }
	float get_back_wall_desaturate() const { return back_wall_desaturate; }

	void set_back_wall_edge_rough(int p_value) { back_wall_edge_rough = p_value; }
	int get_back_wall_edge_rough() const { return back_wall_edge_rough; }

	void set_back_wall_hollow_frequency(float p_value) { back_wall_hollow_frequency = p_value; }
	float get_back_wall_hollow_frequency() const { return back_wall_hollow_frequency; }

	void set_back_wall_hollow_stretch(float p_value) { back_wall_hollow_stretch = p_value; }
	float get_back_wall_hollow_stretch() const { return back_wall_hollow_stretch; }

	void set_back_wall_hollow_amount(float p_value) { back_wall_hollow_amount = p_value; }
	float get_back_wall_hollow_amount() const { return back_wall_hollow_amount; }

	void set_back_wall_hollow_darken(float p_value) { back_wall_hollow_darken = p_value; }
	float get_back_wall_hollow_darken() const { return back_wall_hollow_darken; }

	void set_back_wall_stalactite_length(int p_value) { back_wall_stalactite_length = p_value; }
	int get_back_wall_stalactite_length() const { return back_wall_stalactite_length; }

	void set_back_wall_stalagmite_length(int p_value) { back_wall_stalagmite_length = p_value; }
	int get_back_wall_stalagmite_length() const { return back_wall_stalagmite_length; }

	void set_back_wall_tooth_period(int p_value) { back_wall_tooth_period = p_value; }
	int get_back_wall_tooth_period() const { return back_wall_tooth_period; }

	void set_back_wall_crack_frequency(float p_value) { back_wall_crack_frequency = p_value; }
	float get_back_wall_crack_frequency() const { return back_wall_crack_frequency; }

	void set_back_wall_crack_width(float p_value) { back_wall_crack_width = p_value; }
	float get_back_wall_crack_width() const { return back_wall_crack_width; }

	void set_back_wall_crack_darken(float p_value) { back_wall_crack_darken = p_value; }
	float get_back_wall_crack_darken() const { return back_wall_crack_darken; }

	void set_back_wall_pebble_amount(float p_value) { back_wall_pebble_amount = p_value; }
	float get_back_wall_pebble_amount() const { return back_wall_pebble_amount; }

	void set_back_wall_shade_amount(float p_value) { back_wall_shade_amount = p_value; }
	float get_back_wall_shade_amount() const { return back_wall_shade_amount; }

	void set_biomes(const TypedArray<PixelBiome> &p_value) { biomes = p_value; }
	TypedArray<PixelBiome> get_biomes() const { return biomes; }

	void set_rooms(const TypedArray<PixelRoom> &p_value) { rooms = p_value; }
	TypedArray<PixelRoom> get_rooms() const { return rooms; }

	void set_room_palette(const Ref<PixelRoomPalette> &p_value) { room_palette = p_value; }
	Ref<PixelRoomPalette> get_room_palette() const { return room_palette; }
};

} // namespace godot
