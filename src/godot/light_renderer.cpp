#include "light_renderer.h"

#include <cmath>
#include <cstring>

#include <godot_cpp/classes/rd_sampler_state.hpp>
#include <godot_cpp/classes/rd_shader_source.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "light_shaders.h"

namespace godot {

namespace {

constexpr int WORKGROUP = 8;
// A wrapped texture is freed only after this many further render calls: the main thread's
// Texture2DRD is re-pointed on its own thread, and Godot frees the old shared view there.
constexpr int RETIRE_FRAMES = 3;

int pow4(int p_level) {
	int out = 1;
	for (int i = 0; i < p_level; ++i) out *= 4;
	return out;
}

// Rays per direction quadruple per cascade, capped where the march stops paying for itself.
// shortcut: 64 rays per direction at the finest cascades; raise it if near-field shadow noise
// is worth the extra GPU time.
int rays_for_level(int p_level) {
	if (p_level == 0) return 0;
	return MIN(pow4(p_level - 1), 64);
}

// Cascade `level` covers [start, start + 4^level) cells; the intervals tile [0, 1365).
// shortcut: 1365 cells is the whole reach, which covers the default 992x768 lit area's
// diagonal (1254). Zoomed out further, light farther than that is cut off; more cascades
// (LIGHT_CASCADES) if a game needs it. Probes are spaced in texels, so at light_resolution 2 the
// reach is 2x (2730 world cells) and at 4 it is 4x — coarser light, longer shadows.
int interval_start(int p_level) { return p_level == 0 ? 0 : (pow4(p_level) - 1) / 3; }
int interval_end(int p_level) { return interval_start(p_level) + pow4(p_level) - 1; }

// Cascade `level` (>= 1) reads a mip whose texel is half its probe spacing, 2^(level-1) cells,
// so the near field keeps 1-cell resolution at level 1 and thin walls still block the coarser
// rays.
int mip_shift(int p_level) { return p_level - 1; }
int mip_start(int p_level) {
	const int cells = 1 << mip_shift(p_level);
	return (interval_start(p_level) + cells - 1) / cells;
}
int mip_end(int p_level) { return interval_end(p_level) / (1 << mip_shift(p_level)); }
int mip_size(int p_size, int p_level) { return (p_size + (1 << mip_shift(p_level)) - 1) >> mip_shift(p_level); }

template <typename T>
PackedByteArray push_bytes(const T &p_value) {
	PackedByteArray out;
	out.resize((int64_t)sizeof(T));
	memcpy(out.ptrw(), &p_value, sizeof(T));
	return out;
}

uint32_t groups(int p_size) { return (uint32_t)((p_size + WORKGROUP - 1) / WORKGROUP); }

} // namespace

void LightRenderJob::call(const Variant **p_arguments, int p_argcount, Variant &r_return_value,
                          GDExtensionCallError &r_call_error) const {
	(void)p_arguments;
	(void)p_argcount;
	(void)r_return_value;
	(void)r_call_error;
	if (free_only) {
		renderer->free_all();
	} else {
		renderer->render(texels, area, bloom_threshold, surface_passes);
	}
}

bool LightRenderer::init(RenderingDevice *p_rd, String &r_error) {
	if (state_now.load(std::memory_order_acquire) == STATE_READY) return true;
	rd = p_rd;
	if (rd == nullptr) {
		r_error = "no RenderingDevice";
		state_now.store(STATE_FAILED, std::memory_order_release);
		return false;
	}

	auto compile = [&](const char *p_source, const char *p_name, RID &r_shader, RID &r_pipeline) {
		Ref<RDShaderSource> source;
		source.instantiate();
		source->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
		source->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE, p_source);
		Ref<RDShaderSPIRV> spirv = rd->shader_compile_spirv_from_source(source);
		if (spirv.is_null()) {
			r_error = String(p_name) + ": the shader compiler returned nothing";
			return false;
		}
		const String stage_error = spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
		if (!stage_error.is_empty()) {
			r_error = String(p_name) + ": " + stage_error;
			return false;
		}
		r_shader = rd->shader_create_from_spirv(spirv, p_name);
		if (!r_shader.is_valid()) {
			r_error = String(p_name) + ": shader_create_from_spirv failed";
			return false;
		}
		r_pipeline = rd->compute_pipeline_create(r_shader);
		if (!r_pipeline.is_valid()) {
			r_error = String(p_name) + ": compute_pipeline_create failed";
			return false;
		}
		return true;
	};

