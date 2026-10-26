#pragma once

#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/node_path.hpp>

#include "box2d/box2d.h"

namespace godot {

class PixelWorld;

// What PixelRigidBody and PixelCharacter share: joining a PixelWorld on the first physics frame
// (after every _ready, the world's included), leaving it when the node leaves the tree, the
// `hit` and `left_world` signals, and forgetting the world when it is freed first.
class PixelPhysicsNode : public Node2D {
	GDCLASS(PixelPhysicsNode, Node2D)

	NodePath world_path;
	ObjectID world_id;

protected:
	int handle = -1;
	bool refused = false;  // a bad definition was reported once; don't retry every frame

	static void _bind_methods();
	void _notification(int p_what);
	virtual int join(PixelWorld &p_world) = 0;  // the handle, or -1 after reporting why
	b2BodyId body() const;  // b2_nullBodyId unless joined to a live world
	// Joining runs on the internal physics tick: a script extending the node with its own
	// _physics_process replaces the virtual one, so it must not live there.
	void try_join();

public:

	int get_handle() const { return handle; }
	PixelWorld *get_world() const;
	void detach();  // the world's physics is going away; forget the handle

	void set_world(const NodePath &p_value) { world_path = p_value; }
	NodePath get_world_path() const { return world_path; }
};

} // namespace godot
