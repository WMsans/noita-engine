#pragma once

namespace godot {

// GLSL compute source for the radiance-cascade light pass, and the two canvas shaders that put
// its output on screen. Raw strings, so they live next to the pass that uses them.
//
// Cascade geometry (2D radiance cascades, as in TypeGPU's reference):
// cascade i has one probe every 2^i cells; its rays cover the distance interval
// [(4^i - 1) / 3, (4^i - 1) / 3 + 4^i) cells, so the six intervals tile [0, 1365) without
// double counting and each cascade merges the one above it with bilinear probe interpolation.
// Cascade 0's interval is the probe's own cell, so emissive cells light themselves.
//
// Each probe stores four directions (2x2 texels: rgb = radiance, a = transmittance along that
// direction). Rays per direction quadruple per cascade while each level marches its own mip of
// the input (one texel per half probe spacing), so the ray count grows with distance without the
// march growing with it. Emission averages down the mip chain and opacity takes the max, so thin
// walls still block coarse cascades instead of leaking light through them.

inline constexpr const char *LIGHT_MIP_COMPUTE = R"GLSL(
#version 450

layout(local_size_x = 8, local_size_y = 8) in;

layout(push_constant, std430) uniform Params {
	ivec2 src_size;
} params;

layout(set = 0, binding = 0) uniform sampler2D src;
layout(rgba8, set = 0, binding = 1) uniform image2D dst;

void main() {
	ivec2 p = ivec2(gl_GlobalInvocationID.xy);
	// The destination is ceil(src_size / 2) texels; the dispatch rounds up to whole workgroups.
	if (p.x * 2 >= params.src_size.x || p.y * 2 >= params.src_size.y) {
		return;
	}
	ivec2 src_hi = params.src_size - 1;
	// Emission averages (energy preserving); opacity takes the max, so a 1-cell wall still blocks
	// light in a coarse cascade instead of leaking through it.
	vec3 emission = vec3(0.0);
	float opacity = 0.0;
	for (int y = 0; y < 2; ++y) {
		for (int x = 0; x < 2; ++x) {
			vec4 c = texelFetch(src, min(p * 2 + ivec2(x, y), src_hi), 0);
			emission += c.rgb;
			opacity = max(opacity, c.a);
		}
	}
	imageStore(dst, p, vec4(emission * 0.25, opacity));
}
)GLSL";

inline constexpr const char *LIGHT_CASCADE_COMPUTE = R"GLSL(
#version 450

layout(local_size_x = 8, local_size_y = 8) in;

layout(push_constant, std430) uniform Params {
	ivec2 area;         // lit area in cells (level 0 only)
	ivec2 mip;          // this cascade's reduced input, in its own texels (level 0: the lit area)
	ivec2 upper_probes;
	int t_start;
	int t_end;
	int rays;
	int has_upper;
	int pad;
} params;

layout(set = 0, binding = 0) uniform sampler2D input_tex;
layout(rgba16f, set = 0, binding = 1) uniform image2D dst;
layout(rgba16f, set = 0, binding = 2) uniform image2D upper;

// Must match EMISSION_RANGE in src/core/light_input.h: the CPU packs e / EMISSION_RANGE and
// this shader multiplies the texel back up.
const float EMISSION_RANGE = 4.0;
const float PI = 3.14159265358979;

// Per-probe rotation of the ray fan: with only a few rays per direction, every probe sampling the
// same angles draws radial streaks. Rotating each probe's fan by a hashed fraction of a ray
// spacing, then letting the bilinear probe merge average neighbouring probes, turns those
// streaks into fine noise.
float probe_jitter(ivec2 p) {
	uint h = uint(p.x) * 1973u + uint(p.y) * 9277u + 26699u;
	h ^= h >> 16;
	h *= 0x7feb352du;
	h ^= h >> 15;
	return float(h & 0xffffu) / 65535.0;
}

