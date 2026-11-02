#include "pixel_rigid_body.h"

#include <unordered_map>

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "pixel_world.h"

namespace godot {

namespace {

// Texture2D::get_image() reads the texture back from the GPU, which waits for every frame in
// flight (about 0.5 ms each on an M1, more with the light pass queued), and bodies are spawned
// from a few shared textures. So each texture is decoded to RGBA8 once, keyed by its instance,
// and decoded again after it emits `changed`. Main thread only, like build_def.
std::unordered_map<uint64_t, Ref<Image>> decoded_images;

void forget_decoded_image(uint64_t p_texture_id) { decoded_images.erase(p_texture_id); }

Ref<Image> decoded_image(const Ref<Texture2D> &p_texture) {
	const uint64_t id = (uint64_t)p_texture->get_instance_id();
	const auto found = decoded_images.find(id);
	if (found != decoded_images.end()) return found->second;

	Ref<Image> image = p_texture->get_image();
	if (image.is_null() || image->is_empty()) return Ref<Image>();
	image = image->duplicate();
	if (image->is_compressed()) image->decompress();
	image->convert(Image::FORMAT_RGBA8);
	// Drop the entries of freed textures; a texture's signals go with it, so nothing else would.
	for (auto it = decoded_images.begin(); it != decoded_images.end();)
		it = ObjectDB::get_instance(ObjectID(it->first)) ? std::next(it) : decoded_images.erase(it);
	decoded_images[id] = image;
	p_texture->connect("changed", callable_mp_static(&forget_decoded_image).bind(id), Object::CONNECT_ONE_SHOT);
	return image;
}

} // namespace

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
	ClassDB::bind_method(D_METHOD("get_previous_position"), &PixelRigidBody::get_previous_position);
	ClassDB::bind_method(D_METHOD("get_body_position"), &PixelRigidBody::get_body_position);
	ClassDB::bind_method(D_METHOD("teleport", "position", "rotation"), &PixelRigidBody::teleport, DEFVAL(Variant()));

	ClassDB::bind_method(D_METHOD("set_body_size", "value"), &PixelRigidBody::set_body_size);
	ClassDB::bind_method(D_METHOD("get_body_size"), &PixelRigidBody::get_body_size);
	ClassDB::bind_method(D_METHOD("set_mass", "value"), &PixelRigidBody::set_mass);
	ClassDB::bind_method(D_METHOD("get_body_mass"), &PixelRigidBody::get_body_mass);
	ClassDB::bind_method(D_METHOD("set_gravity_scale", "value"), &PixelRigidBody::set_gravity_scale);
	ClassDB::bind_method(D_METHOD("get_gravity_scale"), &PixelRigidBody::get_gravity_scale);
	ClassDB::bind_method(D_METHOD("set_linear_damping", "value"), &PixelRigidBody::set_linear_damping);
	ClassDB::bind_method(D_METHOD("get_linear_damping"), &PixelRigidBody::get_linear_damping);
	ClassDB::bind_method(D_METHOD("set_angular_damping", "value"), &PixelRigidBody::set_angular_damping);
	ClassDB::bind_method(D_METHOD("get_angular_damping"), &PixelRigidBody::get_angular_damping);
	ClassDB::bind_method(D_METHOD("set_lock_rotation", "value"), &PixelRigidBody::set_lock_rotation);
	ClassDB::bind_method(D_METHOD("get_lock_rotation"), &PixelRigidBody::get_lock_rotation);
	ClassDB::bind_method(D_METHOD("set_terrain_collision", "value"), &PixelRigidBody::set_terrain_collision);
	ClassDB::bind_method(D_METHOD("get_terrain_collision"), &PixelRigidBody::get_terrain_collision);
	ClassDB::bind_method(D_METHOD("set_cell_proxies", "value"), &PixelRigidBody::set_cell_proxies);
	ClassDB::bind_method(D_METHOD("get_cell_proxies"), &PixelRigidBody::get_cell_proxies);
	ClassDB::bind_method(D_METHOD("set_proxy_step_height", "value"), &PixelRigidBody::set_proxy_step_height);
	ClassDB::bind_method(D_METHOD("get_proxy_step_height"), &PixelRigidBody::get_proxy_step_height);
	ClassDB::bind_method(D_METHOD("set_can_sleep", "value"), &PixelRigidBody::set_can_sleep);
	ClassDB::bind_method(D_METHOD("get_can_sleep"), &PixelRigidBody::get_can_sleep);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_texture", "get_texture");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "pixel_material"), "set_pixel_material", "get_pixel_material");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "palette", PROPERTY_HINT_RESOURCE_TYPE, "PixelRoomPalette"), "set_palette", "get_palette");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "shape", PROPERTY_HINT_ENUM, "Hull,Circle,Boxes"), "set_shape", "get_shape");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "friction"), "set_friction", "get_friction");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "restitution"), "set_restitution", "get_restitution");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "linear_velocity", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_linear_velocity", "get_linear_velocity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "angular_velocity", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_angular_velocity", "get_angular_velocity");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2I, "body_size"), "set_body_size", "get_body_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mass"), "set_mass", "get_body_mass");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gravity_scale"), "set_gravity_scale", "get_gravity_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "linear_damping"), "set_linear_damping", "get_linear_damping");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "angular_damping"), "set_angular_damping", "get_angular_damping");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "lock_rotation"), "set_lock_rotation", "get_lock_rotation");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "terrain_collision"), "set_terrain_collision", "get_terrain_collision");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "cell_proxies"), "set_cell_proxies", "get_cell_proxies");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "proxy_step_height"), "set_proxy_step_height", "get_proxy_step_height");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "can_sleep"), "set_can_sleep", "get_can_sleep");

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

