#include "crust_pass.h"

#include <algorithm>
#include <cmath>

#include "rng.h"

namespace {

constexpr uint32_t SALT_CRUST = 0x70u;  // 0x65..0x6C are back_wall's
constexpr float NOISE_FREQUENCY = 0.4f;

int reach_of(int radius) { return (int)std::ceil(radius * 1.3f + 2.0f); }

}  // namespace

CrustPass::CrustPass(const MaterialTable& table, uint32_t seed, float chance, int radius)
    : table_(table), seed_(seed), chance_(chance), radius_(radius) {
	noise_.SetNoiseType(FastNoiseLite::NoiseType_Value);
	noise_.SetFrequency(NOISE_FREQUENCY);
	noise_.SetSeed((int)hash5(seed, 0, 0, 0, SALT_CRUST));
}

int CrustPass::margin() const { return reach_of(radius_) + 1; }

void CrustPass::apply(GenRegion& region) const {
	const int n = region.size;
	const int reach = reach_of(radius_);
	auto open = [&](int wx, int wy) {
		const MatClass cls = table_.get(region.mat_at(wx, wy)).cls;
		return cls == MatClass::Empty || cls == MatClass::Liquid || cls == MatClass::Gas;
	};
	auto crusted = [&](int wx, int wy) {
		const CompiledMaterial& def = table_.get(region.mat_at(wx, wy));
		return def.cls == MatClass::Solid && !def.crust_rgba.empty();
	};
	// Edge cells are tested one cell in from the region border, so their 4-neighbourhood
	// stays inside the region.
	for (int ly = 1; ly < n - 1; ++ly) {
		const int wy = region.y0 + ly;
		for (int lx = 1; lx < n - 1; ++lx) {
			const int wx = region.x0 + lx;
			if (!crusted(wx, wy)) continue;
			if (!open(wx + 1, wy) && !open(wx - 1, wy) && !open(wx, wy + 1) && !open(wx, wy - 1))
				continue;
			if (hash_unit(hash5(seed_, 0, wx, wy, SALT_CRUST)) >= chance_) continue;
			const float r =
			    (float)radius_ * (0.7f + 0.6f * hash_unit(hash5(seed_, 1, wx, wy, SALT_CRUST)));
			// The blob is clipped at the region border: past it the index would wrap onto the far
			// side or off the vectors. Cells margin() in still get every blob that reaches them.
			const int ny0 = std::max(wy - reach, region.y0), ny1 = std::min(wy + reach, region.y0 + n - 1);
			const int nx0 = std::max(wx - reach, region.x0), nx1 = std::min(wx + reach, region.x0 + n - 1);
			for (int ny = ny0; ny <= ny1; ++ny)
				for (int nx = nx0; nx <= nx1; ++nx) {
					if (!crusted(nx, ny)) continue;
					// The blob edge wobbles by +/-2 cells of value noise (GetNoise is [-1, 1]).
					const double edge = std::hypot((double)(nx - wx), (double)(ny - wy)) +
					                    noise_.GetNoise((double)nx, (double)ny) * 2.0;
					if (edge < r) region.flags_at(nx, ny) |= FLAG_CRUST;
				}
		}
	}
}
