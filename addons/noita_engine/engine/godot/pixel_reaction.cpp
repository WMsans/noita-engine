#include "pixel_reaction.h"

#include <cstdint>

#include <godot_cpp/core/class_db.hpp>

namespace godot {

namespace {

std::string to_std(const StringName &p_value) { return String(p_value).utf8().get_data(); }

int16_t to_int16(int p_value) {
	return (int16_t)(p_value < INT16_MIN ? INT16_MIN : p_value > INT16_MAX ? INT16_MAX : p_value);
}

} // namespace

void PixelReaction::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_a", "value"), &PixelReaction::set_a);
	ClassDB::bind_method(D_METHOD("get_a"), &PixelReaction::get_a);
	ClassDB::bind_method(D_METHOD("set_b", "value"), &PixelReaction::set_b);
	ClassDB::bind_method(D_METHOD("get_b"), &PixelReaction::get_b);
	ClassDB::bind_method(D_METHOD("set_a_into", "value"), &PixelReaction::set_a_into);
	ClassDB::bind_method(D_METHOD("get_a_into"), &PixelReaction::get_a_into);
	ClassDB::bind_method(D_METHOD("set_b_into", "value"), &PixelReaction::set_b_into);
	ClassDB::bind_method(D_METHOD("get_b_into"), &PixelReaction::get_b_into);
	ClassDB::bind_method(D_METHOD("set_probability", "value"), &PixelReaction::set_probability);
	ClassDB::bind_method(D_METHOD("get_probability"), &PixelReaction::get_probability);
	ClassDB::bind_method(D_METHOD("set_min_temperature", "value"), &PixelReaction::set_min_temperature);
	ClassDB::bind_method(D_METHOD("get_min_temperature"), &PixelReaction::get_min_temperature);
	ClassDB::bind_method(D_METHOD("set_max_temperature", "value"), &PixelReaction::set_max_temperature);
	ClassDB::bind_method(D_METHOD("get_max_temperature"), &PixelReaction::get_max_temperature);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "a"), "set_a", "get_a");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "b"), "set_b", "get_b");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "a_into"), "set_a_into", "get_a_into");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "b_into"), "set_b_into", "get_b_into");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "probability"), "set_probability", "get_probability");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "min_temperature"), "set_min_temperature", "get_min_temperature");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_temperature"), "set_max_temperature", "get_max_temperature");
}

ReactionDef PixelReaction::to_def() const {
	ReactionDef d;
	d.a = to_std(a);
	d.b = to_std(b);
	d.a_into = to_std(a_into);
	d.b_into = to_std(b_into);
	d.probability = probability;
	d.min_temp = to_int16(min_temperature);
	d.max_temp = to_int16(max_temperature);
	return d;
}

} // namespace godot