// A definition that was refused may become valid once the shape or the mass is set.
// Only applies at join: BodyProps has no size field, so a joined body is not resized.
void PixelRigidBody::set_body_size(const Vector2i &p_value) {
	body_size = p_value;
	refused = false;
}

void PixelRigidBody::set_mass(float p_value) {
	props.mass = p_value;
	refused = false;
	apply_props();
}

void PixelRigidBody::apply_props() {
	if (!B2_IS_NULL(body())) get_world()->get_physics()->set_props(handle, props);
}

bool PixelRigidBody::build_def(const MaterialTable &p_table, const Transform2D &p_to_world, BodyDef &r_def,
                               String &r_error) const {
	r_def.props = props;
	r_def.friction = friction;
	r_def.restitution = restitution;
	r_def.shape = shape == SHAPE_CIRCLE ? BodyShape::Circle : shape == SHAPE_BOXES ? BodyShape::Boxes : BodyShape::Hull;
	if (texture.is_null()) {
		// No pixels: an unstamped box of `body_size`, which is how a character body is made.
		if (body_size.x <= 0 || body_size.y <= 0) {
			r_error = "has no texture";
			return false;
		}
		if (props.mass <= 0) {
			r_error = "body_size needs a mass";
			return false;
		}
		r_def.box_w = body_size.x;
		r_def.box_h = body_size.y;
	} else {
		if (!build_pixel_def(p_table, r_def, r_error)) return false;
	}

	const Transform2D xform = p_to_world * get_global_transform();
	r_def.x = xform.get_origin().x;
	r_def.y = xform.get_origin().y;
	r_def.angle = xform.get_rotation();
	return true;
}

bool PixelRigidBody::build_pixel_def(const MaterialTable &p_table, BodyDef &r_def, String &r_error) const {
	const Ref<Image> image = decoded_image(texture);
	if (image.is_null()) {
		r_error = "texture has no image data";
		return false;
	}

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

void PixelRigidBody::teleport(const Vector2 &p_position, const Variant &p_rotation) {
	set_global_position(p_position);
	if (p_rotation.get_type() != Variant::NIL) set_global_rotation((double)p_rotation);
	const b2BodyId id = body();
	if (B2_IS_NULL(id)) return;
	const Transform2D xform = get_world()->get_global_transform().affine_inverse() * get_global_transform();
	get_world()->get_physics()->teleport(handle, xform.get_origin().x, xform.get_origin().y, xform.get_rotation());
}

Vector2 PixelRigidBody::get_previous_position() const {
	PixelWorld *world = get_world();
	if (!world || B2_IS_NULL(body()) || !world->get_physics()) return get_global_position();
	const b2Vec2 previous = world->get_physics()->previous_position(handle);
	return world->get_global_transform().xform(Vector2(previous.x, previous.y));
}

Vector2 PixelRigidBody::get_body_position() const {
	PixelWorld *world = get_world();
	if (!world || B2_IS_NULL(body()) || !world->get_physics()) return get_global_position();
	const b2Vec2 at = b2Body_GetPosition(body());
	return world->get_global_transform().xform(Vector2(at.x, at.y));
}

} // namespace godot
