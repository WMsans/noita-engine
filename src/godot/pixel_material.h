#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include "core/material.h"

namespace godot {

// Authoring-side mirror of MaterialDef. Values are stored exactly as the .tres files spell
// them: an *_temperature of -1 means "never" (ignite) or "none" (phase change), and an empty
// *_into StringName means air. to_def() maps both onto NO_TEMP and the empty name.
class PixelMaterial : public Resource {
	GDCLASS(PixelMaterial, Resource)

public:
	// The numeric values match MatClass, so to_def() casts without a lookup table.
	enum MaterialClass {
		CLASS_SOLID = (int)MatClass::Solid,
		CLASS_POWDER = (int)MatClass::Powder,
		CLASS_LIQUID = (int)MatClass::Liquid,
		CLASS_GAS = (int)MatClass::Gas,
	};

private:
	// "name" is a Resource method, so the accessors carry the material_ prefix.
	StringName material_name;
	int material_class = CLASS_SOLID;
	float density = 1.0f;
	Color color = Color(1, 1, 1, 1);
	float shade_amount = 0.1f;
	int dispersion = 0;
	Color emission = Color(0, 0, 0, 1);
	float emission_energy = 0.0f;
	float light_opacity = 1.0f;
	bool collision_solid = false;
	float conductivity = 0.1f;
	bool fixed_temperature = false;
	int base_temperature = AMBIENT_TEMP;
	int ignite_temperature = -1;
	int burn_life = 0;
	int burn_heat = 0;
	StringName burns_into;
	int high_temperature = -1;
	StringName high_into;
	int low_temperature = -1;
	StringName low_into;
	int lifetime = 0;
	StringName decays_into;

protected:
	static void _bind_methods();

public:
	MaterialDef to_def() const;

	void set_material_name(const StringName &p_name) { material_name = p_name; }
	StringName get_material_name() const { return material_name; }

	void set_material_class(int p_class) { material_class = p_class; }
	int get_material_class() const { return material_class; }

	void set_density(float p_density) { density = p_density; }
	float get_density() const { return density; }

	void set_color(const Color &p_color) { color = p_color; }
	Color get_color() const { return color; }

	void set_shade_amount(float p_shade_amount) { shade_amount = p_shade_amount; }
	float get_shade_amount() const { return shade_amount; }

	void set_dispersion(int p_dispersion) { dispersion = p_dispersion; }
	int get_dispersion() const { return dispersion; }

	void set_emission(const Color &p_emission) { emission = p_emission; }
	Color get_emission() const { return emission; }

	void set_emission_energy(float p_emission_energy) { emission_energy = p_emission_energy; }
	float get_emission_energy() const { return emission_energy; }

	void set_light_opacity(float p_light_opacity) { light_opacity = p_light_opacity; }
	float get_light_opacity() const { return light_opacity; }

	void set_collision_solid(bool p_collision_solid) { collision_solid = p_collision_solid; }
	bool get_collision_solid() const { return collision_solid; }

	void set_conductivity(float p_conductivity) { conductivity = p_conductivity; }
	float get_conductivity() const { return conductivity; }

	void set_fixed_temperature(bool p_fixed_temperature) { fixed_temperature = p_fixed_temperature; }
	bool get_fixed_temperature() const { return fixed_temperature; }

	void set_base_temperature(int p_base_temperature) { base_temperature = p_base_temperature; }
	int get_base_temperature() const { return base_temperature; }

	void set_ignite_temperature(int p_ignite_temperature) { ignite_temperature = p_ignite_temperature; }
	int get_ignite_temperature() const { return ignite_temperature; }

	void set_burn_life(int p_burn_life) { burn_life = p_burn_life; }
	int get_burn_life() const { return burn_life; }

	void set_burn_heat(int p_burn_heat) { burn_heat = p_burn_heat; }
	int get_burn_heat() const { return burn_heat; }

	void set_burns_into(const StringName &p_burns_into) { burns_into = p_burns_into; }
	StringName get_burns_into() const { return burns_into; }

	void set_high_temperature(int p_high_temperature) { high_temperature = p_high_temperature; }
	int get_high_temperature() const { return high_temperature; }

	void set_high_into(const StringName &p_high_into) { high_into = p_high_into; }
	StringName get_high_into() const { return high_into; }

	void set_low_temperature(int p_low_temperature) { low_temperature = p_low_temperature; }
	int get_low_temperature() const { return low_temperature; }

	void set_low_into(const StringName &p_low_into) { low_into = p_low_into; }
	StringName get_low_into() const { return low_into; }

	void set_lifetime(int p_lifetime) { lifetime = p_lifetime; }
	int get_lifetime() const { return lifetime; }

	void set_decays_into(const StringName &p_decays_into) { decays_into = p_decays_into; }
	StringName get_decays_into() const { return decays_into; }
};

} // namespace godot

VARIANT_ENUM_CAST(godot::PixelMaterial::MaterialClass);
