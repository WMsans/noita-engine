#pragma once

#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include "core/physics.h"
#include "pixel_physics_node.h"

namespace godot {

// An upright creature: an axis-aligned hitbox that is a Box2D body for rigid bodies and other
// creatures, and walks the cells through the grid pass (stands on and climbs sand, steps up
// ledges, knocks sand aside, wades). Game code drives it like CharacterBody2D: read
// `velocity`, change it, write it back each physics frame. It draws nothing; add sprites as
// children. Units are cells and seconds.
class PixelCharacter : public PixelPhysicsNode {
	GDCLASS(PixelCharacter, PixelPhysicsNode)

	Vector2i hitbox_size = Vector2i(8, 16);
	int step_height = 3;
	float mass = 128.0f;
	float friction = 0.6f;
	float gravity_scale = 1.0f;

	const CreatureState &state() const;

protected:
	static void _bind_methods();
	int join(PixelWorld &p_world) override;

public:
	// For PixelWorld: the creature definition at the node's current position, in the world's space.
	CreatureDef build_def(const Transform2D &p_to_world) const;

	void set_hitbox_size(const Vector2i &p_value) { hitbox_size = p_value; }
	Vector2i get_hitbox_size() const { return hitbox_size; }
	void set_step_height(int p_value) { step_height = p_value; }
	int get_step_height() const { return step_height; }
	void set_mass(float p_value) { mass = p_value; }
	float get_mass() const { return mass; }
	void set_friction(float p_value) { friction = p_value; }
	float get_friction() const { return friction; }
	void set_gravity_scale(float p_value) { gravity_scale = p_value; }
	float get_gravity_scale() const { return gravity_scale; }

	void set_velocity(const Vector2 &p_value);
	Vector2 get_velocity() const;
	void apply_impulse(const Vector2 &p_impulse);
	void apply_force(const Vector2 &p_force);
	void teleport(const Vector2 &p_position);

	bool is_on_floor() const { return state().floor; }
	bool is_on_ceiling() const { return state().ceiling; }
	bool is_on_wall() const { return state().wall != 0; }
	int get_wall_side() const { return state().wall; }
	bool is_in_liquid() const { return state().submerged_fraction > 0.0f; }
	float get_submerged_fraction() const { return state().submerged_fraction; }
	StringName get_liquid_material() const;
};

} // namespace godot
