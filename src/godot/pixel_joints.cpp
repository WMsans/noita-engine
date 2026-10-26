#include "pixel_joints.h"

#include <godot_cpp/core/class_db.hpp>

#include "pixel_rigid_body.h"
#include "pixel_world.h"

namespace godot {

void PixelJoint::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_body_a", "value"), &PixelJoint::set_body_a);
	ClassDB::bind_method(D_METHOD("get_body_a"), &PixelJoint::get_body_a);
	ClassDB::bind_method(D_METHOD("set_body_b", "value"), &PixelJoint::set_body_b);
	ClassDB::bind_method(D_METHOD("get_body_b"), &PixelJoint::get_body_b);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "body_a", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "PixelRigidBody"), "set_body_a", "get_body_a");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "body_b", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "PixelRigidBody"), "set_body_b", "get_body_b");
}

void PixelJoint::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY) set_physics_process_internal(true);
	if (p_what == NOTIFICATION_INTERNAL_PHYSICS_PROCESS) try_join();
	if (p_what != NOTIFICATION_EXIT_TREE) return;
	const b2JointId id = live();
	PixelWorld *world = Object::cast_to<PixelWorld>(ObjectDB::get_instance(world_id));
	if (!B2_IS_NULL(id) && world) world->get_physics()->remove_joint(id);
	joint = b2_nullJointId;
	world_id = ObjectID();
}

b2JointId PixelJoint::live() const {
	PixelWorld *world = Object::cast_to<PixelWorld>(ObjectDB::get_instance(world_id));
	if (!world || !world->get_physics() || B2_IS_NULL(joint) || !b2Joint_IsValid(joint)) return b2_nullJointId;
	return joint;
}

// Waits for both bodies to join the same world, then makes the joint (again, if a body was
// freed and replaced).
void PixelJoint::try_join() {
	if (!B2_IS_NULL(live())) return;
	PixelRigidBody *a = Object::cast_to<PixelRigidBody>(get_node_or_null(body_a));
	PixelRigidBody *b = Object::cast_to<PixelRigidBody>(get_node_or_null(body_b));
	if (!a || !b || a == b || a->get_handle() < 0 || b->get_handle() < 0) return;
	PixelWorld *world = a->get_world();
	if (!world || world != b->get_world() || !world->get_physics()) return;
	const Vector2 anchor = world->get_global_transform().affine_inverse().xform(get_global_position());
	joint = create(*world->get_physics(), a->get_handle(), b->get_handle(), anchor);
	world_id = world->get_instance_id();
}

// --- revolute -----------------------------------------------------------------------------

void PixelRevoluteJoint::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_enable_limit", "value"), &PixelRevoluteJoint::set_enable_limit);
	ClassDB::bind_method(D_METHOD("get_enable_limit"), &PixelRevoluteJoint::get_enable_limit);
	ClassDB::bind_method(D_METHOD("set_lower_angle", "value"), &PixelRevoluteJoint::set_lower_angle);
	ClassDB::bind_method(D_METHOD("get_lower_angle"), &PixelRevoluteJoint::get_lower_angle);
	ClassDB::bind_method(D_METHOD("set_upper_angle", "value"), &PixelRevoluteJoint::set_upper_angle);
	ClassDB::bind_method(D_METHOD("get_upper_angle"), &PixelRevoluteJoint::get_upper_angle);
	ClassDB::bind_method(D_METHOD("set_enable_motor", "value"), &PixelRevoluteJoint::set_enable_motor);
	ClassDB::bind_method(D_METHOD("get_enable_motor"), &PixelRevoluteJoint::get_enable_motor);
	ClassDB::bind_method(D_METHOD("set_motor_speed", "value"), &PixelRevoluteJoint::set_motor_speed);
	ClassDB::bind_method(D_METHOD("get_motor_speed"), &PixelRevoluteJoint::get_motor_speed);
	ClassDB::bind_method(D_METHOD("set_max_motor_torque", "value"), &PixelRevoluteJoint::set_max_motor_torque);
	ClassDB::bind_method(D_METHOD("get_max_motor_torque"), &PixelRevoluteJoint::get_max_motor_torque);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enable_limit"), "set_enable_limit", "get_enable_limit");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lower_angle"), "set_lower_angle", "get_lower_angle");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "upper_angle"), "set_upper_angle", "get_upper_angle");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enable_motor"), "set_enable_motor", "get_enable_motor");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "motor_speed"), "set_motor_speed", "get_motor_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_motor_torque"), "set_max_motor_torque", "get_max_motor_torque");
}

b2JointId PixelRevoluteJoint::create(PhysicsWorld &p_physics, int p_a, int p_b, Vector2 p_anchor) {
	return p_physics.add_revolute(p_a, p_b, p_anchor.x, p_anchor.y, params);
}

