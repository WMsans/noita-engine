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

#include "core/generator.h"
#include "core/job_pool.h"
#include "core/simulator.h"
#include "core/world_grid.h"
#include "pixel_material_set.h"

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
	int world_seed = 1337;
	int worker_threads = -1;  // -1 = JobPool::default_workers()
	NodePath follow_target;
	Vector2i load_radius = Vector2i(6, 4);
	Rect2i arena = Rect2i(-512, -256, 1024, 512);
	bool simulating = true;

	MaterialTable table;
	std::unique_ptr<JobPool> pool;
	std::unique_ptr<WorldGrid> grid;
	std::unique_ptr<Simulator> sim;
	std::unique_ptr<BoxGenerator> generator;  // holds a reference to `table`
	std::vector<ChunkCoord> loaded;
	std::unordered_map<int64_t, ChunkView> views;
	std::vector<PaletteEntry> palette;
	PackedByteArray pixels;
	Ref<Image> upload_image;
	double upload_ms = 0.0;

	MatId material_id(const StringName &p_material) const;
	Vector2i target_cell() const;
	void stream_chunks();
	void upload_dirty_chunks();
	void write_chunk_image(const Chunk &chunk, uint8_t *out) const;
	ChunkView &view_for(ChunkCoord coord);
	void drop_view(ChunkCoord coord);

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
	int fill_rect(const Rect2i &p_rect, const StringName &p_material);
	int fill_circle(const Vector2i &p_center, int p_radius, const StringName &p_material);
	void step(int p_ticks);
	bool is_chunk_loaded(const Vector2i &p_chunk) const;
	Dictionary get_stats() const;
};

} // namespace godot
