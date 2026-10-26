#include "pixel_world.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/texture2drd.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "core/rng.h"
#include "light_renderer.h"
#include "light_shaders.h"
#include "pixel_character.h"
#include "pixel_light.h"
#include "pixel_rigid_body.h"

namespace godot {

namespace {

Color color_from_rgba32(uint32_t p_rgba) {
	return Color(((p_rgba >> 24) & 0xFF) / 255.0f, ((p_rgba >> 16) & 0xFF) / 255.0f,
	             ((p_rgba >> 8) & 0xFF) / 255.0f, (p_rgba & 0xFF) / 255.0f);
}

uint8_t to_byte(float p_value) { return (uint8_t)(CLAMP(p_value, 0.0f, 1.0f) * 255.0f); }

// Hash salts for spawn jitter.
constexpr uint32_t SALT_SPREAD = 201;
constexpr uint32_t SALT_SCALE = 202;
constexpr float TICKS_PER_SECOND = 60.0f;
constexpr int FLOATS_PER_PARTICLE = 12;
// Four chunks: past this a blast is a script bug, and the disc loop would stall the frame.
constexpr int MAX_EXPLODE_RADIUS = 256;

} // namespace

void PixelWorld::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_cell", "pos", "material"), &PixelWorld::set_cell);
	ClassDB::bind_method(D_METHOD("get_material_at", "pos"), &PixelWorld::get_material_at);
	ClassDB::bind_method(D_METHOD("fill_rect", "rect", "material"), &PixelWorld::fill_rect);
	ClassDB::bind_method(D_METHOD("fill_circle", "center", "radius", "material"), &PixelWorld::fill_circle);
	ClassDB::bind_method(D_METHOD("step", "ticks"), &PixelWorld::step);
	ClassDB::bind_method(D_METHOD("is_chunk_loaded", "chunk"), &PixelWorld::is_chunk_loaded);
	ClassDB::bind_method(D_METHOD("is_area_ready", "rect"), &PixelWorld::is_area_ready);
	ClassDB::bind_method(D_METHOD("get_stats"), &PixelWorld::get_stats);
	ClassDB::bind_method(D_METHOD("spawn_particles", "material", "position", "velocity", "count", "spread"), &PixelWorld::spawn_particles);
	ClassDB::bind_method(D_METHOD("spawn_visual_particles", "color", "position", "velocity", "count", "spread", "lifetime", "gravity_scale", "collides"), &PixelWorld::spawn_visual_particles, DEFVAL(1.0f), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("explode", "position", "radius", "power"), &PixelWorld::explode);
	ClassDB::bind_method(D_METHOD("raycast", "from", "to", "mask"), &PixelWorld::raycast, DEFVAL(RAY_SOLID | RAY_POWDER | RAY_BODIES));
	BIND_CONSTANT(RAY_SOLID);
	BIND_CONSTANT(RAY_POWDER);
	BIND_CONSTANT(RAY_LIQUID);
	BIND_CONSTANT(RAY_GAS);
	BIND_CONSTANT(RAY_BODIES);

	ClassDB::bind_method(D_METHOD("set_material_set", "value"), &PixelWorld::set_material_set);
	ClassDB::bind_method(D_METHOD("get_material_set"), &PixelWorld::get_material_set);
	ClassDB::bind_method(D_METHOD("set_generator", "value"), &PixelWorld::set_generator);
	ClassDB::bind_method(D_METHOD("get_generator"), &PixelWorld::get_generator);
	ClassDB::bind_method(D_METHOD("set_generator_threads", "value"), &PixelWorld::set_generator_threads);
	ClassDB::bind_method(D_METHOD("get_generator_threads"), &PixelWorld::get_generator_threads);
	ClassDB::bind_method(D_METHOD("set_max_inserts_per_frame", "value"), &PixelWorld::set_max_inserts_per_frame);
	ClassDB::bind_method(D_METHOD("get_max_inserts_per_frame"), &PixelWorld::get_max_inserts_per_frame);
	ClassDB::bind_method(D_METHOD("set_active_radius", "value"), &PixelWorld::set_active_radius);
	ClassDB::bind_method(D_METHOD("get_active_radius"), &PixelWorld::get_active_radius);
	ClassDB::bind_method(D_METHOD("set_loaded_margin", "value"), &PixelWorld::set_loaded_margin);
	ClassDB::bind_method(D_METHOD("get_loaded_margin"), &PixelWorld::get_loaded_margin);
	ClassDB::bind_method(D_METHOD("set_discard_hysteresis", "value"), &PixelWorld::set_discard_hysteresis);
	ClassDB::bind_method(D_METHOD("get_discard_hysteresis"), &PixelWorld::get_discard_hysteresis);
	ClassDB::bind_method(D_METHOD("set_world_seed", "value"), &PixelWorld::set_world_seed);
	ClassDB::bind_method(D_METHOD("get_world_seed"), &PixelWorld::get_world_seed);
	ClassDB::bind_method(D_METHOD("set_worker_threads", "value"), &PixelWorld::set_worker_threads);
	ClassDB::bind_method(D_METHOD("get_worker_threads"), &PixelWorld::get_worker_threads);
	ClassDB::bind_method(D_METHOD("set_follow_target", "value"), &PixelWorld::set_follow_target);
	ClassDB::bind_method(D_METHOD("get_follow_target"), &PixelWorld::get_follow_target);
	ClassDB::bind_method(D_METHOD("set_load_radius", "value"), &PixelWorld::set_load_radius);
	ClassDB::bind_method(D_METHOD("get_load_radius"), &PixelWorld::get_load_radius);
	ClassDB::bind_method(D_METHOD("set_arena", "value"), &PixelWorld::set_arena);
	ClassDB::bind_method(D_METHOD("get_arena"), &PixelWorld::get_arena);
	ClassDB::bind_method(D_METHOD("set_simulating", "value"), &PixelWorld::set_simulating);
	ClassDB::bind_method(D_METHOD("get_simulating"), &PixelWorld::get_simulating);
	ClassDB::bind_method(D_METHOD("set_max_particles", "value"), &PixelWorld::set_max_particles);
	ClassDB::bind_method(D_METHOD("get_max_particles"), &PixelWorld::get_max_particles);
	ClassDB::bind_method(D_METHOD("set_body_gravity", "value"), &PixelWorld::set_body_gravity);
	ClassDB::bind_method(D_METHOD("get_body_gravity"), &PixelWorld::get_body_gravity);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material_set", PROPERTY_HINT_RESOURCE_TYPE, "PixelMaterialSet"), "set_material_set", "get_material_set");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "generator", PROPERTY_HINT_RESOURCE_TYPE, "PixelWorldGenerator"), "set_generator", "get_generator");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "generator_threads"), "set_generator_threads", "get_generator_threads");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_inserts_per_frame"), "set_max_inserts_per_frame", "get_max_inserts_per_frame");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2I, "active_radius"), "set_active_radius", "get_active_radius");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "loaded_margin"), "set_loaded_margin", "get_loaded_margin");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "discard_hysteresis"), "set_discard_hysteresis", "get_discard_hysteresis");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "world_seed"), "set_world_seed", "get_world_seed");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "worker_threads"), "set_worker_threads", "get_worker_threads");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "follow_target"), "set_follow_target", "get_follow_target");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2I, "load_radius"), "set_load_radius", "get_load_radius");
	ADD_PROPERTY(PropertyInfo(Variant::RECT2I, "arena"), "set_arena", "get_arena");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "simulating"), "set_simulating", "get_simulating");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_particles"), "set_max_particles", "get_max_particles");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "body_gravity"), "set_body_gravity", "get_body_gravity");

	ClassDB::bind_method(D_METHOD("set_lighting_enabled", "value"), &PixelWorld::set_lighting_enabled);
	ClassDB::bind_method(D_METHOD("get_lighting_enabled"), &PixelWorld::get_lighting_enabled);
	ClassDB::bind_method(D_METHOD("set_ambient", "value"), &PixelWorld::set_ambient);
	ClassDB::bind_method(D_METHOD("get_ambient"), &PixelWorld::get_ambient);
	ClassDB::bind_method(D_METHOD("set_light_exposure", "value"), &PixelWorld::set_light_exposure);
	ClassDB::bind_method(D_METHOD("get_light_exposure"), &PixelWorld::get_light_exposure);
	ClassDB::bind_method(D_METHOD("set_light_margin", "value"), &PixelWorld::set_light_margin);
	ClassDB::bind_method(D_METHOD("get_light_margin"), &PixelWorld::get_light_margin);
	ClassDB::bind_method(D_METHOD("set_light_resolution", "value"), &PixelWorld::set_light_resolution);
	ClassDB::bind_method(D_METHOD("get_light_resolution"), &PixelWorld::get_light_resolution);
	ClassDB::bind_method(D_METHOD("set_backdrop_color", "value"), &PixelWorld::set_backdrop_color);
	ClassDB::bind_method(D_METHOD("get_backdrop_color"), &PixelWorld::get_backdrop_color);
	ClassDB::bind_method(D_METHOD("set_fire_glow_color", "value"), &PixelWorld::set_fire_glow_color);
	ClassDB::bind_method(D_METHOD("get_fire_glow_color"), &PixelWorld::get_fire_glow_color);
	ClassDB::bind_method(D_METHOD("set_fire_glow_energy", "value"), &PixelWorld::set_fire_glow_energy);
	ClassDB::bind_method(D_METHOD("get_fire_glow_energy"), &PixelWorld::get_fire_glow_energy);
	ClassDB::bind_method(D_METHOD("set_marker_light_color", "value"), &PixelWorld::set_marker_light_color);
	ClassDB::bind_method(D_METHOD("get_marker_light_color"), &PixelWorld::get_marker_light_color);
	ClassDB::bind_method(D_METHOD("set_marker_light_energy", "value"), &PixelWorld::set_marker_light_energy);
	ClassDB::bind_method(D_METHOD("get_marker_light_energy"), &PixelWorld::get_marker_light_energy);
	ClassDB::bind_method(D_METHOD("set_marker_light_radius", "value"), &PixelWorld::set_marker_light_radius);
	ClassDB::bind_method(D_METHOD("get_marker_light_radius"), &PixelWorld::get_marker_light_radius);
	ClassDB::bind_method(D_METHOD("set_bloom_threshold", "value"), &PixelWorld::set_bloom_threshold);
	ClassDB::bind_method(D_METHOD("get_bloom_threshold"), &PixelWorld::get_bloom_threshold);
	ClassDB::bind_method(D_METHOD("set_bloom_strength", "value"), &PixelWorld::set_bloom_strength);
	ClassDB::bind_method(D_METHOD("get_bloom_strength"), &PixelWorld::get_bloom_strength);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "lighting_enabled"), "set_lighting_enabled", "get_lighting_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ambient", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_ambient", "get_ambient");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "light_exposure", PROPERTY_HINT_RANGE, "0,8,0.01,or_greater"), "set_light_exposure", "get_light_exposure");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "light_margin", PROPERTY_HINT_RANGE, "0,4096,1"), "set_light_margin", "get_light_margin");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "light_resolution", PROPERTY_HINT_RANGE, "1,4,1"), "set_light_resolution", "get_light_resolution");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "backdrop_color"), "set_backdrop_color", "get_backdrop_color");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "fire_glow_color"), "set_fire_glow_color", "get_fire_glow_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fire_glow_energy", PROPERTY_HINT_RANGE, "0,16,0.01,or_greater"), "set_fire_glow_energy", "get_fire_glow_energy");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "marker_light_color"), "set_marker_light_color", "get_marker_light_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "marker_light_energy", PROPERTY_HINT_RANGE, "0,16,0.01,or_greater"), "set_marker_light_energy", "get_marker_light_energy");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "marker_light_radius", PROPERTY_HINT_RANGE, "1,1024,0.5,or_greater"), "set_marker_light_radius", "get_marker_light_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bloom_threshold", PROPERTY_HINT_RANGE, "0,16,0.01,or_greater"), "set_bloom_threshold", "get_bloom_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bloom_strength", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_bloom_strength", "get_bloom_strength");
}

