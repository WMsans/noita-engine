#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/texture2drd.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/rect2i.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include "core/chunk_streamer.h"
#include "core/generator.h"
#include "core/job_pool.h"
#include "core/light_input.h"
#include "core/particles.h"
#include "core/physics.h"
#include "core/raycast.h"
#include "core/simulator.h"
#include "core/terrain.h"
#include "core/world_grid.h"
#include "light_renderer.h"
#include "pixel_material_set.h"
#include "pixel_world_generator.h"

namespace godot {

class PixelCharacter;
class PixelPhysicsNode;
class PixelRigidBody;

// Hosts one box world: streams chunks around the follow target, ticks the simulator once per
// physics frame and draws every chunk as one 64x64 RGBA8 texture. 1 world cell = 1 unit.
class PixelWorld : public Node2D {
	GDCLASS(PixelWorld, Node2D)

	// One per loaded chunk: the canvas item parented to this node's, and its texture RID.
	struct ChunkView {
		RID canvas_item;
		RID texture;
		bool has_texture = false;
	};

	// Per-material render state, resolved once at _ready so the pixel loop stays floats only.
	struct PaletteEntry {
		Color color;
		float shade_amount = 0.0f;
	};

	Ref<PixelMaterialSet> material_set;
	Ref<PixelWorldGenerator> generator_resource;
	int world_seed = 1337;
	int worker_threads = -1;  // -1 = JobPool::default_workers()
	int generator_threads = 2;
	int max_inserts_per_frame = 16;
	Vector2i active_radius = Vector2i(8, 5);
	int loaded_margin = 3;
	int discard_hysteresis = 2;
	// Lighting settings: all live, read every lit frame.
	bool lighting_enabled = true;
	float ambient = 0.05f;
	float light_exposure = 1.0f;
	int light_margin = 240;
	int light_resolution = 1;  // world cells per light texel, 1..4: 1 = full, 2 = half, 4 = quarter
	int light_surface_depth = 10;  // cells of light into solid surfaces, 0..LIGHT_SURFACE_MAX_DEPTH; 0 = off
	Color backdrop_color = Color(0.07f, 0.05f, 0.04f);
	Color fire_glow_color = Color(1.0f, 0.6f, 0.2f);
	float fire_glow_energy = 1.5f;
	Color marker_light_color = Color(1.0f, 0.75f, 0.4f);
	float marker_light_energy = 1.5f;
	float marker_light_radius = 48.0f;
	float bloom_threshold = 1.0f;
	float bloom_strength = 0.6f;
	NodePath follow_target;
	Vector2i load_radius = Vector2i(6, 4);
	Rect2i arena = Rect2i(-512, -256, 1024, 512);
	bool simulating = true;
	int max_particles = DEFAULT_PARTICLE_CAPACITY;
	float body_gravity = 600.0f;  // cells/s^2
	// material_set, generator, world_seed, worker_threads, generator_threads,
	// max_inserts_per_frame, max_particles, body_gravity and arena are read once in _ready (set them before adding the node
	// to the tree); load_radius, follow_target, simulating, active_radius, loaded_margin and
	// discard_hysteresis are live.

	MaterialTable table;
	std::unique_ptr<JobPool> pool;
	std::unique_ptr<WorldGrid> grid;
	std::unique_ptr<Simulator> sim;
	std::unique_ptr<ParticlePool> particles;  // after `grid`, so it is destroyed first
	std::unique_ptr<PhysicsWorld> physics;     // ditto
	std::vector<ObjectID> body_nodes;          // index = body handle
	std::unique_ptr<BoxGenerator> arena_generator;  // holds a reference to `table`
	std::unique_ptr<TerrainGenerator> generator;    // ditto; null on the arena path
	// Declared last so it is destroyed first: its workers join before the generator and the
	// table above die.
	std::unique_ptr<ChunkStreamer> streamer;
	std::vector<ChunkCoord> loaded;
	std::unordered_map<int64_t, ChunkView> views;
	std::vector<PaletteEntry> palette;
	// RGBA8 bytes per (material, burning, shade): the values cell_color() gives, so a chunk upload
	// is one table lookup per cell instead of float colour maths.
	std::vector<uint32_t> cell_rgba;
	PackedByteArray pixels;
	std::vector<Chunk *> upload_chunks;   // this frame's dirty chunks
	std::vector<uint32_t> upload_pixels;  // their RGBA8 pixels, CHUNK * CHUNK per chunk
	// Every particle is one instance of a 1x1 quad in a MultiMesh on its own canvas item.
	Ref<QuadMesh> particle_quad;
	RID particle_multimesh;
	RID particle_canvas_item;
	PackedFloat32Array particle_buffer;  // 12 floats per instance: 2D transform, then colour
	int drawn_particles = 0;
	LightPalette light_palette;  // rebuilt when a fire-glow setting changes
	// This frame's lit area and its RGBA8 inputs (r,g,b emission, a opacity), for the renderer.
	LightArea light_area_now;
	PackedByteArray light_texels;  // upload buffer: base + this frame's spots
	// The same texels with no spots in them, plus the texels the last lit frame's spots wrote, so
	// the next frame can undo them and repaint only what changed.
	PackedByteArray light_base;
	LightArea light_base_area;
	std::vector<LightRect> light_spot_rects;
	bool light_ran_this_frame = false;
	double light_cpu_ms = 0.0;
	double light_gpu_ms = 0.0;  // filled from the GPU timestamps once the light pass exists
	int light_spots = 0;
	RID backdrop_canvas_item;
	LightArea backdrop_area;
	Color backdrop_painted_color;
	bool backdrop_painted = false;
	bool backdrop_visible = false;
	// The light pass: the renderer lives on the render thread, the overlay on this one.
	std::shared_ptr<LightRenderer> light_renderer;
	Ref<Texture2DRD> light_texture;
	Ref<Texture2DRD> bloom_texture;
	RID light_canvas_item;
	RID bloom_canvas_item;
	RID light_shader;
	RID bloom_shader;
	RID light_material;
	RID bloom_material;
	LightArea light_overlay_area;
	int light_texture_generation = -1;
	bool light_overlay_visible = false;
	bool light_overlay_broken = false;
	Ref<Image> upload_image;
	double upload_ms = 0.0;
	double first_upload_ms = 0.0;  // texture creation this frame, part of `stream_ms`
	double insert_evict_ms = 0.0;
	double gen_ms = 0.0;
	int64_t chunks_generated = 0;
	int view_missing = 0;

