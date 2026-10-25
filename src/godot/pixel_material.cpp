#include "pixel_material.h"

#include <cstdint>

#include <godot_cpp/core/class_db.hpp>

namespace godot {

namespace {

std::string to_std(const StringName &p_value) { return String(p_value).utf8().get_data(); }

int16_t to_int16(int p_value) {
	return (int16_t)(p_value < INT16_MIN ? INT16_MIN : p_value > INT16_MAX ? INT16_MAX : p_value);
}

uint8_t to_uint8(int p_value) { return (uint8_t)(p_value < 0 ? 0 : p_value > UINT8_MAX ? UINT8_MAX : p_value); }

// -1 is the GDScript-visible "never"/"none"; the core spells it NO_TEMP.
int16_t temp_or_none(int p_value) { return p_value < 0 ? NO_TEMP : to_int16(p_value); }

} // namespace

void PixelMaterial::_bind_methods() {
	BIND_ENUM_CONSTANT(CLASS_SOLID);
	BIND_ENUM_CONSTANT(CLASS_POWDER);
	BIND_ENUM_CONSTANT(CLASS_LIQUID);
	BIND_ENUM_CONSTANT(CLASS_GAS);

	ClassDB::bind_method(D_METHOD("set_material_name", "value"), &PixelMaterial::set_material_name);
	ClassDB::bind_method(D_METHOD("get_material_name"), &PixelMaterial::get_material_name);
	ClassDB::bind_method(D_METHOD("set_material_class", "value"), &PixelMaterial::set_material_class);
	ClassDB::bind_method(D_METHOD("get_material_class"), &PixelMaterial::get_material_class);
	ClassDB::bind_method(D_METHOD("set_density", "value"), &PixelMaterial::set_density);
	ClassDB::bind_method(D_METHOD("get_density"), &PixelMaterial::get_density);
	ClassDB::bind_method(D_METHOD("set_color", "value"), &PixelMaterial::set_color);
	ClassDB::bind_method(D_METHOD("get_color"), &PixelMaterial::get_color);
	ClassDB::bind_method(D_METHOD("set_shade_amount", "value"), &PixelMaterial::set_shade_amount);
	ClassDB::bind_method(D_METHOD("get_shade_amount"), &PixelMaterial::get_shade_amount);
	ClassDB::bind_method(D_METHOD("set_dispersion", "value"), &PixelMaterial::set_dispersion);
	ClassDB::bind_method(D_METHOD("get_dispersion"), &PixelMaterial::get_dispersion);
	ClassDB::bind_method(D_METHOD("set_emission", "value"), &PixelMaterial::set_emission);
	ClassDB::bind_method(D_METHOD("get_emission"), &PixelMaterial::get_emission);
	ClassDB::bind_method(D_METHOD("set_emission_energy", "value"), &PixelMaterial::set_emission_energy);
	ClassDB::bind_method(D_METHOD("get_emission_energy"), &PixelMaterial::get_emission_energy);
	ClassDB::bind_method(D_METHOD("set_light_opacity", "value"), &PixelMaterial::set_light_opacity);
	ClassDB::bind_method(D_METHOD("get_light_opacity"), &PixelMaterial::get_light_opacity);
	ClassDB::bind_method(D_METHOD("set_collision_solid", "value"), &PixelMaterial::set_collision_solid);
	ClassDB::bind_method(D_METHOD("get_collision_solid"), &PixelMaterial::get_collision_solid);
	ClassDB::bind_method(D_METHOD("set_conductivity", "value"), &PixelMaterial::set_conductivity);
	ClassDB::bind_method(D_METHOD("get_conductivity"), &PixelMaterial::get_conductivity);
	ClassDB::bind_method(D_METHOD("set_fixed_temperature", "value"), &PixelMaterial::set_fixed_temperature);
	ClassDB::bind_method(D_METHOD("get_fixed_temperature"), &PixelMaterial::get_fixed_temperature);
	ClassDB::bind_method(D_METHOD("set_base_temperature", "value"), &PixelMaterial::set_base_temperature);
	ClassDB::bind_method(D_METHOD("get_base_temperature"), &PixelMaterial::get_base_temperature);
	ClassDB::bind_method(D_METHOD("set_ignite_temperature", "value"), &PixelMaterial::set_ignite_temperature);
	ClassDB::bind_method(D_METHOD("get_ignite_temperature"), &PixelMaterial::get_ignite_temperature);
	ClassDB::bind_method(D_METHOD("set_burn_life", "value"), &PixelMaterial::set_burn_life);
	ClassDB::bind_method(D_METHOD("get_burn_life"), &PixelMaterial::get_burn_life);
	ClassDB::bind_method(D_METHOD("set_burn_heat", "value"), &PixelMaterial::set_burn_heat);
	ClassDB::bind_method(D_METHOD("get_burn_heat"), &PixelMaterial::get_burn_heat);
	ClassDB::bind_method(D_METHOD("set_burns_into", "value"), &PixelMaterial::set_burns_into);
	ClassDB::bind_method(D_METHOD("get_burns_into"), &PixelMaterial::get_burns_into);
	ClassDB::bind_method(D_METHOD("set_high_temperature", "value"), &PixelMaterial::set_high_temperature);
	ClassDB::bind_method(D_METHOD("get_high_temperature"), &PixelMaterial::get_high_temperature);
	ClassDB::bind_method(D_METHOD("set_high_into", "value"), &PixelMaterial::set_high_into);
	ClassDB::bind_method(D_METHOD("get_high_into"), &PixelMaterial::get_high_into);
	ClassDB::bind_method(D_METHOD("set_low_temperature", "value"), &PixelMaterial::set_low_temperature);
	ClassDB::bind_method(D_METHOD("get_low_temperature"), &PixelMaterial::get_low_temperature);
	ClassDB::bind_method(D_METHOD("set_low_into", "value"), &PixelMaterial::set_low_into);
	ClassDB::bind_method(D_METHOD("get_low_into"), &PixelMaterial::get_low_into);
	ClassDB::bind_method(D_METHOD("set_lifetime", "value"), &PixelMaterial::set_lifetime);
	ClassDB::bind_method(D_METHOD("get_lifetime"), &PixelMaterial::get_lifetime);
	ClassDB::bind_method(D_METHOD("set_decays_into", "value"), &PixelMaterial::set_decays_into);
	ClassDB::bind_method(D_METHOD("get_decays_into"), &PixelMaterial::get_decays_into);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "name"), "set_material_name", "get_material_name");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "material_class", PROPERTY_HINT_ENUM, "Solid:1,Powder:2,Liquid:3,Gas:4"), "set_material_class", "get_material_class");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "density"), "set_density", "get_density");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "color"), "set_color", "get_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shade_amount"), "set_shade_amount", "get_shade_amount");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "dispersion"), "set_dispersion", "get_dispersion");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "emission"), "set_emission", "get_emission");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "emission_energy"), "set_emission_energy", "get_emission_energy");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "light_opacity"), "set_light_opacity", "get_light_opacity");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "collision_solid"), "set_collision_solid", "get_collision_solid");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "conductivity"), "set_conductivity", "get_conductivity");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "fixed_temperature"), "set_fixed_temperature", "get_fixed_temperature");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "base_temperature"), "set_base_temperature", "get_base_temperature");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "ignite_temperature"), "set_ignite_temperature", "get_ignite_temperature");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "burn_life"), "set_burn_life", "get_burn_life");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "burn_heat"), "set_burn_heat", "get_burn_heat");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "burns_into"), "set_burns_into", "get_burns_into");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "high_temperature"), "set_high_temperature", "get_high_temperature");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "high_into"), "set_high_into", "get_high_into");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "low_temperature"), "set_low_temperature", "get_low_temperature");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "low_into"), "set_low_into", "get_low_into");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "lifetime"), "set_lifetime", "get_lifetime");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "decays_into"), "set_decays_into", "get_decays_into");
}