void PixelWorld::_ready() {
	if (grid) return;  // _ready runs again if the node re-enters the tree

	if (!material_set.is_valid()) {
		UtilityFunctions::push_error("PixelWorld: material_set is not set; the world is disabled");
		return;
	}
	String error;
	if (!material_set->build_table(table, error)) {
		UtilityFunctions::push_error("PixelWorld: material set does not compile: " + error);
		return;
	}
	if (generator_resource.is_valid()) {
		generator = generator_resource->build(table, (uint32_t)world_seed, error);
		if (!generator) {
			UtilityFunctions::push_error("PixelWorld: generator does not compile: " + error);
			return;
		}
	} else {
		const MatId rock = table.id_of("rock");
		if (rock == NO_MAT) {
			UtilityFunctions::push_error("PixelWorld: material set has no 'rock' material for the arena border");
			return;
		}
		arena_generator = std::make_unique<BoxGenerator>(arena.position.x, arena.position.y,
		                                                 arena.position.x + arena.size.x,
		                                                 arena.position.y + arena.size.y, rock, table);
	}

	palette.resize(table.size());
	for (size_t i = 0; i < table.size(); ++i) {
		const CompiledMaterial &material = table.get((MatId)i);
		palette[i].color = color_from_rgba32(material.color_rgba);
		palette[i].shade_amount = material.shade_amount;
	}
	cell_rgba.assign(palette.size() * 2 * 256, 0u);
	for (size_t m = 0; m < palette.size(); ++m)
		for (int burning = 0; burning < 2; ++burning)
			for (int shade = 0; shade < 256; ++shade) {
				Cell cell{};
				cell.mat = (MatId)m;
				cell.flags = burning ? FLAG_BURNING : 0;
				cell.shade = (uint8_t)shade;
				const Color color = cell_color(cell);
				const uint8_t px[4] = {to_byte(color.r), to_byte(color.g), to_byte(color.b), to_byte(color.a)};
				memcpy(&cell_rgba[(m * 2 + burning) * 256 + shade], px, sizeof(px));
			}
	rebuild_light_palette();

	// The pool must outlive the simulator, which is why the node owns both.
	pool = std::make_unique<JobPool>(worker_threads == -1 ? JobPool::default_workers() : worker_threads);
	grid = std::make_unique<WorldGrid>(table, (uint32_t)world_seed);
	sim = std::make_unique<Simulator>(*grid, pool.get());
	particles = std::make_unique<ParticlePool>(*grid, max_particles);
	create_particle_view();
	physics = std::make_unique<PhysicsWorld>(*grid, body_gravity);
	physics->set_particle_pool(particles.get());
	add_to_group("pixel_world");

	pixels.resize(CHUNK * CHUNK * 4);
	upload_image = Image::create_from_data(CHUNK, CHUNK, false, Image::FORMAT_RGBA8, pixels);

	if (generator) {
		streamer = std::make_unique<ChunkStreamer>(*generator, (uint32_t)world_seed, generator_threads);
		// Fill the active zone before the first frame, so the world is never half-empty up front.
		const Vector2i cell = target_cell();
		const ChunkCoord centre = chunk_of(cell.x, cell.y);
		const ChunkRect active = ChunkRect::around(centre, active_radius.x < 0 ? 0 : active_radius.x,
		                                           active_radius.y < 0 ? 0 : active_radius.y);
		std::vector<ChunkCoord> missing;
		for (int y = active.y0; y <= active.y1; ++y)
			for (int x = active.x0; x <= active.x1; ++x) missing.push_back({x, y});
		streamer->request(missing, centre);
		streamer->wait_idle();
		for (std::unique_ptr<Chunk> &chunk : streamer->take_ready((int)missing.size())) {
			const ChunkCoord coord = chunk->coord;
			if (grid->find(coord)) continue;
			grid->insert(std::move(chunk));
			loaded.push_back(coord);
		}
	} else {
		stream_chunks();
	}
}

