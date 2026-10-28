#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include "core/material.h"
#include "pixel_material.h"
#include "pixel_reaction.h"

namespace godot {

// A whole authored material table: the .tres default_set.tres loads one of these and
// PixelWorld compiles it once at _ready.
class PixelMaterialSet : public Resource {
	GDCLASS(PixelMaterialSet, Resource)

	TypedArray<PixelMaterial> materials;
	TypedArray<PixelReaction> reactions;

protected:
	static void _bind_methods();

public:
	// Compiles into `out`; on false `error` is MaterialTable::compile's message and `out` is
	// left untouched. C++-only: GDScript uses validate().
	bool build_table(MaterialTable &out, String &error) const;

	// "" when the set compiles, otherwise the compile error verbatim.
	String validate() const;

	void set_materials(const TypedArray<PixelMaterial> &p_materials) { materials = p_materials; }
	TypedArray<PixelMaterial> get_materials() const { return materials; }

	void set_reactions(const TypedArray<PixelReaction> &p_reactions) { reactions = p_reactions; }
	TypedArray<PixelReaction> get_reactions() const { return reactions; }
};

} // namespace godot