void PixelRevoluteJoint::set_enable_limit(bool p_value) {
	params.limit = p_value;
	if (!B2_IS_NULL(live())) b2RevoluteJoint_EnableLimit(joint, p_value);
}

void PixelRevoluteJoint::set_lower_angle(float p_value) {
	params.lower = p_value;
	if (!B2_IS_NULL(live())) b2RevoluteJoint_SetLimits(joint, params.lower, params.upper);
}

void PixelRevoluteJoint::set_upper_angle(float p_value) {
	params.upper = p_value;
	if (!B2_IS_NULL(live())) b2RevoluteJoint_SetLimits(joint, params.lower, params.upper);
}

void PixelRevoluteJoint::set_enable_motor(bool p_value) {
	params.motor = p_value;
	if (!B2_IS_NULL(live())) b2RevoluteJoint_EnableMotor(joint, p_value);
}

void PixelRevoluteJoint::set_motor_speed(float p_value) {
	params.speed = p_value;
	if (!B2_IS_NULL(live())) {
		b2RevoluteJoint_SetTargetMotorSpeed(joint, p_value);
		b2Joint_WakeBodies(joint);
	}
}

void PixelRevoluteJoint::set_max_motor_torque(float p_value) {
	params.max_torque = p_value;
	if (!B2_IS_NULL(live())) b2RevoluteJoint_SetMaxMotorTorque(joint, p_value);
}

// --- wheel --------------------------------------------------------------------------------

void PixelWheelJoint::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_axis", "value"), &PixelWheelJoint::set_axis);
	ClassDB::bind_method(D_METHOD("get_axis"), &PixelWheelJoint::get_axis);
	ClassDB::bind_method(D_METHOD("set_frequency", "value"), &PixelWheelJoint::set_frequency);
	ClassDB::bind_method(D_METHOD("get_frequency"), &PixelWheelJoint::get_frequency);
	ClassDB::bind_method(D_METHOD("set_damping_ratio", "value"), &PixelWheelJoint::set_damping_ratio);
	ClassDB::bind_method(D_METHOD("get_damping_ratio"), &PixelWheelJoint::get_damping_ratio);
	ClassDB::bind_method(D_METHOD("set_enable_motor", "value"), &PixelWheelJoint::set_enable_motor);
	ClassDB::bind_method(D_METHOD("get_enable_motor"), &PixelWheelJoint::get_enable_motor);
	ClassDB::bind_method(D_METHOD("set_motor_speed", "value"), &PixelWheelJoint::set_motor_speed);
	ClassDB::bind_method(D_METHOD("get_motor_speed"), &PixelWheelJoint::get_motor_speed);
	ClassDB::bind_method(D_METHOD("set_max_motor_torque", "value"), &PixelWheelJoint::set_max_motor_torque);
	ClassDB::bind_method(D_METHOD("get_max_motor_torque"), &PixelWheelJoint::get_max_motor_torque);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "axis"), "set_axis", "get_axis");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "frequency"), "set_frequency", "get_frequency");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "damping_ratio"), "set_damping_ratio", "get_damping_ratio");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enable_motor"), "set_enable_motor", "get_enable_motor");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "motor_speed"), "set_motor_speed", "get_motor_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_motor_torque"), "set_max_motor_torque", "get_max_motor_torque");
}

b2JointId PixelWheelJoint::create(PhysicsWorld &p_physics, int p_a, int p_b, Vector2 p_anchor) {
	return p_physics.add_wheel(p_a, p_b, p_anchor.x, p_anchor.y, params);
}

void PixelWheelJoint::set_frequency(float p_value) {
	params.hertz = p_value;
	if (!B2_IS_NULL(live())) b2WheelJoint_SetSpringHertz(joint, p_value);
}

void PixelWheelJoint::set_damping_ratio(float p_value) {
	params.damping = p_value;
	if (!B2_IS_NULL(live())) b2WheelJoint_SetSpringDampingRatio(joint, p_value);
}

void PixelWheelJoint::set_enable_motor(bool p_value) {
	params.motor = p_value;
	if (!B2_IS_NULL(live())) b2WheelJoint_EnableMotor(joint, p_value);
}

void PixelWheelJoint::set_motor_speed(float p_value) {
	params.speed = p_value;
	if (!B2_IS_NULL(live())) {
		b2WheelJoint_SetTargetMotorSpeed(joint, p_value);
		b2Joint_WakeBodies(joint);
	}
}

void PixelWheelJoint::set_max_motor_torque(float p_value) {
	params.max_torque = p_value;
	if (!B2_IS_NULL(live())) b2WheelJoint_SetMaxMotorTorque(joint, p_value);
}

} // namespace godot
