#include "pixel_rigid_body.h"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "pixel_world.h"

namespace godot {

void PixelRigidBody::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_texture", "value"), &PixelRigidBody::set_texture);
	ClassDB::bind_method(D_METHOD("get_texture"), &PixelRigidBody::get_texture);
	ClassDB::bind_method(D_METHOD("set_pixel_material", "value"), &PixelRigidBody::set_pixel_material);
	ClassDB::bind_method(D_METHOD("get_pixel_material"), &PixelRigidBody::get_pixel_material);
	ClassDB::bind_method(D_METHOD("set_palette", "value"), &PixelRigidBody::set_palette);
	ClassDB::bind_method(D_METHOD("get_palette"), &PixelRigidBody::get_palette);
	ClassDB::bind_method(D_METHOD("set_shape", "value"), &PixelRigidBody::set_shape);
	ClassDB::bind_method(D_METHOD("get_shape"), &PixelRigidBody::get_shape);
	ClassDB::bind_method(D_METHOD("set_friction", "value"), &PixelRigidBody::set_friction);
	ClassDB::bind_method(D_METHOD("get_friction"), &PixelRigidBody::get_friction);
	ClassDB::bind_method(D_METHOD("set_restitution", "value"), &PixelRigidBody::set_restitution);
	ClassDB::bind_method(D_METHOD("get_restitution"), &PixelRigidBody::get_restitution);
	ClassDB::bind_method(D_METHOD("set_linear_velocity", "value"), &PixelRigidBody::set_linear_velocity);
	ClassDB::bind_method(D_METHOD("get_linear_velocity"), &PixelRigidBody::get_linear_velocity);
	ClassDB::bind_method(D_METHOD("set_angular_velocity", "value"), &PixelRigidBody::set_angular_velocity);
	ClassDB::bind_method(D_METHOD("get_angular_velocity"), &PixelRigidBody::get_angular_velocity);
	ClassDB::bind_method(D_METHOD("apply_force", "force", "position"), &PixelRigidBody::apply_force, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("apply_impulse", "impulse", "position"), &PixelRigidBody::apply_impulse, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("apply_torque", "torque"), &PixelRigidBody::apply_torque);
	ClassDB::bind_method(D_METHOD("get_mass"), &PixelRigidBody::get_mass);
	ClassDB::bind_method(D_METHOD("is_sleeping"), &PixelRigidBody::is_sleeping);
	ClassDB::bind_method(D_METHOD("teleport", "position", "rotation"), &PixelRigidBody::teleport);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_texture", "get_texture");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "pixel_material"), "set_pixel_material", "get_pixel_material");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "palette", PROPERTY_HINT_RESOURCE_TYPE, "PixelRoomPalette"), "set_palette", "get_palette");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "shape", PROPERTY_HINT_ENUM, "Hull,Circle,Boxes"), "set_shape", "get_shape");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "friction"), "set_friction", "get_friction");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "restitution"), "set_restitution", "get_restitution");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "linear_velocity", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_linear_velocity", "get_linear_velocity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "angular_velocity", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_angular_velocity", "get_angular_velocity");

	BIND_ENUM_CONSTANT(SHAPE_HULL);
	BIND_ENUM_CONSTANT(SHAPE_CIRCLE);
	BIND_ENUM_CONSTANT(SHAPE_BOXES);
}

int PixelRigidBody::join(PixelWorld &p_world) { return p_world.register_body(this); }

void PixelRigidBody::_draw() {
	if (texture.is_valid()) draw_texture(texture, -texture->get_size() * 0.5f);
}

void PixelRigidBody::set_texture(const Ref<Texture2D> &p_value) {
	texture = p_value;
	refused = false;
	queue_redraw();
}