void PixelWorld::_process(double) {
	if (!grid) return;
	if (streamer) {
		stream_generated_chunks();
	} else {
		insert_evict_ms = 0.0;
		stream_chunks();
	}
	upload_dirty_chunks();
	draw_particles();
	view_missing = streamer ? count_view_missing() : 0;
	build_light_frame();
}

void PixelWorld::_physics_process(double) {
	if (simulating && sim) tick_world();
}

// Terrain shapes and Box2D first, then the bodies are stamped into the grid for the cell sim
// and the particles (sand piles on them, water flows around them, debris bounces off them
// instead of landing inside them) and lifted out again.
void PixelWorld::tick_world() {
	physics->update_terrain();
	physics->step();
	physics->stamp();
	sim->tick();
	particles->step();
	physics->unstamp();
	sync_bodies();
}

// Moves every registered node to its Box2D pose, then reports hits and departures. Nodes are
// looked up by ObjectID each time: a signal handler may free any of them.
void PixelWorld::sync_bodies() {
	const Transform2D to_global = get_global_transform();
	auto node_of = [&](int p_handle) -> PixelPhysicsNode * {
		if (p_handle < 0 || (size_t)p_handle >= body_nodes.size() || !physics->has_body(p_handle)) return nullptr;
		return Object::cast_to<PixelPhysicsNode>(ObjectDB::get_instance(body_nodes[(size_t)p_handle]));
	};
	for (size_t h = 0; h < body_nodes.size(); ++h) {
		PixelPhysicsNode *node = node_of((int)h);
		if (!node) continue;
		const b2BodyId id = physics->body_id((int)h);
		if (!b2Body_IsEnabled(id)) continue;
		const b2Vec2 p = b2Body_GetPosition(id);
		node->set_global_transform(to_global * Transform2D(b2Rot_GetAngle(b2Body_GetRotation(id)), Vector2(p.x, p.y)));
	}
	const std::vector<BodyHit> hits = physics->hits();  // a copy: handlers may remove bodies
	for (const BodyHit &hit : hits) {
		const Vector2 point = to_global.xform(Vector2(hit.x, hit.y));
		const Vector2 normal = to_global.basis_xform(Vector2(hit.nx, hit.ny)).normalized();
		PixelPhysicsNode *a = node_of(hit.a);
		PixelPhysicsNode *b = node_of(hit.b);
		if (a) a->emit_signal("hit", b ? Variant(b) : Variant(), point, normal, hit.speed);
		a = node_of(hit.a);
		if (b && node_of(hit.b)) b->emit_signal("hit", a ? Variant(a) : Variant(), point, -normal, hit.speed);
	}
	for (int h : physics->take_left_world())
		if (PixelPhysicsNode *node = node_of(h)) node->emit_signal("left_world");
}

int PixelWorld::register_body(PixelRigidBody *p_body) {
	if (!physics) return -1;
	BodyDef def;
	String error;
	if (!p_body->build_def(table, get_global_transform().affine_inverse(), def, error)) {
		UtilityFunctions::push_error("PixelRigidBody " + String(p_body->get_path()) + ": " + error);
		return -1;
	}
	const int handle = physics->add_body(def);
	if (handle < 0) {
		UtilityFunctions::push_error("PixelRigidBody " + String(p_body->get_path()) + ": texture has no opaque pixels");
		return -1;
	}
	if (body_nodes.size() <= (size_t)handle) body_nodes.resize((size_t)handle + 1);
	body_nodes[(size_t)handle] = p_body->get_instance_id();
	return handle;
}

int PixelWorld::register_character(PixelCharacter *p_character) {
	if (!physics) return -1;
	const CreatureDef def = p_character->build_def(get_global_transform().affine_inverse());
	if (def.w <= 0 || def.h <= 0) {
		UtilityFunctions::push_error("PixelCharacter " + String(p_character->get_path()) + ": hitbox_size must be positive");
		return -1;
	}
	const int handle = physics->add_creature(def);
	if (body_nodes.size() <= (size_t)handle) body_nodes.resize((size_t)handle + 1);
	body_nodes[(size_t)handle] = p_character->get_instance_id();
	return handle;
}

void PixelWorld::unregister_body(int p_handle) {
	if (!physics) return;
	physics->remove_body(p_handle);
	if (p_handle >= 0 && (size_t)p_handle < body_nodes.size()) body_nodes[(size_t)p_handle] = ObjectID();
}

void PixelWorld::_notification(int p_what) {
	if (p_what != NOTIFICATION_PREDELETE) return;
	// Bodies and joints may outlive the world: they must forget their Box2D ids first.
	for (const ObjectID &id : body_nodes)
		if (PixelPhysicsNode *node = Object::cast_to<PixelPhysicsNode>(ObjectDB::get_instance(id))) node->detach();
	body_nodes.clear();
	physics.reset();
	RenderingServer *rs = RenderingServer::get_singleton();
	if (!rs) return;
	for (const auto &kv : views) {
		if (kv.second.texture.is_valid()) rs->free_rid(kv.second.texture);
		if (kv.second.canvas_item.is_valid()) rs->free_rid(kv.second.canvas_item);
	}
	views.clear();
	if (particle_canvas_item.is_valid()) rs->free_rid(particle_canvas_item);
	if (particle_multimesh.is_valid()) rs->free_rid(particle_multimesh);
	if (backdrop_canvas_item.is_valid()) rs->free_rid(backdrop_canvas_item);
	particle_canvas_item = RID();
	particle_multimesh = RID();
	backdrop_canvas_item = RID();
	free_light_overlay();
	// The renderer's resources are freed on the render thread, through the same kind of call
	// that uses them; the job keeps the renderer alive until then.
	if (light_renderer) {
		rs->call_on_render_thread(
		    Callable(memnew(LightRenderJob(light_renderer, PackedByteArray(), LightArea(), 0.0f, true))));
		light_renderer.reset();
	}
}

void PixelWorld::create_particle_view() {
	if (max_particles <= 0) return;
	RenderingServer *rs = RenderingServer::get_singleton();
	particle_quad.instantiate();  // 1x1, centred on the instance origin
	particle_multimesh = rs->multimesh_create();
	rs->multimesh_set_mesh(particle_multimesh, particle_quad->get_rid());
	rs->multimesh_allocate_data(particle_multimesh, max_particles, RenderingServer::MULTIMESH_TRANSFORM_2D, true);
	rs->multimesh_set_visible_instances(particle_multimesh, 0);
	particle_canvas_item = rs->canvas_item_create();
	rs->canvas_item_set_parent(particle_canvas_item, get_canvas_item());
	rs->canvas_item_set_z_index(particle_canvas_item, 1);  // above chunk views made later
	rs->canvas_item_add_multimesh(particle_canvas_item, particle_multimesh);
	particle_buffer.resize((int64_t)max_particles * FLOATS_PER_PARTICLE);
}

