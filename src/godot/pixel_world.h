#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/rect2i.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include "core/chunk_streamer.h"
#include "core/generator.h"
#include "core/job_pool.h"
#include "core/simulator.h"
#include "core/terrain.h"
#include "core/world_grid.h"
#include "pixel_material_set.h"
#include "pixel_world_generator.h"

namespace godot {

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
	NodePath follow_target;
	Vector2i load_radius = Vector2i(6, 4);
	Rect2i arena = Rect2i(-512, -256, 1024, 512);
	bool simulating = true;
	// material_set, generator, world_seed, worker_threads, generator_threads,
	// max_inserts_per_frame and arena are read once in _ready (set them before adding the node
	// to the tree); load_radius, follow_target, simulating, active_radius, loaded_margin and
	// discard_hysteresis are live.

	MaterialTable table;
	std::unique_ptr<JobPool> pool;
	std::unique_ptr<WorldGrid> grid;
	std::unique_ptr<Simulator> sim;
	std::unique_ptr<BoxGenerator> arena_generator;  // holds a reference to `table`
	std::unique_ptr<TerrainGenerator> generator;    // ditto; null on the arena path
	// Declared last so it is destroyed first: its workers join before the generator and the
	// table above die.
	std::unique_ptr<ChunkStreamer> streamer;
	std::vector<ChunkCoord> loaded;
	std::unordered_map<int64_t, ChunkView> views;
	std::vector<PaletteEntry> palette;
	PackedByteArray pixels;
	Ref<Image> upload_image;
	double upload_ms = 0.0;
	double first_upload_ms = 0.0;  // texture creation this frame, part of `stream_ms`
	double insert_evict_ms = 0.0;
	double gen_ms = 0.0;
	int64_t chunks_generated = 0;
	int view_missing = 0;

	MatId material_id(const StringName &p_material) const;
	Vector2i target_cell() const;
	void stream_chunks();
	void stream_generated_chunks();
	void upload_dirty_chunks();
	void write_chunk_image(const Chunk &chunk, uint8_t *out) const;
	ChunkView &view_for(ChunkCoord coord);
	void drop_view(ChunkCoord coord);
	void evict(ChunkCoord coord);
	int count_view_missing() const;

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
};

} // namespace godot
