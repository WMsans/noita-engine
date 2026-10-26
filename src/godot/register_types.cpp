#include <gdextension_interface.h>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "pixel_biome.h"
#include "pixel_joints.h"
#include "pixel_material.h"
#include "pixel_material_set.h"
#include "pixel_reaction.h"
#include "pixel_rigid_body.h"
#include "pixel_room.h"
#include "pixel_room_palette.h"
#include "pixel_world.h"
#include "pixel_world_generator.h"

using namespace godot;

static void initialize_noita_engine_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(PixelMaterial);
	GDREGISTER_CLASS(PixelReaction);
	GDREGISTER_CLASS(PixelMaterialSet);
	GDREGISTER_CLASS(PixelBiome);
	GDREGISTER_CLASS(PixelRoomPalette);
	GDREGISTER_CLASS(PixelRoom);
	GDREGISTER_CLASS(PixelWorldGenerator);
	GDREGISTER_CLASS(PixelWorld);
	GDREGISTER_CLASS(PixelRigidBody);
	GDREGISTER_ABSTRACT_CLASS(PixelJoint);
	GDREGISTER_CLASS(PixelRevoluteJoint);
	GDREGISTER_CLASS(PixelWheelJoint);
}

static void uninitialize_noita_engine_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
}

extern "C" {
GDExtensionBool GDE_EXPORT noita_engine_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

	init_obj.register_initializer(initialize_noita_engine_module);
	init_obj.register_terminator(uninitialize_noita_engine_module);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

	return init_obj.init();
}
}
