#include "pixel_material_set.h"

#include <godot_cpp/core/class_db.hpp>

namespace godot {

void PixelMaterialSet::_bind_methods() {
	ClassDB::bind_method(D_METHOD("validate"), &PixelMaterialSet::validate);

	ClassDB::bind_method(D_METHOD("set_materials", "value"), &PixelMaterialSet::set_materials);
	ClassDB::bind_method(D_METHOD("get_materials"), &PixelMaterialSet::get_materials);
	ClassDB::bind_method(D_METHOD("set_reactions", "value"), &PixelMaterialSet::set_reactions);
	ClassDB::bind_method(D_METHOD("get_reactions"), &PixelMaterialSet::get_reactions);

	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "materials", PROPERTY_HINT_ARRAY_TYPE, "PixelMaterial"), "set_materials", "get_materials");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "reactions", PROPERTY_HINT_ARRAY_TYPE, "PixelReaction"), "set_reactions", "get_reactions");
}

bool PixelMaterialSet::build_table(MaterialTable &out, String &error) const {
	std::vector<MaterialDef> defs;
	defs.reserve((size_t)materials.size());
	for (int i = 0; i < materials.size(); ++i) {
		Ref<PixelMaterial> material = materials[i];
		// A null slot becomes an unnamed def, so compile() reports it by index.
		defs.push_back(material.is_valid() ? material->to_def() : MaterialDef{});
	}

	std::vector<ReactionDef> def_reactions;
	def_reactions.reserve((size_t)reactions.size());
	for (int i = 0; i < reactions.size(); ++i) {
		Ref<PixelReaction> reaction = reactions[i];
		def_reactions.push_back(reaction.is_valid() ? reaction->to_def() : ReactionDef{});
	}

	std::string compile_error;
	if (!MaterialTable::compile(defs, def_reactions, out, compile_error)) {
		error = compile_error.c_str();
		return false;
	}
	error = String();
	return true;
}

String PixelMaterialSet::validate() const {
	MaterialTable table;
	String error;
	return build_table(table, error) ? String() : error;
}

} // namespace godot
