#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include "core/material.h"

namespace godot {

// One authored reaction pair, mirroring ReactionDef. An empty *_into StringName means air.
class PixelReaction : public Resource {
	GDCLASS(PixelReaction, Resource)

	StringName a;
	StringName b;
	StringName a_into;
	StringName b_into;
	float probability = 0.0f;
	int min_temperature = -273;
	int max_temperature = 5000;

protected:
	static void _bind_methods();

public:
	ReactionDef to_def() const;

	void set_a(const StringName &p_a) { a = p_a; }
	StringName get_a() const { return a; }

	void set_b(const StringName &p_b) { b = p_b; }
	StringName get_b() const { return b; }

	void set_a_into(const StringName &p_a_into) { a_into = p_a_into; }
	StringName get_a_into() const { return a_into; }

	void set_b_into(const StringName &p_b_into) { b_into = p_b_into; }
	StringName get_b_into() const { return b_into; }

	void set_probability(float p_probability) { probability = p_probability; }
	float get_probability() const { return probability; }

	void set_min_temperature(int p_min_temperature) { min_temperature = p_min_temperature; }
	int get_min_temperature() const { return min_temperature; }

	void set_max_temperature(int p_max_temperature) { max_temperature = p_max_temperature; }
	int get_max_temperature() const { return max_temperature; }
};

} // namespace godot
