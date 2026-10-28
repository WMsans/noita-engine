#pragma once

#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/variant/color.hpp>

namespace godot {

// A point light. PixelWorld collects every node in the "pixel_light" group each frame and adds
// it to the light inputs as a small emissive disc; nothing else lives on this node.
class PixelLight : public Node2D {
	GDCLASS(PixelLight, Node2D)

	Color color = Color(1.0f, 0.75f, 0.4f);
	float energy = 1.5f;
	float radius = 48.0f;
	float flicker = 0.0f;  // 0 = steady, 1 = full swing down to dark
	bool enabled = true;

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	// energy * (1 - flicker * n(time)), n = 8 Hz value noise, so flicker is smooth. Not bound.
	float current_energy(double p_time) const;

	void set_color(const Color &p_value) { color = p_value; }
	Color get_color() const { return color; }

	void set_energy(float p_value) { energy = p_value; }
	float get_energy() const { return energy; }

	void set_radius(float p_value) { radius = p_value; }
	float get_radius() const { return radius; }

	void set_flicker(float p_value) { flicker = CLAMP(p_value, 0.0f, 1.0f); }
	float get_flicker() const { return flicker; }

	void set_enabled(bool p_value) { enabled = p_value; }
	bool get_enabled() const { return enabled; }
};

} // namespace godot
