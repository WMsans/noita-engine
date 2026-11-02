#pragma once

#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include "core/physics.h"
#include "pixel_physics_node.h"
#include "pixel_room_palette.h"

namespace godot {

// A non-breakable Box2D body made of material pixels: each opaque texture pixel is
// `pixel_material`, or the palette's material for its colour when `palette` is set. Without a
// texture it is a plain box of `body_size` instead, with its mass from `mass`. It joins its
// PixelWorld on its first physics frame and follows Box2D from then on. Velocities and forces
// are in cells and seconds.
class PixelRigidBody : public PixelPhysicsNode {
	GDCLASS(PixelRigidBody, PixelPhysicsNode)

public:
	enum Shape { SHAPE_HULL, SHAPE_CIRCLE, SHAPE_BOXES };

private:
	Ref<Texture2D> texture;
	StringName pixel_material;
	Ref<PixelRoomPalette> palette;
	int shape = SHAPE_HULL;
	float friction = 0.6f;
	float restitution = 0.1f;
	Vector2i body_size;
	BodyProps props;

	void apply_props();  // live: hands `props` to the joined body
	bool build_pixel_def(const MaterialTable &p_table, BodyDef &r_def, String &r_error) const;

protected:
	static void _bind_methods();
	int join(PixelWorld &p_world) override;

public:
	void _draw() override;

	// For PixelWorld: the body definition at the node's current pose, in the world's space.
	bool build_def(const MaterialTable &p_table, const Transform2D &p_to_world, BodyDef &r_def, String &r_error) const;

	void set_texture(const Ref<Texture2D> &p_value);
	Ref<Texture2D> get_texture() const { return texture; }
	void set_pixel_material(const StringName &p_value) { pixel_material = p_value; }
	StringName get_pixel_material() const { return pixel_material; }
	void set_palette(const Ref<PixelRoomPalette> &p_value) { palette = p_value; }
	Ref<PixelRoomPalette> get_palette() const { return palette; }
	void set_shape(int p_value) { shape = p_value; }
	int get_shape() const { return shape; }
	void set_friction(float p_value) { friction = p_value; }
	float get_friction() const { return friction; }
	void set_restitution(float p_value) { restitution = p_value; }
	float get_restitution() const { return restitution; }

	// The shape when there is no texture: an unstamped box, which is how a character body is made.
	void set_body_size(const Vector2i &p_value);
	Vector2i get_body_size() const { return body_size; }
	void set_mass(float p_value);
	float get_body_mass() const { return props.mass; }
	void set_gravity_scale(float p_value) {
		props.gravity_scale = p_value;
		apply_props();
	}
	float get_gravity_scale() const { return props.gravity_scale; }
	void set_linear_damping(float p_value) {
		props.linear_damping = p_value;
		apply_props();
	}
	float get_linear_damping() const { return props.linear_damping; }
	void set_angular_damping(float p_value) {
		props.angular_damping = p_value;
		apply_props();
	}
	float get_angular_damping() const { return props.angular_damping; }
	void set_lock_rotation(bool p_value) {
		props.lock_rotation = p_value;
		apply_props();
	}
	bool get_lock_rotation() const { return props.lock_rotation; }
	void set_terrain_collision(bool p_value) {
		props.terrain_collision = p_value;
		apply_props();
	}
	bool get_terrain_collision() const { return props.terrain_collision; }
	void set_cell_proxies(bool p_value) {
		props.cell_proxies = p_value;
		apply_props();
	}
	bool get_cell_proxies() const { return props.cell_proxies; }
	void set_proxy_step_height(int p_value) {
		props.proxy_step_height = p_value;
		apply_props();
	}
	int get_proxy_step_height() const { return props.proxy_step_height; }
	void set_can_sleep(bool p_value) {
		props.can_sleep = p_value;
		apply_props();
	}
	bool get_can_sleep() const { return props.can_sleep; }

	void apply_force(const Vector2 &p_force, const Variant &p_position = Variant());
	void apply_impulse(const Vector2 &p_impulse, const Variant &p_position = Variant());
	void apply_torque(float p_torque);
	void set_linear_velocity(const Vector2 &p_value);
	Vector2 get_linear_velocity() const;
	void set_angular_velocity(float p_value);
	float get_angular_velocity() const;
	float get_mass() const;
	bool is_sleeping() const;
	// The pose at the start of this tick's Box2D step, in global space (as the node transform is).
	Vector2 get_previous_position() const;
	// A null rotation keeps the current one.
	void teleport(const Vector2 &p_position, const Variant &p_rotation = Variant());
};

} // namespace godot

VARIANT_ENUM_CAST(PixelRigidBody::Shape);