// Writes every particle into the MultiMesh buffer, snapped to the pixel grid. The buffer must
// be sent whole; visible_instances limits what draws.
void PixelWorld::draw_particles() {
	if (!particle_multimesh.is_valid()) return;
	const std::vector<Particle> &material = particles->material();
	const std::vector<VisualParticle> &visual = particles->visual();
	const int count = (int)(material.size() + visual.size());
	if (count == 0 && drawn_particles == 0) return;

	float *out = particle_buffer.ptrw();
	auto write = [&](float p_x, float p_y, const Color &p_color) {
		out[0] = 1.0f;
		out[1] = 0.0f;
		out[2] = 0.0f;
		out[3] = std::floor(p_x) + 0.5f;
		out[4] = 0.0f;
		out[5] = 1.0f;
		out[6] = 0.0f;
		out[7] = std::floor(p_y) + 0.5f;
		out[8] = p_color.r;
		out[9] = p_color.g;
		out[10] = p_color.b;
		out[11] = p_color.a;
		out += FLOATS_PER_PARTICLE;
	};
	for (const Particle &p : material) write(p.x, p.y, cell_color(p.cell));
	for (const VisualParticle &p : visual) {
		Color color = color_from_rgba32(p.rgba);
		color.a *= (float)p.life / (float)(p.lifetime > 0 ? p.lifetime : 1);
		write(p.x, p.y, color);
	}

	RenderingServer *rs = RenderingServer::get_singleton();
	rs->multimesh_set_buffer(particle_multimesh, particle_buffer);
	rs->multimesh_set_visible_instances(particle_multimesh, count);
	drawn_particles = count;
}

MatId PixelWorld::material_id(const StringName &p_material) const {
	const MatId id = table.id_of(String(p_material).utf8().get_data());
	if (id == NO_MAT) {
		UtilityFunctions::push_error("PixelWorld: unknown material '" + String(p_material) + "'");
	}
	return id;
}

Vector2i PixelWorld::target_cell() const {
	if (!follow_target.is_empty()) {
		const Node2D *target = Object::cast_to<Node2D>(get_node_or_null(follow_target));
		if (target) {
			const Vector2 pos = target->get_global_position();
			return Vector2i((int)std::floor(pos.x), (int)std::floor(pos.y));
		}
	}
	return Vector2i(0, 0);
}

void PixelWorld::stream_chunks() {
	const Vector2i cell = target_cell();
	const ChunkCoord centre = chunk_of(cell.x, cell.y);
	const ChunkRect keep = ChunkRect::around(centre, load_radius.x, load_radius.y);

	for (int dy = -load_radius.y; dy <= load_radius.y; ++dy)
		for (int dx = -load_radius.x; dx <= load_radius.x; ++dx) {
			const ChunkCoord coord{centre.x + dx, centre.y + dy};
			if (grid->find(coord)) continue;
			grid->load(coord, *arena_generator);
			loaded.push_back(coord);
		}

	// Unload what the target has moved away from; the data and its canvas item go with it.
	for (size_t i = 0; i < loaded.size();) {
		if (keep.contains(loaded[i])) {
			++i;
			continue;
		}
		evict(loaded[i]);  // removes the entry, so `i` points at the next candidate
	}
}

// Generator path: active / loaded / discard zones around the follow target, in this order.
void PixelWorld::stream_generated_chunks() {
	const Vector2i cell = target_cell();
	const ChunkCoord centre = chunk_of(cell.x, cell.y);
	// Live values, clamped on every read: a negative radius must never allocate a huge rect.
	const int ax = active_radius.x < 0 ? 0 : active_radius.x;
	const int ay = active_radius.y < 0 ? 0 : active_radius.y;
	const int margin = loaded_margin < 0 ? 0 : loaded_margin;
	const int hysteresis = discard_hysteresis < 0 ? 0 : discard_hysteresis;
	const ChunkRect active = ChunkRect::around(centre, ax, ay);
	const ChunkRect loaded_rect = ChunkRect::around(centre, ax + margin, ay + margin);
	const ChunkRect discard =
	    ChunkRect::around(centre, ax + margin + hysteresis, ay + margin + hysteresis);

	sim->set_active_rect(active);
	physics->set_active_rect(active);

	std::vector<ChunkCoord> missing;
	for (int y = loaded_rect.y0; y <= loaded_rect.y1; ++y)
		for (int x = loaded_rect.x0; x <= loaded_rect.x1; ++x)
			if (!grid->find({x, y})) missing.push_back({x, y});
	streamer->request(missing, centre);

	const auto start = std::chrono::steady_clock::now();
	for (std::unique_ptr<Chunk> &chunk : streamer->take_ready(max_inserts_per_frame)) {
		if (!loaded_rect.contains(chunk->coord)) {  // the target left while it generated
			streamer->forget(chunk->coord);  // else the coord stays delivered and never reloads
			continue;
		}
		const ChunkCoord coord = chunk->coord;
		if (grid->find(coord)) continue;
		grid->insert(std::move(chunk));
		loaded.push_back(coord);
	}
	for (size_t i = 0; i < loaded.size();) {
		if (discard.contains(loaded[i])) {
			++i;
			continue;
		}
		evict(loaded[i]);
	}
	insert_evict_ms = std::chrono::duration<double, std::milli>(
	                      std::chrono::steady_clock::now() - start)
	                      .count();

	const StreamStats stats = streamer->take_stats();
	gen_ms = stats.generated > 0 ? stats.gen_ms_total / stats.generated : 0.0;
	chunks_generated += stats.generated;
}

// The single discard path: unload the chunk, drop its view and forget the coord. This is the
// future persistence hook — a save would read the chunk here, before it is unloaded.
void PixelWorld::evict(ChunkCoord coord) {
	grid->unload(coord);
	drop_view(coord);
	const auto it = std::find(loaded.begin(), loaded.end(), coord);
	if (it != loaded.end()) loaded.erase(it);
}

// Viewport pixels -> canvas -> this node's space, so the visible rect is in world cells.
bool PixelWorld::visible_rect(Rect2 &r_rect) const {
	const Viewport *viewport = get_viewport();
	if (!viewport) return false;
	const Transform2D view_to_local =
	    get_global_transform().affine_inverse() * get_canvas_transform().affine_inverse();
	r_rect = view_to_local.xform(viewport->get_visible_rect());
	return true;
}

int PixelWorld::count_view_missing() const {
	Rect2 visible;
	if (!visible_rect(visible)) return 0;
	const int left = (int)std::floor(visible.position.x);
	const int top = (int)std::floor(visible.position.y);
	const int right = (int)std::ceil(visible.position.x + visible.size.x) - 1;
	const int bottom = (int)std::ceil(visible.position.y + visible.size.y) - 1;
	const ChunkCoord c0 = chunk_of(left, top);
	const ChunkCoord c1 = chunk_of(right, bottom);
	int missing = 0;
	for (int y = c0.y; y <= c1.y; ++y)
		for (int x = c0.x; x <= c1.x; ++x)
			if (!grid->find({x, y})) ++missing;
	return missing;
}

void PixelWorld::rebuild_light_palette() {
	if (table.size() == 0) return;  // the table is compiled in _ready, which rebuilds it
	light_palette = LightPalette::build(table, fire_glow_color.r, fire_glow_color.g,
	                                    fire_glow_color.b, fire_glow_energy);
}

void PixelWorld::reset_light_stats() {
	light_cpu_ms = 0.0;
	light_gpu_ms = 0.0;
	light_spots = 0;
	light_ran_this_frame = false;
}

