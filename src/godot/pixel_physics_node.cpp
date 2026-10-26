#include "pixel_physics_node.h"

#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "pixel_world.h"

namespace godot {

void PixelPhysicsNode::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_world", "value"), &PixelPhysicsNode::set_world);
	ClassDB::bind_method(D_METHOD("get_world"), &PixelPhysicsNode::get_world_path);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "world", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "PixelWorld"), "set_world", "get_world");

	ADD_SIGNAL(MethodInfo("hit", PropertyInfo(Variant::OBJECT, "other"), PropertyInfo(Variant::VECTOR2, "point"),
	                      PropertyInfo(Variant::VECTOR2, "normal"), PropertyInfo(Variant::FLOAT, "speed")));
	ADD_SIGNAL(MethodInfo("left_world"));
}

void PixelPhysicsNode::_ready() { set_physics_process(true); }

void PixelPhysicsNode::_notification(int p_what) {
	if (p_what != NOTIFICATION_EXIT_TREE) return;
	PixelWorld *world = get_world();
	if (world && handle >= 0) world->unregister_body(handle);
	detach();
}

PixelWorld *PixelPhysicsNode::get_world() const {
	return world_id.is_valid() ? Object::cast_to<PixelWorld>(ObjectDB::get_instance(world_id)) : nullptr;
}

void PixelPhysicsNode::detach() {
	handle = -1;
	world_id = ObjectID();
}

void PixelPhysicsNode::_physics_process(double) {
	if (handle >= 0 || refused) return;
	PixelWorld *world = nullptr;
	if (!world_path.is_empty()) {
		world = Object::cast_to<PixelWorld>(get_node_or_null(world_path));
	} else if (SceneTree *tree = get_tree()) {
		world = Object::cast_to<PixelWorld>(tree->get_first_node_in_group("pixel_world"));
	}
	if (!world || !world->get_physics()) return;  // no world yet, or it has not started
	handle = join(*world);
	if (handle < 0) {
		refused = true;
		return;
	}
	world_id = world->get_instance_id();
}

b2BodyId PixelPhysicsNode::body() const {
	PixelWorld *world = get_world();
	if (!world || handle < 0 || !world->get_physics()) return b2_nullBodyId;
	return world->get_physics()->body_id(handle);
}

} // namespace godot