	if (!compile(LIGHT_MIP_COMPUTE, "LightMip", mip_shader, mip_pipeline) ||
	    !compile(LIGHT_CASCADE_COMPUTE, "LightCascade", cascade_shader, cascade_pipeline) ||
	    !compile(LIGHT_OUTPUT_COMPUTE, "LightOutput", output_shader, output_pipeline) ||
	    !compile(LIGHT_SURFACE_COMPUTE, "LightSurface", surface_shader, surface_pipeline) ||
	    !compile(BLOOM_DOWN_COMPUTE, "BloomDown", bloom_down_shader, bloom_down_pipeline) ||
	    !compile(BLOOM_BLUR_COMPUTE, "BloomBlur", bloom_blur_shader, bloom_blur_pipeline)) {
		state_now.store(STATE_FAILED, std::memory_order_release);
		return false;
	}

	Ref<RDSamplerState> sampler_state;
	sampler_state.instantiate();
	sampler_state->set_min_filter(RenderingDevice::SAMPLER_FILTER_NEAREST);
	sampler_state->set_mag_filter(RenderingDevice::SAMPLER_FILTER_NEAREST);
	sampler_state->set_repeat_u(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
	sampler_state->set_repeat_v(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
	sampler = rd->sampler_create(sampler_state);
	if (!sampler.is_valid()) {
		r_error = "sampler_create failed";
		state_now.store(STATE_FAILED, std::memory_order_release);
		return false;
	}

	state_now.store(STATE_READY, std::memory_order_release);
	return true;
}

void LightRenderer::retire_texture(RID p_rid) {
	if (!p_rid.is_valid()) return;
	retired.push_back(RetiredTexture{ p_rid, 0 });
}

void LightRenderer::free_retired(bool p_all) {
	for (size_t i = 0; i < retired.size();) {
		RetiredTexture &entry = retired[i];
		if (p_all || entry.frames >= RETIRE_FRAMES) {
			if (rd && entry.rid.is_valid()) rd->free_rid(entry.rid);
			retired[i] = retired.back();
			retired.pop_back();
		} else {
			entry.frames++;
			i++;
		}
	}
}

bool LightRenderer::create_textures(int p_w, int p_h) {
	if (p_w <= 0 || p_h <= 0) return false;
	// The textures the overlay wraps must outlive their old Texture2DRD; the rest can go now.
	retire_texture(light_texture);
	retire_texture(bloom_textures[0]);
	destroy_textures();

	Ref<RDTextureView> view;
	view.instantiate();
	auto make = [&](RenderingDevice::DataFormat p_format, int p_tex_w, int p_tex_h,
	                BitField<RenderingDevice::TextureUsageBits> p_usage) {
		Ref<RDTextureFormat> format;
		format.instantiate();
		format->set_texture_type(RenderingDevice::TEXTURE_TYPE_2D);
		format->set_format(p_format);
		format->set_width((uint32_t)p_tex_w);
		format->set_height((uint32_t)p_tex_h);
		format->set_depth(1);
		format->set_array_layers(1);
		format->set_mipmaps(1);
		format->set_samples(RenderingDevice::TEXTURE_SAMPLES_1);
		format->set_usage_bits(p_usage);
		return rd->texture_create(format, view);
	};

	const BitField<RenderingDevice::TextureUsageBits> sampled_storage =
	    RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT | RenderingDevice::TEXTURE_USAGE_STORAGE_BIT;
	// 2x2 texels per probe hold the probe's four directions.
	cascade_textures[0] = make(RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT, p_w * 2, p_h * 2, sampled_storage);
	cascade_textures[1] = make(RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT, p_w * 2, p_h * 2, sampled_storage);
	light_texture = make(RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT, p_w, p_h, sampled_storage);
	surface_texture = make(RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT, p_w, p_h, sampled_storage);
	const int bloom_tex_w = (p_w + 3) / 4;
	const int bloom_tex_h = (p_h + 3) / 4;
	bloom_textures[0] = make(RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT, bloom_tex_w, bloom_tex_h, sampled_storage);
	bloom_textures[1] = make(RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT, bloom_tex_w, bloom_tex_h, sampled_storage);

	const bool ok = cascade_textures[0].is_valid() && cascade_textures[1].is_valid() &&
	                light_texture.is_valid() && surface_texture.is_valid() && bloom_textures[0].is_valid() &&
	                bloom_textures[1].is_valid();
	if (!ok) return false;

	bloom_texture = bloom_textures[0];

	const int final_parity = (LIGHT_CASCADES - 1) % 2;  // cascade 0 lands in this one

	// The output lands wherever leaves the last surface pass writing light_texture.
	for (int target = 0; target < 2; ++target) {
		Ref<RDUniform> cascade_uniform;
		cascade_uniform.instantiate();
		cascade_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
		cascade_uniform->set_binding(0);
		cascade_uniform->add_id(cascade_textures[final_parity]);
		Ref<RDUniform> light_uniform;
		light_uniform.instantiate();
		light_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
		light_uniform->set_binding(1);
		light_uniform->add_id(target == 0 ? light_texture : surface_texture);
		TypedArray<Ref<RDUniform>> uniforms;
		uniforms.push_back(cascade_uniform);
		uniforms.push_back(light_uniform);
		output_sets[target] = rd->uniform_set_create(uniforms, output_shader, 0);
	}
	{
		Ref<RDUniform> light_uniform;
		light_uniform.instantiate();
		light_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
		light_uniform->set_binding(0);
		light_uniform->add_id(sampler);
		light_uniform->add_id(light_texture);
		Ref<RDUniform> dst_uniform;
		dst_uniform.instantiate();
		dst_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
		dst_uniform->set_binding(1);
		dst_uniform->add_id(bloom_textures[0]);
		TypedArray<Ref<RDUniform>> uniforms;
		uniforms.push_back(light_uniform);
		uniforms.push_back(dst_uniform);
		bloom_down_set = rd->uniform_set_create(uniforms, bloom_down_shader, 0);
	}
	for (int pass = 0; pass < 2; ++pass) {
		Ref<RDUniform> src_uniform;
		src_uniform.instantiate();
		src_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
		src_uniform->set_binding(0);
		src_uniform->add_id(bloom_textures[pass]);
		Ref<RDUniform> dst_uniform;
		dst_uniform.instantiate();
		dst_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
		dst_uniform->set_binding(1);
		dst_uniform->add_id(bloom_textures[1 - pass]);
		TypedArray<Ref<RDUniform>> uniforms;
		uniforms.push_back(src_uniform);
		uniforms.push_back(dst_uniform);
		bloom_blur_sets[pass] = rd->uniform_set_create(uniforms, bloom_blur_shader, 0);
	}

	generation.fetch_add(1, std::memory_order_acq_rel);
	return true;
}

// The input is one texel per light texel of the lit area and must be uploaded whole, so it is
// sized to the area's texel dims. The six cascade sets bind it, so they are rebuilt with it. Only
// called after create_textures(), which freed the old input, its mips and every set that bound
// them.
bool LightRenderer::create_input_texture(int p_w, int p_h) {
	if (rd == nullptr || p_w <= 0 || p_h <= 0) return false;

	Ref<RDTextureFormat> format;
	format.instantiate();
	format->set_texture_type(RenderingDevice::TEXTURE_TYPE_2D);
	format->set_format(RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM);
	format->set_width((uint32_t)p_w);
	format->set_height((uint32_t)p_h);
	format->set_depth(1);
	format->set_array_layers(1);
	format->set_mipmaps(1);
	format->set_samples(RenderingDevice::TEXTURE_SAMPLES_1);
	format->set_usage_bits(RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
	                       RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT);
	Ref<RDTextureView> view;
	view.instantiate();
	input_texture = rd->texture_create(format, view);
	if (!input_texture.is_valid()) return false;
	input_w = p_w;
	input_h = p_h;

	if (!create_mips()) return false;

	// Cascade `level` writes its own size into alternating textures and reads the one above.
	for (int level = 0; level < LIGHT_CASCADES; ++level) {
		const int parity = (LIGHT_CASCADES - 1 - level) % 2;  // level 5 -> 0, level 4 -> 1, ...
		const int upper_parity = 1 - parity;
		Ref<RDUniform> input_uniform;
		input_uniform.instantiate();
		input_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
		input_uniform->set_binding(0);
		input_uniform->add_id(sampler);
		input_uniform->add_id(level <= 1 ? input_texture : mip_textures[level]);
		Ref<RDUniform> dst_uniform;
		dst_uniform.instantiate();
		dst_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
		dst_uniform->set_binding(1);
		dst_uniform->add_id(cascade_textures[parity]);
		Ref<RDUniform> upper_uniform;
		upper_uniform.instantiate();
		upper_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
		upper_uniform->set_binding(2);
		// The top cascade has no upper; a valid texture keeps the set complete.
		upper_uniform->add_id(level == LIGHT_CASCADES - 1 ? cascade_textures[parity] : cascade_textures[upper_parity]);
		TypedArray<Ref<RDUniform>> uniforms;
		uniforms.push_back(input_uniform);
		uniforms.push_back(dst_uniform);
		uniforms.push_back(upper_uniform);
		cascade_sets[level] = rd->uniform_set_create(uniforms, cascade_shader, 0);
	}

	// The surface passes read the input's opacity, so they are built with it.
	for (int dir = 0; dir < 2; ++dir) {
		Ref<RDUniform> input_uniform;
		input_uniform.instantiate();
		input_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
		input_uniform->set_binding(0);
		input_uniform->add_id(sampler);
		input_uniform->add_id(input_texture);
		Ref<RDUniform> src_uniform;
		src_uniform.instantiate();
		src_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
		src_uniform->set_binding(1);
		src_uniform->add_id(dir == 0 ? light_texture : surface_texture);
		Ref<RDUniform> dst_uniform;
		dst_uniform.instantiate();
		dst_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
		dst_uniform->set_binding(2);
		dst_uniform->add_id(dir == 0 ? surface_texture : light_texture);
		TypedArray<Ref<RDUniform>> uniforms;
		uniforms.push_back(input_uniform);
		uniforms.push_back(src_uniform);
		uniforms.push_back(dst_uniform);
		surface_sets[dir] = rd->uniform_set_create(uniforms, surface_shader, 0);
		if (!surface_sets[dir].is_valid()) return false;
	}
	return true;
}

// One reduced copy of the input per cascade above 1: emission averaged, opacity maxed, so
// cascade `i` samples cells no smaller than 2^(i-1).
bool LightRenderer::create_mips() {
	// Mip 1 is the lit-area input itself; mips 2..5 halve from there.
	for (int level = 2; level < LIGHT_CASCADES; ++level) {
		Ref<RDTextureFormat> format;
		format.instantiate();
		format->set_texture_type(RenderingDevice::TEXTURE_TYPE_2D);
		format->set_format(RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM);
		format->set_width((uint32_t)mip_size(input_w, level));
		format->set_height((uint32_t)mip_size(input_h, level));
		format->set_depth(1);
		format->set_array_layers(1);
		format->set_mipmaps(1);
		format->set_samples(RenderingDevice::TEXTURE_SAMPLES_1);
		format->set_usage_bits(RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
		                       RenderingDevice::TEXTURE_USAGE_STORAGE_BIT);
		Ref<RDTextureView> view;
		view.instantiate();
		mip_textures[level] = rd->texture_create(format, view);
		if (!mip_textures[level].is_valid()) return false;

		Ref<RDUniform> src_uniform;
		src_uniform.instantiate();
		src_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
		src_uniform->set_binding(0);
		src_uniform->add_id(sampler);
		src_uniform->add_id(level == 2 ? input_texture : mip_textures[level - 1]);
		Ref<RDUniform> dst_uniform;
		dst_uniform.instantiate();
		dst_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
		dst_uniform->set_binding(1);
		dst_uniform->add_id(mip_textures[level]);
		TypedArray<Ref<RDUniform>> uniforms;
		uniforms.push_back(src_uniform);
		uniforms.push_back(dst_uniform);
		mip_sets[level] = rd->uniform_set_create(uniforms, mip_shader, 0);
		if (!mip_sets[level].is_valid()) return false;
	}
	return true;
}

// Frees everything an area change replaces. Sets before the textures they bind: free_rid frees a
// texture's dependent sets, and the other order prints "Attempted to free invalid ID".
void LightRenderer::destroy_textures() {
	if (rd == nullptr) return;
	for (RID &set : cascade_sets)
		if (set.is_valid()) rd->free_rid(set);
	for (RID &set : mip_sets)
		if (set.is_valid()) rd->free_rid(set);
	for (RID &set : output_sets)
		if (set.is_valid()) rd->free_rid(set);
	for (RID &set : surface_sets)
		if (set.is_valid()) rd->free_rid(set);
	if (bloom_down_set.is_valid()) rd->free_rid(bloom_down_set);
	for (RID &set : bloom_blur_sets)
		if (set.is_valid()) rd->free_rid(set);
	for (RID &set : cascade_sets) set = RID();
	for (RID &set : mip_sets) set = RID();
	for (RID &set : output_sets) set = RID();
	for (RID &set : surface_sets) set = RID();
	bloom_down_set = RID();
	for (RID &set : bloom_blur_sets) set = RID();

	// light_texture and bloom_textures[0] are retired (or freed) by the caller.
	for (RID *texture : { &input_texture, &mip_textures[2], &mip_textures[3], &mip_textures[4],
	                      &mip_textures[5], &cascade_textures[0], &cascade_textures[1],
	                      &surface_texture, &bloom_textures[1] }) {
		if (texture->is_valid()) rd->free_rid(*texture);
		*texture = RID();
	}
	light_texture = RID();
	bloom_textures[0] = RID();
	bloom_textures[1] = RID();
	bloom_texture = RID();
	input_w = 0;
	input_h = 0;
}

void LightRenderer::render(const PackedByteArray &p_texels, const LightArea &p_area, float p_bloom_threshold,
                           int p_surface_passes) {
	if (state_now.load(std::memory_order_acquire) == STATE_UNINITIALISED) {
		// First frame: compile the shaders here, on the render thread, like every other call.
		RenderingServer *rs = RenderingServer::get_singleton();
		String error;
		if (!init(rs ? rs->get_rendering_device() : nullptr, error)) {
			UtilityFunctions::push_error("PixelWorld: the light pass is disabled: " + error);
			return;
		}
	}
	if (state_now.load(std::memory_order_acquire) != STATE_READY) return;
	if (rd == nullptr || p_area.w <= 0 || p_area.h <= 0 || p_area.scale < 1) return;
	const int texel_w = light_texel_w(p_area);
	const int texel_h = light_texel_h(p_area);
	if ((int64_t)p_texels.size() < (int64_t)texel_w * texel_h * 4) return;

	update_timestamps();
	free_retired(false);

	// The texel size only changes when the view, the margin or light_resolution does (PixelWorld
	// passes a view rect with a snapped size), so recreating everything here costs nothing while
	// scrolling.
	if (texel_w != area_w || texel_h != area_h || p_area.scale != area_scale) {
		// Publish "nothing rendered" before the old textures go: the overlay checks this from the
		// main thread and must not draw the previous area's texture into this one.
		rendered_w.store(0, std::memory_order_release);
		rendered_h.store(0, std::memory_order_release);
		rendered_scale.store(0, std::memory_order_release);
		if (!create_textures(texel_w, texel_h)) {
			UtilityFunctions::push_error("PixelWorld: the light pass could not create its textures");
			state_now.store(STATE_FAILED, std::memory_order_release);
			return;
		}
		area_w = texel_w;
		area_h = texel_h;
		area_scale = p_area.scale;
		bloom_w = (area_w + 3) / 4;
		bloom_h = (area_h + 3) / 4;
		if (!create_input_texture(area_w, area_h)) {
			UtilityFunctions::push_error("PixelWorld: the light pass could not create its input texture");
			state_now.store(STATE_FAILED, std::memory_order_release);
			return;
		}
	}
	rd->capture_timestamp("light_begin");
	rd->texture_update(input_texture, 0, p_texels);

	const int64_t list = rd->compute_list_begin();
	if (list == RenderingDevice::INVALID_ID) {
		rd->capture_timestamp("light_end");
		return;
	}

	// Reduced inputs, full resolution down to one texel per probe spacing.
	rd->compute_list_bind_compute_pipeline(list, mip_pipeline);
	for (int level = 2; level < LIGHT_CASCADES; ++level) {
		MipParams params{};
		params.src_x = mip_size(input_w, level - 1);
		params.src_y = mip_size(input_h, level - 1);
		const PackedByteArray bytes = push_bytes(params);
		const int dst_w = mip_size(input_w, level);
		const int dst_h = mip_size(input_h, level);
		rd->compute_list_bind_uniform_set(list, mip_sets[level], 0);
		rd->compute_list_set_push_constant(list, bytes, (uint32_t)bytes.size());
		rd->compute_list_dispatch(list, groups(dst_w), groups(dst_h), 1);
		rd->compute_list_add_barrier(list);
	}

	// Cascades, coarsest first, so each one can merge the one above it.
	rd->compute_list_bind_compute_pipeline(list, cascade_pipeline);
	for (int level = LIGHT_CASCADES - 1; level >= 0; --level) {
		// Level 0 reads the lit area at cell resolution; the others read their mip, which has
		// two texels per probe spacing (one direction tile per texel pair).
		const int mip_w = level == 0 ? area_w : mip_size(area_w, level);
		const int mip_h = level == 0 ? area_h : mip_size(area_h, level);
		const int probe_w = level == 0 ? area_w : (mip_w + 1) / 2;
		const int probe_h = level == 0 ? area_h : (mip_h + 1) / 2;
		CascadeParams params{};
		params.area_x = area_w;
		params.area_y = area_h;
		params.mip_x = mip_w;
		params.mip_y = mip_h;
		// The merge samples the cascade above on its own probe lattice: ceil(area / 2^(level+1)).
		params.upper_x = mip_size(area_w, level + 2);
		params.upper_y = mip_size(area_h, level + 2);
		params.t_start = level == 0 ? 0 : mip_start(level);
		params.t_end = level == 0 ? 0 : mip_end(level);
		params.rays = rays_for_level(level);
		params.has_upper = level < LIGHT_CASCADES - 1 ? 1 : 0;
		const PackedByteArray bytes = push_bytes(params);
		rd->compute_list_bind_uniform_set(list, cascade_sets[level], 0);
		rd->compute_list_set_push_constant(list, bytes, (uint32_t)bytes.size());
		rd->compute_list_dispatch(list, groups(probe_w * 2), groups(probe_h * 2), 1);
		rd->compute_list_add_barrier(list);
	}

	// Light output: average the four directions of cascade 0. With an odd number of surface
	// passes it lands in surface_texture, so the last pass writes light_texture either way.
	const int passes = MAX(p_surface_passes, 0);
	{
		OutputParams params{};
		params.area_x = area_w;
		params.area_y = area_h;
		const PackedByteArray bytes = push_bytes(params);
		rd->compute_list_bind_compute_pipeline(list, output_pipeline);
		rd->compute_list_bind_uniform_set(list, output_sets[passes % 2], 0);
		rd->compute_list_set_push_constant(list, bytes, (uint32_t)bytes.size());
		rd->compute_list_dispatch(list, groups(area_w), groups(area_h), 1);
		rd->compute_list_add_barrier(list);
	}

	// Light into solid surfaces, one cell deeper per pass, before bloom reads the light.
	if (passes > 0) {
		SurfaceParams params{};
		params.size_x = area_w;
		params.size_y = area_h;
		params.falloff = light_surface_falloff(passes);
		params.falloff_diag = std::pow(params.falloff, 1.41421356f);
		const PackedByteArray bytes = push_bytes(params);
		rd->compute_list_bind_compute_pipeline(list, surface_pipeline);
		for (int i = 0; i < passes; ++i) {
			// (passes - i) passes remain: an even count means the light is in light_texture now.
			rd->compute_list_bind_uniform_set(list, surface_sets[(passes - i) % 2], 0);
			rd->compute_list_set_push_constant(list, bytes, (uint32_t)bytes.size());
			rd->compute_list_dispatch(list, groups(area_w), groups(area_h), 1);
			rd->compute_list_add_barrier(list);
		}
	}

	// Bloom: thresholded 1/4 downsample, then a separable Gaussian at quarter resolution.
	{
		BloomDownParams params{};
		params.size_x = bloom_w;
		params.size_y = bloom_h;
		params.threshold = p_bloom_threshold;
		const PackedByteArray bytes = push_bytes(params);
		rd->compute_list_bind_compute_pipeline(list, bloom_down_pipeline);
		rd->compute_list_bind_uniform_set(list, bloom_down_set, 0);
		rd->compute_list_set_push_constant(list, bytes, (uint32_t)bytes.size());
		rd->compute_list_dispatch(list, groups(bloom_w), groups(bloom_h), 1);
		rd->compute_list_add_barrier(list);
	}
	rd->compute_list_bind_compute_pipeline(list, bloom_blur_pipeline);
	for (int pass = 0; pass < 2; ++pass) {
		BloomBlurParams params{};
		params.size_x = bloom_w;
		params.size_y = bloom_h;
		params.dir_x = pass == 0 ? 1 : 0;
		params.dir_y = pass == 0 ? 0 : 1;
		const PackedByteArray bytes = push_bytes(params);
		rd->compute_list_bind_uniform_set(list, bloom_blur_sets[pass], 0);
		rd->compute_list_set_push_constant(list, bytes, (uint32_t)bytes.size());
		rd->compute_list_dispatch(list, groups(bloom_w), groups(bloom_h), 1);
		rd->compute_list_add_barrier(list);
	}
	rd->compute_list_end();
	rd->capture_timestamp("light_end");
	rendered_w.store(area_w, std::memory_order_release);
	rendered_h.store(area_h, std::memory_order_release);
	rendered_scale.store(area_scale, std::memory_order_release);
}

void LightRenderer::update_timestamps() {
	if (rd == nullptr) return;
	const uint32_t count = rd->get_captured_timestamps_count();
	if (count < 2) return;
	uint64_t begin = 0;
	uint64_t end = 0;
	bool has_begin = false;
	bool has_end = false;
	for (uint32_t i = 0; i < count; ++i) {
		const String name = rd->get_captured_timestamp_name(i);
		if (name == "light_begin") {
			begin = rd->get_captured_timestamp_gpu_time(i);
			has_begin = true;
		} else if (name == "light_end") {
			end = rd->get_captured_timestamp_gpu_time(i);
			has_end = true;
		}
	}
	if (has_begin && has_end && end > begin) {
		gpu_ms_now.store((double)(end - begin) / 1.0e6, std::memory_order_release);
	}
}

void LightRenderer::free_all() {
	if (rd == nullptr) return;
	update_timestamps();
	retire_texture(light_texture);
	retire_texture(bloom_textures[0]);
	destroy_textures();
	free_retired(true);
	// Pipelines before shaders: freeing a shader frees its dependent pipelines too.
	for (RID *rid : { &mip_pipeline, &cascade_pipeline, &output_pipeline, &surface_pipeline,
	                  &bloom_down_pipeline, &bloom_blur_pipeline, &mip_shader, &cascade_shader,
	                  &output_shader, &surface_shader,
	                  &bloom_down_shader, &bloom_blur_shader, &sampler }) {
		if (rid->is_valid()) rd->free_rid(*rid);
		*rid = RID();
	}
	rd = nullptr;
	state_now.store(STATE_UNINITIALISED, std::memory_order_release);
}

} // namespace godot
