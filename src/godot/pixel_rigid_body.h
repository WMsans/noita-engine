#pragma once

#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include "core/physics.h"
#include "pixel_physics_node.h"
#include "pixel_room_palette.h"

namespace godot {

// A non-breakable Box2D body made of material pixels: each opaque texture pixel is
// `pixel_material`, or the palette's material for its colour when `palette` is set. It joins
// its PixelWorld on its first physics frame and follows Box2D from then on. Velocities and
// forces are in cells and seconds.
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

	void apply_force(const Vector2 &p_force, const Variant &p_position = Variant());
	void apply_impulse(const Vector2 &p_impulse, const Variant &p_position = Variant());
	void apply_torque(float p_torque);
	void set_linear_velocity(const Vector2 &p_value);
	Vector2 get_linear_velocity() const;
	void set_angular_velocity(float p_value);
	float get_angular_velocity() const;
	float get_mass() const;
	bool is_sleeping() const;
	void teleport(const Vector2 &p_position, float p_rotation);
};

} // namespace godot

VARIANT_ENUM_CAST(PixelRigidBody::Shape);