// One RGBA8 texel per lit-area cell (per light_resolution x light_resolution cells above 1) from
// the grid the physics frame just ticked, then the game lights and room markers on top. Only the
// texels of chunks that changed since the last lit frame are rebuilt into the base, and only the
// texels the last frame's spots wrote are restored from it. --headless has no RenderingDevice:
// lighting is then a no-op with zeroed stats, not an error.
void PixelWorld::build_light_frame() {
	// Drained before any early return: a return that leaves the coords queued would grow the grid's
	// vector without bound (the area cap's zoom-out path returns on most frames). Every path below
	// that drops them also invalidates the base, so the next lit frame rebuilds the whole area.
	const std::vector<ChunkCoord> unloaded = grid->take_light_unloaded();
	RenderingServer *rs = RenderingServer::get_singleton();
	Rect2 visible;
	if (!lighting_enabled || !rs || !rs->get_rendering_device() || !visible_rect(visible)) {
		reset_light_stats();
		hide_light_backdrop();
		hide_light_overlay();
		light_base_area = LightArea();
		return;
	}
	// Pass the view as a rect with a snapped size and an aligned origin: the lit area's size then
	// only changes when the view does (zoom, resize), so scrolling never has to hide the overlay
	// for a frame, while the margin still grows it out to snapped boundaries.
	const float snap = (float)LIGHT_SNAP;
	const int vis_w = 2 * LIGHT_SNAP * (int)std::ceil(visible.size.x / (2.0f * LIGHT_SNAP));
	const int vis_h = 2 * LIGHT_SNAP * (int)std::ceil(visible.size.y / (2.0f * LIGHT_SNAP));
	const float vis_x = std::floor((visible.position.x + visible.size.x * 0.5f) / snap) * snap - vis_w * 0.5f;
	const float vis_y = std::floor((visible.position.y + visible.size.y * 0.5f) / snap) * snap - vis_h * 0.5f;
	LightArea area = light_area(vis_x, vis_y, (float)vis_w, (float)vis_h, light_margin, LIGHT_SNAP);
	// The cap stays in cells: the input buffer is cells x 4 bytes at scale 1 and smaller above it.
	if (!light_area_fits(area)) {
		reset_light_stats();
		hide_light_backdrop();
		hide_light_overlay();
		light_base_area = LightArea();  // the drained unloaded coords are dropped: repaint in full
		return;
	}
	area.scale = light_resolution;

	light_area_now = area;
	const int texel_w = light_texel_w(area);
	const int texel_h = light_texel_h(area);
	const int64_t bytes = (int64_t)texel_w * texel_h * 4;
	light_texels.resize(bytes);
	light_base.resize(bytes);
	if (light_texels.size() != bytes || light_base.size() != bytes) {  // never write through null
		reset_light_stats();
		hide_light_backdrop();
		hide_light_overlay();
		light_base_area = LightArea();  // the drained unloaded coords are dropped: repaint in full
		return;
	}

	const auto start = std::chrono::steady_clock::now();
	LightTexel *texels = reinterpret_cast<LightTexel *>(light_texels.ptrw());
	LightTexel *base = reinterpret_cast<LightTexel *>(light_base.ptrw());
	const bool area_changed = light_base_area.x0 != area.x0 || light_base_area.y0 != area.y0 ||
	                          light_base_area.w != area.w || light_base_area.h != area.h ||
	                          light_base_area.scale != area.scale;
	// The texels of the chunks that changed since the last lit frame, coalesced into x-runs per
	// chunk row: a frame that touches many chunks then costs a few region builds, not one pool
	// handshake per chunk.
	std::vector<LightRect> repainted;
	if (!area_changed) {
		std::vector<ChunkCoord> dirty = unloaded;
		grid->for_each_chunk([&](Chunk &chunk) {
			if (chunk.light_dirty.exchange(false)) dirty.push_back(chunk.coord);
		});
		std::sort(dirty.begin(), dirty.end(), [](ChunkCoord a, ChunkCoord b) {
			return a.y != b.y ? a.y < b.y : a.x < b.x;
		});
		dirty.erase(std::unique(dirty.begin(), dirty.end(),
		                        [](ChunkCoord a, ChunkCoord b) { return a.x == b.x && a.y == b.y; }),
		            dirty.end());
		for (size_t i = 0; i < dirty.size();) {
			size_t j = i + 1;
			while (j < dirty.size() && dirty[j].y == dirty[i].y && dirty[j].x == dirty[j - 1].x + 1)
				++j;
			// Whole chunk cells: an unloaded chunk's cells then read as absent and go dark.
			const LightRect rect =
			    light_region_rect(area, dirty[i].x * CHUNK, dirty[i].y * CHUNK,
			                      (dirty[j - 1].x + 1) * CHUNK, (dirty[i].y + 1) * CHUNK);
			if (rect.w > 0 && rect.h > 0) repainted.push_back(rect);
			i = j;
		}
	}
	if (area_changed) {
		// A different area on any of its five fields (origin, width, height, scale) falls back to the
		// whole-area build, which is also what a never-built base hits: its remembered area starts
		// zeroed. The first frame after a change must show the new terrain.
		build_light_inputs(*grid, light_palette, area, base, pool.get());
		grid->for_each_chunk([](Chunk &chunk) { chunk.light_dirty.store(false); });
		light_spot_rects.clear();
		memcpy(texels, base, (size_t)bytes);
		light_base_area = area;
	} else {
		build_light_rects(*grid, light_palette, area, repainted, base, pool.get());
		for (const LightRect &rect : repainted)
			for (int ty = rect.y; ty < rect.y + rect.h; ++ty)
				memcpy(texels + (size_t)ty * texel_w + rect.x,
				       base + (size_t)ty * texel_w + rect.x, (size_t)rect.w * sizeof(LightTexel));
		// Undo the last frame's spots before adding this frame's, instead of copying the base.
		restore_light_rects(area, light_spot_rects, base, texels);
	}

	std::vector<LightSpot> spots;
	const double time = (double)Time::get_singleton()->get_ticks_msec() / 1000.0;
	const TypedArray<Node> lights = get_tree()->get_nodes_in_group("pixel_light");
	for (int i = 0; i < lights.size(); ++i) {
		const PixelLight *light = Object::cast_to<PixelLight>(lights[i]);
		if (!light || !light->get_enabled()) continue;
		const Vector2 at = to_local(light->get_global_position());
		const Color color = light->get_color();
		spots.push_back(LightSpot{at.x, at.y, color.r, color.g, color.b,
		                          light->current_energy(time), light->get_radius()});
	}
	const LightSpot marker{0.0f, 0.0f, marker_light_color.r, marker_light_color.g,
	                       marker_light_color.b, marker_light_energy, marker_light_radius};
	collect_marker_lights(*grid, area, marker, spots);
	add_light_spots(area, spots, texels);
	light_spot_rects = spot_rects(area, spots);

	light_spots = (int)spots.size();
	// The build, the repaints and the spots are all of the input budget. The job stores the same
	// copy-on-write buffer (there is no packed copy): a ptrw() next frame detaches it into a fresh
	// copy if the render thread has not consumed this one.
	light_cpu_ms =
	    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	light_ran_this_frame = true;
	update_light_backdrop(area);
	render_light_frame(area, light_texels);
	light_gpu_ms = light_renderer ? light_renderer->gpu_ms() : 0.0;
}

