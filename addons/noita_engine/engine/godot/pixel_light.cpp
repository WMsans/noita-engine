#include "pixel_light.h"

#include <cmath>
#include <cstdint>

#include <godot_cpp/core/class_db.hpp>

#include "core/rng.h"

namespace godot {

namespace {

constexpr double FLICKER_HZ = 8.0;
constexpr uint32_t SALT_FLICKER = 203;

} // namespace

void PixelLight::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_color", "value"), &PixelLight::set_color);
	ClassDB::bind_method(D_METHOD("get_color"), &PixelLight::get_color);
	ClassDB::bind_method(D_METHOD("set_energy", "value"), &PixelLight::set_energy);
	ClassDB::bind_method(D_METHOD("get_energy"), &PixelLight::get_energy);
	ClassDB::bind_method(D_METHOD("set_radius", "value"), &PixelLight::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &PixelLight::get_radius);
	ClassDB::bind_method(D_METHOD("set_flicker", "value"), &PixelLight::set_flicker);
	ClassDB::bind_method(D_METHOD("get_flicker"), &PixelLight::get_flicker);
	ClassDB::bind_method(D_METHOD("set_enabled", "value"), &PixelLight::set_enabled);
	ClassDB::bind_method(D_METHOD("get_enabled"), &PixelLight::get_enabled);

	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "color"), "set_color", "get_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "energy", PROPERTY_HINT_RANGE, "0,16,0.01,or_greater"), "set_energy", "get_energy");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "1,1024,0.5,or_greater"), "set_radius", "get_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "flicker", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_flicker", "get_flicker");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "get_enabled");
}

void PixelLight::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE) add_to_group("pixel_light");
}

float PixelLight::current_energy(double p_time) const {
	if (flicker <= 0.0f) return energy;
	const double ticks = p_time * FLICKER_HZ;
	const double base = std::floor(ticks);
	const uint32_t seed = (uint32_t)get_instance_id();
	const uint32_t frame = (uint32_t)(int64_t)base;
	const float a = hash_unit(hash5(seed, frame, 0, 0, SALT_FLICKER));
	const float b = hash_unit(hash5(seed, frame + 1, 0, 0, SALT_FLICKER));
	return energy * (1.0f - flicker * Math::lerp(a, b, (float)(ticks - base)));
}

} // namespace godot