bool PixelRigidBody::build_def(const MaterialTable &p_table, const Transform2D &p_to_world, BodyDef &r_def,
                               String &r_error) const {
	if (texture.is_null()) {
		r_error = "has no texture";
		return false;
	}
	Ref<Image> image = texture->get_image();
	if (image.is_null() || image->is_empty()) {
		r_error = "texture has no image data";
		return false;
	}
	image = image->duplicate();
	if (image->is_compressed()) image->decompress();
	image->convert(Image::FORMAT_RGBA8);

	std::vector<PaletteEntry> entries;
	if (palette.is_valid()) entries = palette->to_entries();
	MatId single = NO_MAT;
	if (palette.is_null()) {
		single = p_table.id_of(String(pixel_material).utf8().get_data());
		if (single == NO_MAT || single == MAT_AIR) {
			r_error = "pixel_material '" + String(pixel_material) + "' is not a material";
			return false;
		}
	}

	r_def.w = image->get_width();
	r_def.h = image->get_height();
	r_def.pixels.assign((size_t)r_def.w * r_def.h, NO_MAT);
	for (int y = 0; y < r_def.h; ++y)
		for (int x = 0; x < r_def.w; ++x) {
			const Color colour = image->get_pixel(x, y);
			if (colour.a < 0.5f) continue;
			MatId mat = single;
			if (palette.is_valid()) {
				mat = NO_MAT;
				for (const PaletteEntry &e : entries)
					if (e.kind == PaletteKind::Material && e.rgba == colour.to_rgba32()) mat = p_table.id_of(e.material);
				if (mat == NO_MAT || mat == MAT_AIR) {
					r_error = vformat("pixel (%d, %d) colour %s is not a material in the palette", x, y, colour.to_html());
					return false;
				}
			}
			r_def.pixels[(size_t)y * r_def.w + x] = mat;
		}

	const Transform2D xform = p_to_world * get_global_transform();
	r_def.x = xform.get_origin().x;
	r_def.y = xform.get_origin().y;
	r_def.angle = xform.get_rotation();
	r_def.shape = shape == SHAPE_CIRCLE ? BodyShape::Circle : shape == SHAPE_BOXES ? BodyShape::Boxes : BodyShape::Hull;
	r_def.friction = friction;
	r_def.restitution = restitution;
	return true;
}

// Godot positions are global; Box2D works in the world node's space.
void PixelRigidBody::apply_force(const Vector2 &p_force, const Variant &p_position) {
	const b2BodyId id = body();
	if (B2_IS_NULL(id)) return;
	const Transform2D to_world = get_world()->get_global_transform().affine_inverse();
	const Vector2 force = to_world.basis_xform(p_force);
	if (p_position.get_type() == Variant::VECTOR2) {
		const Vector2 at = to_world.xform((Vector2)p_position);
		b2Body_ApplyForce(id, {force.x, force.y}, {at.x, at.y}, true);
	} else {
		b2Body_ApplyForceToCenter(id, {force.x, force.y}, true);
	}
}

void PixelRigidBody::apply_impulse(const Vector2 &p_impulse, const Variant &p_position) {
	const b2BodyId id = body();
	if (B2_IS_NULL(id)) return;
	const Transform2D to_world = get_world()->get_global_transform().affine_inverse();
	const Vector2 impulse = to_world.basis_xform(p_impulse);
	if (p_position.get_type() == Variant::VECTOR2) {
		const Vector2 at = to_world.xform((Vector2)p_position);
		b2Body_ApplyLinearImpulse(id, {impulse.x, impulse.y}, {at.x, at.y}, true);
	} else {
		b2Body_ApplyLinearImpulseToCenter(id, {impulse.x, impulse.y}, true);
	}
}

void PixelRigidBody::apply_torque(float p_torque) {
	const b2BodyId id = body();
	if (!B2_IS_NULL(id)) b2Body_ApplyTorque(id, p_torque, true);
}

void PixelRigidBody::set_linear_velocity(const Vector2 &p_value) {
	const b2BodyId id = body();
	if (B2_IS_NULL(id)) return;
	const Vector2 v = get_world()->get_global_transform().affine_inverse().basis_xform(p_value);
	b2Body_SetLinearVelocity(id, {v.x, v.y});
}

Vector2 PixelRigidBody::get_linear_velocity() const {
	const b2BodyId id = body();
	if (B2_IS_NULL(id)) return Vector2();
	const b2Vec2 v = b2Body_GetLinearVelocity(id);
	return get_world()->get_global_transform().basis_xform(Vector2(v.x, v.y));
}

void PixelRigidBody::set_angular_velocity(float p_value) {
	const b2BodyId id = body();
	if (!B2_IS_NULL(id)) b2Body_SetAngularVelocity(id, p_value);
}

float PixelRigidBody::get_angular_velocity() const {
	const b2BodyId id = body();
	return B2_IS_NULL(id) ? 0.0f : b2Body_GetAngularVelocity(id);
}

float PixelRigidBody::get_mass() const {
	const b2BodyId id = body();
	return B2_IS_NULL(id) ? 0.0f : b2Body_GetMass(id);
}

bool PixelRigidBody::is_sleeping() const {
	const b2BodyId id = body();
	return !B2_IS_NULL(id) && !b2Body_IsAwake(id);
}

void PixelRigidBody::teleport(const Vector2 &p_position, float p_rotation) {
	set_global_position(p_position);
	set_global_rotation(p_rotation);
	const b2BodyId id = body();
	if (B2_IS_NULL(id)) return;
	const Transform2D xform = get_world()->get_global_transform().affine_inverse() * get_global_transform();
	get_world()->get_physics()->teleport(handle, xform.get_origin().x, xform.get_origin().y, xform.get_rotation());
}

} // namespace godot