// The air between the chunks needs something to light: one rect behind everything, redrawn
// only when the lit area or its colour changes.
void PixelWorld::update_light_backdrop(const LightArea &p_area) {
	RenderingServer *rs = RenderingServer::get_singleton();
	if (!rs) return;
	if (!backdrop_canvas_item.is_valid()) {
		backdrop_canvas_item = rs->canvas_item_create();
		rs->canvas_item_set_parent(backdrop_canvas_item, get_canvas_item());
		rs->canvas_item_set_z_index(backdrop_canvas_item, -4096);
		rs->canvas_item_set_z_as_relative_to_parent(backdrop_canvas_item, false);
		backdrop_painted = false;
	}
	if (!backdrop_painted || p_area.x0 != backdrop_area.x0 || p_area.y0 != backdrop_area.y0 ||
	    p_area.w != backdrop_area.w || p_area.h != backdrop_area.h ||
	    backdrop_color != backdrop_painted_color) {
		rs->canvas_item_clear(backdrop_canvas_item);
		rs->canvas_item_add_rect(backdrop_canvas_item,
		                         Rect2((float)p_area.x0, (float)p_area.y0, (float)p_area.w, (float)p_area.h),
		                         backdrop_color);
		backdrop_area = p_area;
		backdrop_painted_color = backdrop_color;
		backdrop_painted = true;
	}
	if (!backdrop_visible) {
		rs->canvas_item_set_visible(backdrop_canvas_item, true);
		backdrop_visible = true;
	}
}

void PixelWorld::hide_light_backdrop() {
	if (!backdrop_visible) return;
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs && backdrop_canvas_item.is_valid()) rs->canvas_item_set_visible(backdrop_canvas_item, false);
	backdrop_visible = false;
}

// Queues one frame of the GPU pass. The job holds the renderer by shared_ptr and its own copy of
// the texels, so a queued call stays valid even if this node dies first.
void PixelWorld::render_light_frame(const LightArea &p_area, const PackedByteArray &p_texels) {
	RenderingServer *rs = RenderingServer::get_singleton();
	if (!rs) return;
	if (light_overlay_broken) {
		hide_light_overlay();
		return;
	}
	if (!light_renderer) light_renderer = std::make_shared<LightRenderer>();
	if (light_renderer->state() == LightRenderer::STATE_FAILED) {
		light_overlay_broken = true;
		hide_light_overlay();
		return;
	}
	// The job stores the same copy-on-write buffer as light_texels: the next frame's ptrw() detaches
	// (copies) it if the render thread has not consumed this one, so the two never share storage.
	rs->call_on_render_thread(
	    Callable(memnew(LightRenderJob(light_renderer, p_texels, p_area, bloom_threshold, false))));
	update_light_overlay(p_area);
}

// Two canvas items over the lit area: one multiplies by ambient + light, one adds bloom. The
// texture wrappers are re-pointed when the render thread recreates them, and the overlay is
// hidden until the renderer has rendered this exact area, so a stale texture is never stretched
// over a new area.
void PixelWorld::update_light_overlay(const LightArea &p_area) {
	RenderingServer *rs = RenderingServer::get_singleton();
	if (!rs || !light_renderer || p_area.scale < 1) return;

	if (!light_texture.is_valid()) {
		light_texture.instantiate();
		bloom_texture.instantiate();
		light_shader = rs->shader_create();
		rs->shader_set_code(light_shader, LIGHT_OVERLAY_SHADER);
		light_material = rs->material_create();
		rs->material_set_shader(light_material, light_shader);
		bloom_shader = rs->shader_create();
		rs->shader_set_code(bloom_shader, BLOOM_OVERLAY_SHADER);
		bloom_material = rs->material_create();
		rs->material_set_shader(bloom_material, bloom_shader);
		for (bool bloom : { false, true }) {
			const RID item = rs->canvas_item_create();
			rs->canvas_item_set_parent(item, get_canvas_item());
			rs->canvas_item_set_z_index(item, bloom ? 4001 : 4000);
			rs->canvas_item_set_z_as_relative_to_parent(item, false);
			rs->canvas_item_set_default_texture_filter(item, RenderingServer::CANVAS_ITEM_TEXTURE_FILTER_LINEAR);
			rs->canvas_item_set_material(item, bloom ? bloom_material : light_material);
			rs->canvas_item_set_visible(item, false);
			if (bloom) {
				bloom_canvas_item = item;
			} else {
				light_canvas_item = item;
			}
		}
	}

	// Read the rendered texel size before the generation: the area is published last, so a
	// generation read after it sees the textures that texel size was rendered into (no stale
	// texture is wrapped).
	const int texel_w = light_texel_w(p_area);
	const int texel_h = light_texel_h(p_area);
	const bool rendered = light_renderer->has_rendered_area(texel_w, texel_h, p_area.scale);
	const int generation = light_renderer->texture_generation();
	const bool new_textures = generation != light_texture_generation;
	if (new_textures) {
		light_texture_generation = generation;
		if (generation > 0) {
			light_texture->set_texture_rd_rid(light_renderer->light_rd());
			bloom_texture->set_texture_rd_rid(light_renderer->bloom_rd());
		}
	}

	// The rects map the lit area in world cells onto the textures: the world-local rect times the
	// canvas transform is one texel per light texel (light_resolution cells), so the overlay itself
	// is what scales the coarser texture back up.
	const auto rebuild_rects = [&]() {
		const Rect2 rect((float)p_area.x0, (float)p_area.y0, (float)p_area.w, (float)p_area.h);
		const Rect2 light_region(0.0f, 0.0f, (float)texel_w, (float)texel_h);
		// The bloom texture is the light output at a quarter resolution.
		const Rect2 bloom_region(0.0f, 0.0f, (float)((texel_w + 3) / 4), (float)((texel_h + 3) / 4));
		rs->canvas_item_clear(light_canvas_item);
		rs->canvas_item_add_texture_rect_region(light_canvas_item, rect, light_texture->get_rid(), light_region);
		rs->canvas_item_clear(bloom_canvas_item);
		rs->canvas_item_add_texture_rect_region(bloom_canvas_item, rect, bloom_texture->get_rid(), bloom_region);
	};

	// Show only a texture the render thread rendered for this exact area: a resize or zoom is
	// hidden until the renderer has rendered this exact area, and a stale texture can never be
	// stretched over a different area because the render is what makes the areas match (one
	// atomic check, no frame counting).
	if (generation <= 0 || !rendered) {
		hide_light_overlay();
		return;
	}
	if (new_textures || p_area.x0 != light_overlay_area.x0 || p_area.y0 != light_overlay_area.y0 ||
	    p_area.w != light_overlay_area.w || p_area.h != light_overlay_area.h ||
	    p_area.scale != light_overlay_area.scale) {
		light_overlay_area = p_area;
		rebuild_rects();
	}

	rs->material_set_param(light_material, "ambient", ambient);
	rs->material_set_param(light_material, "light_exposure", light_exposure);
	rs->material_set_param(bloom_material, "bloom_strength", bloom_strength);
	if (!light_overlay_visible) {
		rs->canvas_item_set_visible(light_canvas_item, true);
		rs->canvas_item_set_visible(bloom_canvas_item, true);
		light_overlay_visible = true;
	}
}

void PixelWorld::hide_light_overlay() {
	if (!light_overlay_visible) return;
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs) {
		if (light_canvas_item.is_valid()) rs->canvas_item_set_visible(light_canvas_item, false);
		if (bloom_canvas_item.is_valid()) rs->canvas_item_set_visible(bloom_canvas_item, false);
	}
	light_overlay_visible = false;
}