	MatId material_id(const StringName &p_material) const;
	Color cell_color(const Cell &p_cell) const;
	void tick_world();
	void sync_bodies();
	void create_particle_view();
	void draw_particles();
	Vector2 jittered(const Vector2 &p_velocity, float p_spread, int p_index, int p_count) const;
	Vector2i target_cell() const;
	void stream_chunks();
	void stream_generated_chunks();
	void upload_dirty_chunks();
	void write_chunk_image(const Chunk &chunk, uint32_t *out) const;
	ChunkView &view_for(ChunkCoord coord);
	void drop_view(ChunkCoord coord);
	void evict(ChunkCoord coord);
	int count_view_missing() const;
	bool visible_rect(Rect2 &r_rect) const;
	void rebuild_light_palette();
	void build_light_frame();
	void update_light_backdrop(const LightArea &p_area);
	void hide_light_backdrop();
	void render_light_frame(const LightArea &p_area, const PackedByteArray &p_texels);
	void update_light_overlay(const LightArea &p_area);
	void hide_light_overlay();
	void free_light_overlay();
	void reset_light_stats();

protected:
	static void _bind_methods();
	// Not virtual in Wrapped; GDCLASS picks the derived one up by pointer.
	void _notification(int p_what);

public:
	void _ready() override;
	void _process(double p_delta) override;
	void _physics_process(double p_delta) override;

	void set_material_set(const Ref<PixelMaterialSet> &p_value) { material_set = p_value; }
	Ref<PixelMaterialSet> get_material_set() const { return material_set; }

	void set_generator(const Ref<PixelWorldGenerator> &p_value) { generator_resource = p_value; }
	Ref<PixelWorldGenerator> get_generator() const { return generator_resource; }

	void set_generator_threads(int p_value) { generator_threads = p_value; }
	int get_generator_threads() const { return generator_threads; }

	void set_max_inserts_per_frame(int p_value) { max_inserts_per_frame = p_value; }
	int get_max_inserts_per_frame() const { return max_inserts_per_frame; }

	void set_active_radius(const Vector2i &p_value) { active_radius = p_value; }
	Vector2i get_active_radius() const { return active_radius; }

	void set_loaded_margin(int p_value) { loaded_margin = p_value; }
	int get_loaded_margin() const { return loaded_margin; }

	void set_discard_hysteresis(int p_value) { discard_hysteresis = p_value; }
	int get_discard_hysteresis() const { return discard_hysteresis; }

	void set_world_seed(int p_value) { world_seed = p_value; }
	int get_world_seed() const { return world_seed; }

	void set_worker_threads(int p_value) { worker_threads = p_value; }
	int get_worker_threads() const { return worker_threads; }

	void set_follow_target(const NodePath &p_value) { follow_target = p_value; }
	NodePath get_follow_target() const { return follow_target; }

	void set_load_radius(const Vector2i &p_value) { load_radius = p_value; }
	Vector2i get_load_radius() const { return load_radius; }

	void set_arena(const Rect2i &p_value) { arena = p_value; }
	Rect2i get_arena() const { return arena; }

	void set_simulating(bool p_value) { simulating = p_value; }
	bool get_simulating() const { return simulating; }