MaterialDef PixelMaterial::to_def() const {
	MaterialDef d;
	d.name = to_std(material_name);
	d.cls = material_class >= CLASS_SOLID && material_class <= CLASS_GAS ? (MatClass)material_class : MatClass::Empty;
	d.density = density;
	d.color_rgba = color.to_rgba32();
	d.shade_amount = shade_amount;
	d.dispersion = to_uint8(dispersion);
	d.emission_r = emission.r;
	d.emission_g = emission.g;
	d.emission_b = emission.b;
	d.emission_energy = emission_energy;
	d.light_opacity = light_opacity;
	d.collision_solid = collision_solid;
	d.conductivity = conductivity;
	d.fixed_temp = fixed_temperature;
	d.base_temp = to_int16(base_temperature);
	d.ignite_temp = temp_or_none(ignite_temperature);
	d.burn_life = to_uint8(burn_life);
	d.burn_heat = to_int16(burn_heat);
	d.burns_into = to_std(burns_into);
	d.high_temp = temp_or_none(high_temperature);
	d.high_into = to_std(high_into);
	d.low_temp = temp_or_none(low_temperature);
	d.low_into = to_std(low_into);
	d.lifetime = to_uint8(lifetime);
	d.decays_into = to_std(decays_into);
	return d;
}

} // namespace godot