void PixelWorld::free_light_overlay() {
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs) {
		for (RID *item : { &light_canvas_item, &bloom_canvas_item, &light_material, &bloom_material,
		                   &light_shader, &bloom_shader })
			if (item->is_valid()) rs->free_rid(*item);
	}
	light_canvas_item = RID();
	bloom_canvas_item = RID();
	light_material = RID();
	bloom_material = RID();
	light_shader = RID();
	bloom_shader = RID();
	if (light_texture.is_valid()) light_texture->set_texture_rd_rid(RID());
	if (bloom_texture.is_valid()) bloom_texture->set_texture_rd_rid(RID());
	light_texture.unref();
	bloom_texture.unref();
	light_overlay_visible = false;
	light_texture_generation = -1;
}

void PixelWorld::upload_dirty_chunks() {
	RenderingServer *rs = RenderingServer::get_singleton();
	const auto start = std::chrono::steady_clock::now();
	double first_upload = 0.0;

	upload_chunks.clear();
	grid->for_each_chunk([&](Chunk &chunk) {
		if (chunk.render_dirty.exchange(false)) upload_chunks.push_back(&chunk);
	});
	// The colour conversion is most of the upload (about 160 dirty chunks a frame in bench_world)
	// and the chunks are independent, so it runs over the pool; the RenderingServer calls stay on
	// this thread.
	const size_t chunk_texels = (size_t)CHUNK * CHUNK;
	upload_pixels.resize(upload_chunks.size() * chunk_texels);
	if (!upload_chunks.empty()) {
		pool->parallel_for((int)upload_chunks.size(), [&](int i) {
			write_chunk_image(*upload_chunks[(size_t)i], &upload_pixels[(size_t)i * chunk_texels]);
		});
	}

	for (size_t i = 0; i < upload_chunks.size(); ++i) {
		const Chunk &chunk = *upload_chunks[i];
		memcpy(upload_image->ptrw(), &upload_pixels[i * chunk_texels], chunk_texels * sizeof(uint32_t));

		ChunkView &view = view_for(chunk.coord);
		if (view.has_texture) {
			rs->texture_2d_update(view.texture, upload_image, 0);
		} else {
			const auto create_start = std::chrono::steady_clock::now();
			view.texture = rs->texture_2d_create(upload_image);
			const Vector2 origin((float)(chunk.coord.x * CHUNK), (float)(chunk.coord.y * CHUNK));
			rs->canvas_item_add_texture_rect(view.canvas_item, Rect2(origin, Vector2(CHUNK, CHUNK)), view.texture);
			view.has_texture = true;
			first_upload +=
			    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - create_start)
			        .count();
		}
	}
	first_upload_ms = first_upload;

	const auto elapsed = std::chrono::steady_clock::now() - start;
	upload_ms = (double)std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() / 1000.0;
}

// Shared by chunk pixels and material particles. Material ids are data-derived, so this
// bounds them itself instead of trusting them: an id past the palette draws nothing rather
// than reading past its end.
Color PixelWorld::cell_color(const Cell &p_cell) const {
	if (p_cell.mat == MAT_AIR || p_cell.mat >= palette.size()) return Color(0, 0, 0, 0);
	const PaletteEntry &entry = palette[p_cell.mat];
	const float scale = 1.0f + entry.shade_amount * ((float)p_cell.shade / 255.0f - 0.5f);
	Color color(entry.color.r * scale, entry.color.g * scale, entry.color.b * scale, entry.color.a);
	if (p_cell.flags & FLAG_BURNING) {  // half-way towards a glowing orange
		color.r = Math::lerp(color.r, 1.0f, 0.5f);
		color.g = Math::lerp(color.g, 0.5f, 0.5f);
		color.b = Math::lerp(color.b, 0.1f, 0.5f);
	}
	return color;
}

// Runs on the pool: reads only the chunk and cell_rgba. An id past the palette draws nothing,
// like cell_color().
void PixelWorld::write_chunk_image(const Chunk &chunk, uint32_t *out) const {
	const size_t mats = palette.size();
	for (size_t i = 0; i < (size_t)CHUNK * CHUNK; ++i) {
		const Cell &cell = chunk.cells[i];
		out[i] = cell.mat < mats ? cell_rgba[((size_t)cell.mat * 2 + (cell.flags & FLAG_BURNING)) * 256 + cell.shade] : 0u;
	}
}

PixelWorld::ChunkView &PixelWorld::view_for(ChunkCoord coord) {
	auto it = views.find(chunk_key(coord));
	if (it != views.end()) return it->second;

	RenderingServer *rs = RenderingServer::get_singleton();
	ChunkView view;
	view.canvas_item = rs->canvas_item_create();
	rs->canvas_item_set_parent(view.canvas_item, get_canvas_item());
	return views.emplace(chunk_key(coord), view).first->second;
}

void PixelWorld::drop_view(ChunkCoord coord) {
	auto it = views.find(chunk_key(coord));
	if (it == views.end()) return;
	RenderingServer *rs = RenderingServer::get_singleton();
	if (it->second.texture.is_valid()) rs->free_rid(it->second.texture);
	if (it->second.canvas_item.is_valid()) rs->free_rid(it->second.canvas_item);
	views.erase(it);
}

bool PixelWorld::set_cell(const Vector2i &p_pos, const StringName &p_material) {
	if (!grid) return false;
	const MatId id = material_id(p_material);
	if (id == NO_MAT) return false;
	return grid->set(p_pos.x, p_pos.y, id);
}

StringName PixelWorld::get_material_at(const Vector2i &p_pos) const {
	if (!grid) return StringName();
	Cell cell{};
	if (!grid->get(p_pos.x, p_pos.y, cell)) return StringName();
	return StringName(table.name_of(cell.mat).c_str());
}

int PixelWorld::fill_rect(const Rect2i &p_rect, const StringName &p_material) {
	if (!grid) return 0;
	const MatId id = material_id(p_material);
	if (id == NO_MAT) return 0;

	int written = 0;
	for (int y = p_rect.position.y; y < p_rect.position.y + p_rect.size.y; ++y)
		for (int x = p_rect.position.x; x < p_rect.position.x + p_rect.size.x; ++x)
			if (grid->set(x, y, id)) ++written;
	return written;
}

int PixelWorld::fill_circle(const Vector2i &p_center, int p_radius, const StringName &p_material) {
	if (!grid) return 0;
	const MatId id = material_id(p_material);
	if (id == NO_MAT) return 0;

	int written = 0;
	const int radius_squared = p_radius * p_radius;
	for (int dy = -p_radius; dy <= p_radius; ++dy)
		for (int dx = -p_radius; dx <= p_radius; ++dx)
			if (dx * dx + dy * dy <= radius_squared && grid->set(p_center.x + dx, p_center.y + dy, id)) ++written;
	return written;
}

void PixelWorld::step(int p_ticks) {
	if (!sim) return;
	for (int i = 0; i < p_ticks; ++i) tick_world();
}

// `p_velocity` rotated by a uniform angle in [-spread/2, spread/2] and scaled by [0.75, 1.25],
// in cells per tick. Hashed from the tick and index, so the same inputs repeat.
Vector2 PixelWorld::jittered(const Vector2 &p_velocity, float p_spread, int p_index, int p_count) const {
	const uint32_t tick = sim->tick_count();
	const float angle = (hash_unit(hash5((uint32_t)world_seed, tick, p_index, p_count, SALT_SPREAD)) - 0.5f) * p_spread;
	const float scale = 0.75f + 0.5f * hash_unit(hash5((uint32_t)world_seed, tick, p_index, p_count, SALT_SCALE));
	return p_velocity.rotated(angle) * (scale / TICKS_PER_SECOND);
}