// This probe's direction, read from the cascade above. Probe spacing halves per level, so half
// of the probes sit exactly on an upper probe and the other half are 50/50 between two.
vec4 upper_at(ivec2 probe, ivec2 dir) {
	ivec2 base = probe >> 1;
	vec2 f = vec2(probe - base * 2) * 0.5;
	ivec2 hi = params.upper_probes - 1;
	vec4 c00 = imageLoad(upper, clamp(base, ivec2(0), hi) * 2 + dir);
	vec4 c10 = imageLoad(upper, clamp(base + ivec2(1, 0), ivec2(0), hi) * 2 + dir);
	vec4 c01 = imageLoad(upper, clamp(base + ivec2(0, 1), ivec2(0), hi) * 2 + dir);
	vec4 c11 = imageLoad(upper, clamp(base + ivec2(1, 1), ivec2(0), hi) * 2 + dir);
	return mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
}

void main() {
	// One invocation per (probe, direction); the direction tiles the probe's 2x2 texels.
	ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
	ivec2 probe = texel >> 1;
	ivec2 dir = texel & 1;
	// Level 0 has a probe per cell; the levels above one per 2^level cells, two mip texels apart.
	const bool own_cell = params.rays == 0;
	if (own_cell) {
		if (probe.x >= params.area.x || probe.y >= params.area.y) {
			return;
		}
	} else if (probe.x * 2 >= params.mip.x || probe.y * 2 >= params.mip.y) {
		return;
	}

	// Level 0 reads the lit area at cell resolution; every level above reads its own mip, whose
	// texel is half a probe spacing, so fine occluders near a probe still block its rays.
	vec3 radiance = vec3(0.0);
	float transmittance = 1.0;

	if (own_cell) {
		// The near interval is the cell itself: a lava or fire cell lights itself.
		vec4 own = texelFetch(input_tex, clamp(probe, ivec2(0), params.area - 1), 0);
		radiance = own.rgb * EMISSION_RANGE;
	} else {
		// A probe sits two mip texels apart (its spacing is 2^level cells, a mip texel 2^(level-1)).
		ivec2 probe_mip = probe * 2;
		vec2 axis = vec2(float(dir.x) * 2.0 - 1.0, float(dir.y) * 2.0 - 1.0);
		float base_angle = atan(axis.y, axis.x) + probe_jitter(probe) * (PI * 0.5) / float(params.rays);
		vec3 sum = vec3(0.0);
		float trans = 0.0;
		for (int r = 0; r < params.rays; ++r) {
			float angle = base_angle + (float(r) + 0.5) / float(params.rays) * (PI * 0.5) - PI * 0.25;
			vec2 step_dir = vec2(cos(angle), sin(angle));
			step_dir /= max(abs(step_dir.x), abs(step_dir.y));  // one mip texel per step on the long axis
			vec3 ray = vec3(0.0);
			float t = 1.0;
			for (int step = params.t_start; step <= params.t_end; ++step) {
				ivec2 cell = probe_mip + ivec2(round(step_dir * float(step)));
				// Outside the lit area is opaque: the area already covers the view plus the
				// margin, and guessing about cells we do not have would leak light.
				vec4 texel_in = (cell.x < 0 || cell.y < 0 || cell.x >= params.mip.x || cell.y >= params.mip.y)
				    ? vec4(0.0, 0.0, 0.0, 1.0)
				    : texelFetch(input_tex, cell, 0);
				ray += t * texel_in.rgb * EMISSION_RANGE;
				t *= 1.0 - texel_in.a;
				if (t < 0.01) {
					break;
				}
			}
			sum += ray;
			trans += t;
		}
		radiance = sum / float(params.rays);
		transmittance = trans / float(params.rays);
	}

	if (params.has_upper != 0) {
		radiance += transmittance * upper_at(probe, dir).rgb;
	}
	imageStore(dst, texel, vec4(radiance, transmittance));
}
)GLSL";

inline constexpr const char *LIGHT_OUTPUT_COMPUTE = R"GLSL(
#version 450

layout(local_size_x = 8, local_size_y = 8) in;

