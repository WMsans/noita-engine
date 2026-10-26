#include "pixel_world.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "core/rng.h"

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
	ClassDB::bind_method(D_METHOD("raycast", "from", "to", "mask"), &PixelWorld::raycast, DEFVAL(RAY_SOLID | RAY_POWDER));
	BIND_CONSTANT(RAY_SOLID);
	BIND_CONSTANT(RAY_POWDER);
	BIND_CONSTANT(RAY_LIQUID);
	BIND_CONSTANT(RAY_GAS);

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

	// The pool must outlive the simulator, which is why the node owns both.
	pool = std::make_unique<JobPool>(worker_threads == -1 ? JobPool::default_workers() : worker_threads);
	grid = std::make_unique<WorldGrid>(table, (uint32_t)world_seed);
	sim = std::make_unique<Simulator>(*grid, pool.get());
	particles = std::make_unique<ParticlePool>(*grid, max_particles);
	create_particle_view();

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
}

void PixelWorld::_physics_process(double) {
	if (simulating && sim) tick_world();
}

void PixelWorld::tick_world() {
	sim->tick();
	particles->step();
}

void PixelWorld::_notification(int p_what) {
	if (p_what != NOTIFICATION_PREDELETE) return;
	RenderingServer *rs = RenderingServer::get_singleton();
	if (!rs) return;
	for (const auto &kv : views) {
		if (kv.second.texture.is_valid()) rs->free_rid(kv.second.texture);
		if (kv.second.canvas_item.is_valid()) rs->free_rid(kv.second.canvas_item);
	}
	views.clear();
	if (particle_canvas_item.is_valid()) rs->free_rid(particle_canvas_item);
	if (particle_multimesh.is_valid()) rs->free_rid(particle_multimesh);
	particle_canvas_item = RID();
	particle_multimesh = RID();
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

int PixelWorld::count_view_missing() const {
	const Viewport *viewport = get_viewport();
	if (!viewport) return 0;
	// Viewport pixels -> canvas -> this node's space, so the visible rect is in world cells.
	const Transform2D view_to_local =
	    get_global_transform().affine_inverse() * get_canvas_transform().affine_inverse();
	const Rect2 visible = view_to_local.xform(viewport->get_visible_rect());
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

void PixelWorld::upload_dirty_chunks() {
	RenderingServer *rs = RenderingServer::get_singleton();
	const auto start = std::chrono::steady_clock::now();
	double first_upload = 0.0;

	grid->for_each_chunk([&](Chunk &chunk) {
		if (!chunk.render_dirty.exchange(false)) return;
		write_chunk_image(chunk, pixels.ptrw());
		upload_image->set_data(CHUNK, CHUNK, false, Image::FORMAT_RGBA8, pixels);

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
	});
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

void PixelWorld::write_chunk_image(const Chunk &chunk, uint8_t *out) const {
	for (int ly = 0; ly < CHUNK; ++ly)
		for (int lx = 0; lx < CHUNK; ++lx) {
			const Color color = cell_color(chunk.cells[(size_t)ly * CHUNK + lx]);
			uint8_t *px = out + ((size_t)ly * CHUNK + lx) * 4;
			px[0] = to_byte(color.r);
			px[1] = to_byte(color.g);
			px[2] = to_byte(color.b);
			px[3] = to_byte(color.a);
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
	const RayHit hit = ::raycast(*grid, p_from.x, p_from.y, p_to.x, p_to.y, (uint32_t)p_mask);
	if (!hit.hit) return out;
	out["position"] = Vector2i(hit.x, hit.y);
	out["material"] = hit.unloaded ? StringName() : StringName(table.name_of(hit.mat).c_str());
	out["normal"] = Vector2i(hit.nx, hit.ny);
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
	return stats;
}

} // namespace godot
