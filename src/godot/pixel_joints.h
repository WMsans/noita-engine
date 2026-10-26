#pragma once

#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/node_path.hpp>

#include "box2d/box2d.h"
#include "core/physics.h"

namespace godot {

class PixelRigidBody;
class PixelWorld;

// Joins two PixelRigidBody nodes at this node's global position once both have joined their
// world; removed again when this node leaves the tree. Setters apply live.
class PixelJoint : public Node2D {
	GDCLASS(PixelJoint, Node2D)

	NodePath body_a;
	NodePath body_b;
	ObjectID world_id;

protected:
	b2JointId joint = b2_nullJointId;

	static void _bind_methods();
	void _notification(int p_what);
	// Creates the Box2D joint; the anchor is in the world's cell space.
	virtual b2JointId create(PhysicsWorld &p_physics, int p_a, int p_b, Vector2 p_anchor) = 0;
	b2JointId live() const;  // the joint if it and its world are alive, else null
	void try_join();         // on the internal physics tick, so a script can't shadow it

public:

	void set_body_a(const NodePath &p_value) { body_a = p_value; }
	NodePath get_body_a() const { return body_a; }
	void set_body_b(const NodePath &p_value) { body_b = p_value; }
	NodePath get_body_b() const { return body_b; }
};

class PixelRevoluteJoint : public PixelJoint {
	GDCLASS(PixelRevoluteJoint, PixelJoint)

	RevoluteParams params;

protected:
	static void _bind_methods();
	b2JointId create(PhysicsWorld &p_physics, int p_a, int p_b, Vector2 p_anchor) override;

public:
	void set_enable_limit(bool p_value);
	bool get_enable_limit() const { return params.limit; }
	void set_lower_angle(float p_value);
	float get_lower_angle() const { return params.lower; }
	void set_upper_angle(float p_value);
	float get_upper_angle() const { return params.upper; }
	void set_enable_motor(bool p_value);
	bool get_enable_motor() const { return params.motor; }
	void set_motor_speed(float p_value);
	float get_motor_speed() const { return params.speed; }
	void set_max_motor_torque(float p_value);
	float get_max_motor_torque() const { return params.max_torque; }
};

class PixelWheelJoint : public PixelJoint {
	GDCLASS(PixelWheelJoint, PixelJoint)

	WheelParams params;

protected:
	static void _bind_methods();
	b2JointId create(PhysicsWorld &p_physics, int p_a, int p_b, Vector2 p_anchor) override;

public:
	void set_axis(const Vector2 &p_value) { params.axis_x = p_value.x, params.axis_y = p_value.y; }
	Vector2 get_axis() const { return Vector2(params.axis_x, params.axis_y); }
	void set_frequency(float p_value);
	float get_frequency() const { return params.hertz; }
	void set_damping_ratio(float p_value);
	float get_damping_ratio() const { return params.damping; }
	void set_enable_motor(bool p_value);
	bool get_enable_motor() const { return params.motor; }
	void set_motor_speed(float p_value);
	float get_motor_speed() const { return params.speed; }
	void set_max_motor_torque(float p_value);
	float get_max_motor_torque() const { return params.max_torque; }
};

} // namespace godot