layout(push_constant, std430) uniform Params {
	ivec2 area;
} params;

layout(rgba16f, set = 0, binding = 0) uniform image2D cascade;
layout(rgba16f, set = 0, binding = 1) uniform image2D light;

void main() {
	ivec2 p = ivec2(gl_GlobalInvocationID.xy);
	if (p.x >= params.area.x || p.y >= params.area.y) {
		return;
	}
	// Four 90-degree directions: an evenly lit cell's light is the average of the four.
	vec3 sum = imageLoad(cascade, p * 2).rgb;
	sum += imageLoad(cascade, p * 2 + ivec2(1, 0)).rgb;
	sum += imageLoad(cascade, p * 2 + ivec2(0, 1)).rgb;
	sum += imageLoad(cascade, p * 2 + ivec2(1, 1)).rgb;
	imageStore(light, p, vec4(sum * 0.25, 1.0));
}
)GLSL";

inline constexpr const char *BLOOM_DOWN_COMPUTE = R"GLSL(
#version 450

layout(local_size_x = 8, local_size_y = 8) in;

layout(push_constant, std430) uniform Params {
	ivec2 size;
	float threshold;
	float pad;
} params;

layout(set = 0, binding = 0) uniform sampler2D light_tex;
layout(rgba16f, set = 0, binding = 1) uniform image2D dst;

void main() {
	ivec2 p = ivec2(gl_GlobalInvocationID.xy);
	if (p.x >= params.size.x || p.y >= params.size.y) {
		return;
	}
	// One quarter-res texel is a 4x4 box of light texels; only emission above the threshold blooms.
	vec3 sum = vec3(0.0);
	for (int y = 0; y < 4; ++y) {
		for (int x = 0; x < 4; ++x) {
			vec3 c = texelFetch(light_tex, p * 4 + ivec2(x, y), 0).rgb;
			sum += max(c - vec3(params.threshold), vec3(0.0));
		}
	}
	imageStore(dst, p, vec4(sum / 16.0, 1.0));
}
)GLSL";

inline constexpr const char *BLOOM_BLUR_COMPUTE = R"GLSL(
#version 450

layout(local_size_x = 8, local_size_y = 8) in;

layout(push_constant, std430) uniform Params {
	ivec2 size;
	ivec2 dir;
} params;

layout(rgba16f, set = 0, binding = 0) uniform image2D src;
layout(rgba16f, set = 0, binding = 1) uniform image2D dst;

const int RADIUS = 8;
const float SIGMA = 4.0;

void main() {
	ivec2 p = ivec2(gl_GlobalInvocationID.xy);
	if (p.x >= params.size.x || p.y >= params.size.y) {
		return;
	}
	ivec2 hi = params.size - 1;
	vec4 sum = vec4(0.0);
	float weight_sum = 0.0;
	for (int k = -RADIUS; k <= RADIUS; ++k) {
		float w = exp(-0.5 * float(k * k) / (SIGMA * SIGMA));
		sum += imageLoad(src, clamp(p + params.dir * k, ivec2(0), hi)) * w;
		weight_sum += w;
	}
	imageStore(dst, p, sum / weight_sum);
}
)GLSL";

// Multiplies what is already on screen by ambient + light. Linear filtering, one texel per cell.
inline constexpr const char *LIGHT_OVERLAY_SHADER = R"GLSL(
shader_type canvas_item;
render_mode blend_mul, unshaded;

uniform float ambient = 0.05;
uniform float light_exposure = 1.0;

void fragment() {
	COLOR = vec4(min(vec3(ambient) + texture(TEXTURE, UV).rgb * light_exposure, 1.0), 1.0);
}
)GLSL";

inline constexpr const char *BLOOM_OVERLAY_SHADER = R"GLSL(
shader_type canvas_item;
render_mode blend_add, unshaded;

uniform float bloom_strength = 0.6;

void fragment() {
	COLOR = vec4(texture(TEXTURE, UV).rgb * bloom_strength, 1.0);
}
)GLSL";

} // namespace godot