	void set_max_particles(int p_value) { max_particles = p_value; }
	int get_max_particles() const { return max_particles; }

	void set_body_gravity(float p_value) { body_gravity = p_value; }
	float get_body_gravity() const { return body_gravity; }

	void set_lighting_enabled(bool p_value) { lighting_enabled = p_value; }
	bool get_lighting_enabled() const { return lighting_enabled; }

	void set_ambient(float p_value) { ambient = p_value; }
	float get_ambient() const { return ambient; }

	void set_light_exposure(float p_value) { light_exposure = p_value; }
	float get_light_exposure() const { return light_exposure; }

	void set_light_margin(int p_value) { light_margin = p_value; }
	int get_light_margin() const { return light_margin; }

	// Clamped into 1..4: a coarser setting is the only thing it can mean.
	void set_light_resolution(int p_value) { light_resolution = p_value < 1 ? 1 : (p_value > 4 ? 4 : p_value); }
	int get_light_resolution() const { return light_resolution; }
	void set_light_surface_depth(int p_value) {
		light_surface_depth = p_value < 0 ? 0 : (p_value > LIGHT_SURFACE_MAX_DEPTH ? LIGHT_SURFACE_MAX_DEPTH : p_value);
	}
	int get_light_surface_depth() const { return light_surface_depth; }

	void set_backdrop_color(const Color &p_value) { backdrop_color = p_value; }
	Color get_backdrop_color() const { return backdrop_color; }

	void set_fire_glow_color(const Color &p_value) {
		fire_glow_color = p_value;
		rebuild_light_palette();
	}
	Color get_fire_glow_color() const { return fire_glow_color; }

	void set_fire_glow_energy(float p_value) {
		fire_glow_energy = p_value;
		rebuild_light_palette();
	}
	float get_fire_glow_energy() const { return fire_glow_energy; }

	void set_marker_light_color(const Color &p_value) { marker_light_color = p_value; }
	Color get_marker_light_color() const { return marker_light_color; }

	void set_marker_light_energy(float p_value) { marker_light_energy = p_value; }
	float get_marker_light_energy() const { return marker_light_energy; }

	void set_marker_light_radius(float p_value) { marker_light_radius = p_value; }
	float get_marker_light_radius() const { return marker_light_radius; }

	void set_bloom_threshold(float p_value) { bloom_threshold = p_value; }
	float get_bloom_threshold() const { return bloom_threshold; }

	void set_bloom_strength(float p_value) { bloom_strength = p_value; }
	float get_bloom_strength() const { return bloom_strength; }

	static constexpr int RAY_BODIES = 16;  // with the core's RAY_SOLID..RAY_GAS

	// For PixelRigidBody and the joints (not bound): null until _ready has run.
	PhysicsWorld *get_physics() const { return physics.get(); }
	int register_body(PixelRigidBody *p_body);  // handle, or -1 after reporting why
	int register_character(PixelCharacter *p_character);
	void unregister_body(int p_handle);  // bodies and characters alike
	StringName material_name(MatId p_id) const { return StringName(table.name_of(p_id).c_str()); }

	bool set_cell(const Vector2i &p_pos, const StringName &p_material);
	StringName get_material_at(const Vector2i &p_pos) const;
	bool is_area_ready(const Rect2i &p_rect) const;
	int fill_rect(const Rect2i &p_rect, const StringName &p_material);
	int fill_circle(const Vector2i &p_center, int p_radius, const StringName &p_material);
	// Ticks `p_ticks` times regardless of `simulating`: calling this while simulating is true
	// double-ticks (the physics frame ticks too).
	void step(int p_ticks);
	bool is_chunk_loaded(const Vector2i &p_chunk) const;
	Dictionary get_stats() const;

	// C++ only, for the light pass: the area and RGBA8 texels built this frame.
	const LightArea &get_light_area_now() const { return light_area_now; }
	const PackedByteArray &get_light_texels() const { return light_texels; }
	bool get_light_ran_this_frame() const { return light_ran_this_frame; }

	// Velocities and power are in cells per second; spread is the full cone angle in radians.
	int spawn_particles(const StringName &p_material, const Vector2 &p_position, const Vector2 &p_velocity,
	                    int p_count, float p_spread);
	int spawn_visual_particles(const Color &p_color, const Vector2 &p_position, const Vector2 &p_velocity,
	                           int p_count, float p_spread, float p_lifetime, float p_gravity_scale = 1.0f,
	                           bool p_collides = true);
	int explode(const Vector2i &p_position, int p_radius, float p_power);
	// Hits on terrain carry "position", "material", "normal" (cells) and a null "collider";
	// hits on a body carry the body as "collider" and the exact "point".
	Dictionary raycast(const Vector2 &p_from, const Vector2 &p_to, int p_mask = RAY_SOLID | RAY_POWDER | RAY_BODIES) const;
};

} // namespace godot
