#include "pixel_room.h"

#include <cstddef>

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/color.hpp>

namespace godot {

void PixelRoom::_bind_methods() {
	BIND_ENUM_CONSTANT(ANCHOR_SURFACE);
	BIND_ENUM_CONSTANT(ANCHOR_EMBEDDED);
	BIND_ENUM_CONSTANT(ANCHOR_CAVE);

	ClassDB::bind_method(D_METHOD("set_image", "value"), &PixelRoom::set_image);
	ClassDB::bind_method(D_METHOD("get_image"), &PixelRoom::get_image);
	ClassDB::bind_method(D_METHOD("set_weight", "value"), &PixelRoom::set_weight);
	ClassDB::bind_method(D_METHOD("get_weight"), &PixelRoom::get_weight);
	ClassDB::bind_method(D_METHOD("set_anchor", "value"), &PixelRoom::set_anchor);
	ClassDB::bind_method(D_METHOD("get_anchor"), &PixelRoom::get_anchor);
	ClassDB::bind_method(D_METHOD("set_biomes", "value"), &PixelRoom::set_biomes);
	ClassDB::bind_method(D_METHOD("get_biomes"), &PixelRoom::get_biomes);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "image", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_image", "get_image");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "weight"), "set_weight", "get_weight");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "anchor", PROPERTY_HINT_ENUM, "Surface:0,Embedded:1,Cave:2"), "set_anchor", "get_anchor");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "biomes"), "set_biomes", "get_biomes");
}

bool PixelRoom::to_def(int index, RoomDef &out, String &error) const {
	out = RoomDef();
	if (!image.is_valid()) {
		error = "room " + itos(index) + " has no image";
		return false;
	}
	Ref<Image> pixels = image->get_image();
	if (pixels.is_null() || pixels->is_empty()) {
		error = "room " + itos(index) + " has no image";
		return false;
	}
	if (pixels->get_format() != Image::FORMAT_RGBA8) {
		// Convert a copy: the texture's own image must not change when a .tres is validated.
		pixels = pixels->duplicate();
		if (pixels->is_compressed()) pixels->decompress();
		pixels->convert(Image::FORMAT_RGBA8);
	}

	const String path = image->get_path();
	out.name = path.is_empty() ? (String("room ") + itos(index)).utf8().get_data()
	                           : path.get_file().get_basename().utf8().get_data();
	out.width = pixels->get_width();
	out.height = pixels->get_height();
	out.rgba.resize((size_t)out.width * (size_t)out.height);
	for (int y = 0; y < out.height; ++y)
		for (int x = 0; x < out.width; ++x)
			out.rgba[(size_t)y * (size_t)out.width + (size_t)x] = pixels->get_pixel(x, y).to_rgba32();
	out.weight = weight;
	out.anchor = anchor == ANCHOR_SURFACE  ? RoomAnchor::Surface
	             : anchor == ANCHOR_CAVE ? RoomAnchor::Cave
	                                     : RoomAnchor::Embedded;
	out.biomes.clear();
	for (int i = 0; i < biomes.size(); ++i)
		out.biomes.push_back(String(biomes[i]).utf8().get_data());
	return true;
}

} // namespace godot
