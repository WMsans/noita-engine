#pragma once

#include <godot_cpp/classes/node2d.hpp>

namespace godot {

// Empty Node2D subclass for now; Task 8 fills in the simulation host.
class PixelWorld : public Node2D {
	GDCLASS(PixelWorld, Node2D)

protected:
	static void _bind_methods();
};

} // namespace godot
