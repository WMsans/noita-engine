#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include "core/terrain.h"

namespace godot {

// One PNG room. to_def() reads the image once through get_image(), converts a copy to
// RGBA8 and fills RoomDef::rgba row-major. The room is named after the image's file.
class PixelRoom : public Resource {
	GDCLASS(PixelRoom, Resource)

public:
	// Values match RoomAnchor for the three named anchors; to_def() falls back to
	// Embedded for anything out of range.
	enum Anchor {
		ANCHOR_SURFACE = 0,
		ANCHOR_EMBEDDED = 1,
		ANCHOR_CAVE = 2,
	};

private:
	Ref<Texture2D> image;
	float weight = 1.0f;
	int anchor = ANCHOR_EMBEDDED;
	PackedStringArray biomes;

protected:
	static void _bind_methods();

public:
	// False + `error` when there is no image; everything else is a core compile error.
	bool to_def(int index, RoomDef &out, String &error) const;

	void set_image(const Ref<Texture2D> &p_value) { image = p_value; }
	Ref<Texture2D> get_image() const { return image; }

	void set_weight(float p_value) { weight = p_value; }
	float get_weight() const { return weight; }

	void set_anchor(int p_value) { anchor = p_value; }
	int get_anchor() const { return anchor; }

	void set_biomes(const PackedStringArray &p_value) { biomes = p_value; }
	PackedStringArray get_biomes() const { return biomes; }
};

} // namespace godot

VARIANT_ENUM_CAST(godot::PixelRoom::Anchor);
