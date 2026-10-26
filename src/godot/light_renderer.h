#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/callable_custom.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/string.hpp>

#include "core/light_input.h"

namespace godot {

class LightRenderer;

// Runs one frame of lighting on the render thread. Holds the renderer through a shared_ptr and
// the texels by value (never the PixelWorld), so freeing the world while this is queued cannot
// leave a dangling call.
class LightRenderJob : public CallableCustom {
public:
	LightRenderJob(std::shared_ptr<LightRenderer> p_renderer, const PackedByteArray &p_texels,
	               const LightArea &p_area, float p_bloom_threshold, bool p_free_only) :
			renderer(std::move(p_renderer)), texels(p_texels), area(p_area),
			bloom_threshold(p_bloom_threshold), free_only(p_free_only) {}

	// The job owns everything it touches, so it is valid without an Object behind it
	// (CallableCustom's default is_valid() looks get_object() up in ObjectDB).
	bool is_valid() const override { return true; }
	uint32_t hash() const override { return (uint32_t)(uintptr_t)this; }
	String get_as_text() const override { return String("LightRenderJob"); }
	ObjectID get_object() const override { return ObjectID(); }
	void call(const Variant **p_arguments, int p_argcount, Variant &r_return_value,
	          GDExtensionCallError &r_call_error) const override;

	static bool compare_equal(const CallableCustom *p_a, const CallableCustom *p_b) { return p_a == p_b; }
	static bool compare_less(const CallableCustom *p_a, const CallableCustom *p_b) { return p_a < p_b; }
	CompareEqualFunc get_compare_equal_func() const override { return compare_equal; }
	CompareLessFunc get_compare_less_func() const override { return compare_less; }

private:
	std::shared_ptr<LightRenderer> renderer;
	PackedByteArray texels;
	LightArea area;
	float bloom_threshold = 1.0f;
	bool free_only = false;
};

// The GPU side of lighting: an RGBA8 emission/opacity input, the radiance cascades, the light
// output and the bloom chain. Every method here runs on the render thread (through
// RenderingServer::call_on_render_thread); PixelWorld owns it through a shared_ptr.
//
// The light and bloom textures are the two the overlay wraps with a Texture2DRD, which Godot
// frees through that wrapper, so a recreated one is retired for a few frames before being freed
// here.
class LightRenderer {
public:
	// Compiles the shaders and creates the sampler. False + error turns lighting off.
	bool init(RenderingDevice *p_rd, String &r_error);
	// (Re)creates the textures when the area outgrows them, uploads the texels, runs the
	// cascades top-down, the light output and the bloom passes, and captures GPU timestamps.
	void render(const PackedByteArray &p_texels, const LightArea &p_area, float p_bloom_threshold);
	// For Texture2DRD on the main thread, after texture_generation() reports a new value.
	RID light_rd() const { return light_texture; }
	RID bloom_rd() const { return bloom_texture; }
	// Bumped whenever light_rd()/bloom_rd() change, so the overlay can re-point and skip a frame.
	int texture_generation() const { return generation.load(std::memory_order_acquire); }
	// Light + bloom GPU milliseconds, from the newest frame whose timestamps were readable.
	double gpu_ms() const { return gpu_ms_now.load(std::memory_order_acquire); }
	void free_all();

	enum State { STATE_UNINITIALISED = 0, STATE_READY = 1, STATE_FAILED = 2 };
	int state() const { return state_now.load(std::memory_order_acquire); }

private:
	struct CascadeParams {
		int32_t area_x, area_y;
		int32_t mip_x, mip_y;
		int32_t upper_x, upper_y;
		int32_t t_start, t_end, rays, has_upper, pad;
	};
	struct MipParams {
		int32_t src_x, src_y;
	};
	struct OutputParams {
		int32_t area_x, area_y;
	};
	struct BloomDownParams {
		int32_t size_x, size_y;
		float threshold, pad;
	};
	struct BloomBlurParams {
		int32_t size_x, size_y;
		int32_t dir_x, dir_y;
	};
	struct RetiredTexture {
		RID rid;
		int frames = 0;
	};

	bool create_textures(int p_w, int p_h);
	bool create_input_texture(int p_w, int p_h);
	bool create_mips();
	void destroy_textures();
	void retire_texture(RID p_rid);
	void free_retired(bool p_all);
	void update_timestamps();

	RenderingDevice *rd = nullptr;
	std::atomic<int> state_now{ STATE_UNINITIALISED };
	std::atomic<int> generation{ 0 };
	std::atomic<double> gpu_ms_now{ 0.0 };

	RID sampler;
	RID mip_shader;
	RID cascade_shader;
	RID output_shader;
	RID bloom_down_shader;
	RID bloom_blur_shader;
	RID mip_pipeline;
	RID cascade_pipeline;
	RID output_pipeline;
	RID bloom_down_pipeline;
	RID bloom_blur_pipeline;

	int area_w = 0;
	int area_h = 0;
	int input_w = 0;
	int input_h = 0;
	int bloom_w = 0;  // the lit area's quarter resolution
	int bloom_h = 0;
	RID input_texture;
	RID mip_textures[LIGHT_CASCADES];  // 1..5: one probe spacing per texel; [0] unused
	RID cascade_textures[2];
	RID light_texture;
	RID bloom_textures[2];
	RID bloom_texture;  // the one holding the final blurred result
	RID mip_sets[LIGHT_CASCADES];      // 1..5: reduce mip i-1 into mip i
	RID cascade_sets[LIGHT_CASCADES];
	RID output_set;
	RID bloom_down_set;
	RID bloom_blur_sets[2];
	std::vector<RetiredTexture> retired;

};

} // namespace godot
