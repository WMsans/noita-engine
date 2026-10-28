#include "pixel_character.h"

#include <godot_cpp/core/class_db.hpp>

#include "pixel_world.h"

namespace godot {

void PixelCharacter::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_hitbox_size", "value"), &PixelCharacter::set_hitbox_size);
	ClassDB::bind_method(D_METHOD("get_hitbox_size"), &PixelCharacter::get_hitbox_size);
	ClassDB::bind_method(D_METHOD("set_step_height", "value"), &PixelCharacter::set_step_height);
	ClassDB::bind_method(D_METHOD("get_step_height"), &PixelCharacter::get_step_height);
	ClassDB::bind_method(D_METHOD("set_mass", "value"), &PixelCharacter::set_mass);
	ClassDB::bind_method(D_METHOD("get_mass"), &PixelCharacter::get_mass);
	ClassDB::bind_method(D_METHOD("set_friction", "value"), &PixelCharacter::set_friction);
	ClassDB::bind_method(D_METHOD("get_friction"), &PixelCharacter::get_friction);
	ClassDB::bind_method(D_METHOD("set_gravity_scale", "value"), &PixelCharacter::set_gravity_scale);
	ClassDB::bind_method(D_METHOD("get_gravity_scale"), &PixelCharacter::get_gravity_scale);
	ClassDB::bind_method(D_METHOD("set_velocity", "value"), &PixelCharacter::set_velocity);
	ClassDB::bind_method(D_METHOD("get_velocity"), &PixelCharacter::get_velocity);
	ClassDB::bind_method(D_METHOD("apply_impulse", "impulse"), &PixelCharacter::apply_impulse);
	ClassDB::bind_method(D_METHOD("apply_force", "force"), &PixelCharacter::apply_force);
	ClassDB::bind_method(D_METHOD("teleport", "position"), &PixelCharacter::teleport);
	ClassDB::bind_method(D_METHOD("is_on_floor"), &PixelCharacter::is_on_floor);
	ClassDB::bind_method(D_METHOD("is_on_ceiling"), &PixelCharacter::is_on_ceiling);
	ClassDB::bind_method(D_METHOD("is_on_wall"), &PixelCharacter::is_on_wall);
	ClassDB::bind_method(D_METHOD("get_wall_side"), &PixelCharacter::get_wall_side);
	ClassDB::bind_method(D_METHOD("is_in_liquid"), &PixelCharacter::is_in_liquid);
	ClassDB::bind_method(D_METHOD("get_submerged_fraction"), &PixelCharacter::get_submerged_fraction);
	ClassDB::bind_method(D_METHOD("get_liquid_material"), &PixelCharacter::get_liquid_material);

	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2I, "hitbox_size"), "set_hitbox_size", "get_hitbox_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "step_height"), "set_step_height", "get_step_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mass"), "set_mass", "get_mass");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "friction"), "set_friction", "get_friction");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gravity_scale"), "set_gravity_scale", "get_gravity_scale");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "velocity", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_velocity", "get_velocity");
}

int PixelCharacter::join(PixelWorld &p_world) { return p_world.register_character(this); }

CreatureDef PixelCharacter::build_def(const Transform2D &p_to_world) const {
	CreatureDef def;
	def.w = hitbox_size.x;
	def.h = hitbox_size.y;
	def.step_height = step_height;
	def.mass = mass;
	def.friction = friction;
	def.gravity_scale = gravity_scale;
	const Vector2 at = p_to_world.xform(get_global_position());
	def.x = at.x;
	def.y = at.y;
	return def;
}

const CreatureState &PixelCharacter::state() const {
	static const CreatureState none;
	PixelWorld *world = get_world();
	if (!world || handle < 0 || !world->get_physics()) return none;
	return world->get_physics()->creature_state(handle);
}

void PixelCharacter::set_velocity(const Vector2 &p_value) {
	const b2BodyId id = body();
	if (B2_IS_NULL(id)) return;
	const Vector2 v = get_world()->get_global_transform().affine_inverse().basis_xform(p_value);
	b2Body_SetLinearVelocity(id, {v.x, v.y});
}

Vector2 PixelCharacter::get_velocity() const {
	const b2BodyId id = body();
	if (B2_IS_NULL(id)) return Vector2();
	const b2Vec2 v = b2Body_GetLinearVelocity(id);
	return get_world()->get_global_transform().basis_xform(Vector2(v.x, v.y));
}

void PixelCharacter::apply_impulse(const Vector2 &p_impulse) {
	const b2BodyId id = body();
	if (B2_IS_NULL(id)) return;
	const Vector2 i = get_world()->get_global_transform().affine_inverse().basis_xform(p_impulse);
	b2Body_ApplyLinearImpulseToCenter(id, {i.x, i.y}, true);
}

void PixelCharacter::apply_force(const Vector2 &p_force) {
	const b2BodyId id = body();
	if (B2_IS_NULL(id)) return;
	const Vector2 f = get_world()->get_global_transform().affine_inverse().basis_xform(p_force);
	b2Body_ApplyForceToCenter(id, {f.x, f.y}, true);
}

void PixelCharacter::teleport(const Vector2 &p_position) {
	set_global_position(p_position);
	const b2BodyId id = body();
	if (B2_IS_NULL(id)) return;
	const Vector2 at = get_world()->get_global_transform().affine_inverse().xform(p_position);
	get_world()->get_physics()->teleport(handle, at.x, at.y, 0.0f);
}

StringName PixelCharacter::get_liquid_material() const {
	PixelWorld *world = get_world();
	const MatId liquid = state().liquid;
	return world && liquid != MAT_AIR ? world->material_name(liquid) : StringName();
}

} // namespace godot