int PixelWorld::spawn_particles(const StringName &p_material, const Vector2 &p_position, const Vector2 &p_velocity,
                                int p_count, float p_spread) {
	if (!particles || p_count <= 0) return 0;
	if (!p_position.is_finite()) {
		UtilityFunctions::push_error("PixelWorld.spawn_particles: position is not finite");
		return 0;
	}
	const MatId id = material_id(p_material);
	if (id == NO_MAT) return 0;
	const Cell cell = spawn_cell(id, table, (uint32_t)world_seed, (int)std::floor(p_position.x),
	                             (int)std::floor(p_position.y));
	const int count = std::min(p_count, max_particles);  // the rest could never pool
	int pooled = 0;
	for (int i = 0; i < count; ++i) {
		const Vector2 v = jittered(p_velocity, p_spread, i, count);
		if (particles->emit(p_position.x, p_position.y, v.x, v.y, cell)) ++pooled;
	}
	return pooled;
}

int PixelWorld::spawn_visual_particles(const Color &p_color, const Vector2 &p_position, const Vector2 &p_velocity,
                                       int p_count, float p_spread, float p_lifetime, float p_gravity_scale,
                                       bool p_collides) {
	if (!particles || p_count <= 0) return 0;
	if (!p_position.is_finite()) {
		UtilityFunctions::push_error("PixelWorld.spawn_visual_particles: position is not finite");
		return 0;
	}
	const float ticks = std::isfinite(p_lifetime) ? std::round(p_lifetime * TICKS_PER_SECOND) : 1.0f;
	VisualParticle p;
	p.x = p_position.x;
	p.y = p_position.y;
	p.rgba = p_color.to_rgba32();
	p.lifetime = p.life = (uint16_t)CLAMP(ticks, 1.0f, 65535.0f);
	p.gravity_scale = p_gravity_scale;
	p.collides = p_collides;
	const int count = std::min(p_count, max_particles);
	int pooled = 0;
	for (int i = 0; i < count; ++i) {
		const Vector2 v = jittered(p_velocity, p_spread, i, count);
		p.vx = v.x;
		p.vy = v.y;
		if (particles->emit_visual(p)) ++pooled;
	}
	return pooled;
}

int PixelWorld::explode(const Vector2i &p_position, int p_radius, float p_power) {
	if (!particles) return 0;
	if (p_radius > MAX_EXPLODE_RADIUS) {
		UtilityFunctions::push_error(vformat("PixelWorld.explode: radius %d is over the limit of %d", p_radius, MAX_EXPLODE_RADIUS));
		return 0;
	}
	return ::explode(*grid, *particles, p_position.x, p_position.y, p_radius, p_power / TICKS_PER_SECOND,
	                 sim->tick_count())
	    .removed;
}

Dictionary PixelWorld::raycast(const Vector2 &p_from, const Vector2 &p_to, int p_mask) const {
	Dictionary out;
	if (!grid || !p_from.is_finite() || !p_to.is_finite()) return out;
	const Transform2D to_world = get_global_transform().affine_inverse();
	const Vector2 from = to_world.xform(p_from), to = to_world.xform(p_to);
	const RayHit hit = ::raycast(*grid, from.x, from.y, to.x, to.y, (uint32_t)p_mask & 15u);
	const float grid_distance =
	    hit.hit ? (Vector2(hit.x + 0.5f, hit.y + 0.5f) - from).length() - 0.5f : INFINITY;

	int handle = -1;
	float hx, hy, nx, ny;
	if ((p_mask & RAY_BODIES) && physics && physics->raycast(from.x, from.y, to.x, to.y, handle, hx, hy, nx, ny) &&
	    (Vector2(hx, hy) - from).length() < grid_distance && (size_t)handle < body_nodes.size()) {
		out["position"] = Vector2i((int)std::floor(hx), (int)std::floor(hy));
		out["point"] = get_global_transform().xform(Vector2(hx, hy));
		out["material"] = StringName();
		out["normal"] = Vector2i((int)std::lround(nx), (int)std::lround(ny));
		out["collider"] = ObjectDB::get_instance(body_nodes[(size_t)handle]);
		return out;
	}
	if (!hit.hit) return out;
	out["position"] = Vector2i(hit.x, hit.y);
	out["material"] = hit.unloaded ? StringName() : StringName(table.name_of(hit.mat).c_str());
	out["normal"] = Vector2i(hit.nx, hit.ny);
	out["collider"] = Variant();
	return out;
}

bool PixelWorld::is_chunk_loaded(const Vector2i &p_chunk) const {
	if (!grid) return false;
	return grid->find({p_chunk.x, p_chunk.y}) != nullptr;
}

bool PixelWorld::is_area_ready(const Rect2i &p_rect) const {
	if (p_rect.size.x <= 0 || p_rect.size.y <= 0) return true;
	if (!grid) return false;
	const ChunkCoord c0 = chunk_of(p_rect.position.x, p_rect.position.y);
	const ChunkCoord c1 = chunk_of(p_rect.position.x + p_rect.size.x - 1,
	                               p_rect.position.y + p_rect.size.y - 1);
	for (int y = c0.y; y <= c1.y; ++y)
		for (int x = c0.x; x <= c1.x; ++x)
			if (!grid->find({x, y})) return false;
	return true;
}

Dictionary PixelWorld::get_stats() const {
	Dictionary stats;
	if (sim) {
		const SimStats &last = sim->last_stats();
		stats["sim_ms"] = last.sim_ms;
		stats["chunks_simulated"] = last.chunks_simulated;
		stats["cells_updated"] = last.cells_updated;
		stats["tick"] = (int64_t)sim->tick_count();
	} else {
		stats["sim_ms"] = 0.0;
		stats["chunks_simulated"] = 0;
		stats["cells_updated"] = 0;
		stats["tick"] = (int64_t)0;
	}
	stats["upload_ms"] = upload_ms;
	stats["chunks_loaded"] = (int64_t)loaded.size();
	// The arena path has no streamer: every loaded chunk is active and streaming reads 0.
	stats["chunks_active"] = streamer ? (int64_t)sim->last_stats().chunks_active : (int64_t)loaded.size();
	stats["gen_pending"] = streamer ? streamer->pending() : 0;
	stats["gen_ms"] = streamer ? gen_ms : 0.0;
	stats["chunks_generated"] = streamer ? chunks_generated : (int64_t)0;
	stats["stream_ms"] = streamer ? insert_evict_ms + first_upload_ms : 0.0;
	stats["view_missing"] = streamer ? view_missing : 0;
	stats["particles_material"] = particles ? (int64_t)particles->material().size() : (int64_t)0;
	stats["particles_visual"] = particles ? (int64_t)particles->visual().size() : (int64_t)0;
	stats["particles_dropped"] = particles ? particles->stats().dropped : (int64_t)0;
	stats["particle_ms"] = particles ? particles->stats().particle_ms : 0.0;
	const PhysicsStats physics_stats = physics ? physics->stats() : PhysicsStats{};
	stats["bodies"] = physics_stats.bodies;
	stats["bodies_awake"] = physics_stats.awake;
	stats["terrain_bodies"] = physics_stats.terrain_bodies;
	stats["shape_builds"] = physics_stats.shape_builds;
	stats["physics_ms"] = physics_stats.physics_ms;
	stats["shape_ms"] = physics_stats.shape_ms;
	stats["stamp_ms"] = physics_stats.stamp_ms;
	stats["creatures"] = physics_stats.creatures;
	stats["creature_ms"] = physics_stats.creature_ms;
	stats["light_cpu_ms"] = light_cpu_ms;
	stats["light_gpu_ms"] = light_gpu_ms;
	stats["light_spots"] = light_spots;
	return stats;
}

} // namespace godot
