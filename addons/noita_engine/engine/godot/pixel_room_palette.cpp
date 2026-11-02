#include "pixel_room_palette.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/color.hpp>

namespace godot {

namespace {

std::string to_std(const StringName &p_value) { return String(p_value).utf8().get_data(); }

} // namespace

void PixelRoomPalette::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_entries", "value"), &PixelRoomPalette::set_entries);
	ClassDB::bind_method(D_METHOD("get_entries"), &PixelRoomPalette::get_entries);

	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "entries"), "set_entries", "get_entries");
}

std::vector<PaletteEntry> PixelRoomPalette::to_entries() const {
	std::vector<PaletteEntry> out;
	out.reserve((size_t)entries.size());
	const Array keys = entries.keys();
	for (int i = 0; i < keys.size(); ++i) {
		const Variant key = keys[i];
		const Color colour = key;
		const StringName target = entries[key];
		PaletteEntry entry;
		entry.rgba = colour.to_rgba32();
		entry.kind = PaletteKind::Material;
		entry.material = to_std(target);
		if (target == StringName("@keep")) {
			entry.kind = PaletteKind::Keep;
			entry.material.clear();
		} else if (target == StringName("@light")) {
			entry.kind = PaletteKind::MarkerLight;
			entry.material.clear();
		} else if (target == StringName("@body")) {
			entry.kind = PaletteKind::MarkerBody;
			entry.material.clear();
		}
		out.push_back(entry);
	}
	return out;
}

} // namespace godot
