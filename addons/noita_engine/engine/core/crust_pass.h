#pragma once
#include <cstdint>

#include "FastNoiseLite/FastNoiseLite.h"
#include "gen_pass.h"
#include "material.h"

// Blob crust on Solid cells with a crust texture that touch open space (Empty/Liquid/Gas;
// Powder is not open). Deterministic per seed; seamless across chunks because the blob is
// stamped from the edge cell, not clipped at any border.
class CrustPass : public GenPass {
public:
	CrustPass(const MaterialTable& table, uint32_t seed, float chance, int radius);
	int margin() const override;
	void apply(GenRegion& region) const override;

private:
	const MaterialTable& table_;
	uint32_t seed_;
	float chance_;
	int radius_;
	FastNoiseLite noise_;
};
