#pragma once

#include <vector>

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include "core/terrain.h"

namespace godot {

// One palette shared by every room: maps an exact RGBA8 Color to a material name, or to one
// of the special values "@keep", "@light" and "@body". Anything else is a material name.
class PixelRoomPalette : public Resource {
	GDCLASS(PixelRoomPalette, Resource)

	Dictionary entries;

protected:
	static void _bind_methods();

public:
	std::vector<PaletteEntry> to_entries() const; // keyed by Color::to_rgba32()

	void set_entries(const Dictionary &p_entries) { entries = p_entries; }
	Dictionary get_entries() const { return entries; }
};

} // namespace godot
