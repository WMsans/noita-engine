#pragma once
#include <cstdint>

#include "cell.h"

class WorldGrid;

// Material classes a ray stops at. Unloaded cells always stop a ray with a non-zero mask.
enum RayMask : uint32_t { RAY_SOLID = 1, RAY_POWDER = 2, RAY_LIQUID = 4, RAY_GAS = 8 };

struct RayHit {
	bool hit = false;
	int x = 0, y = 0;       // the hit cell
	MatId mat = MAT_AIR;    // MAT_AIR when `unloaded`
	bool unloaded = false;  // hit an unloaded chunk (counts as solid)
	int nx = 0, ny = 0;     // face normal; (0,0) when the ray starts inside the hit cell
};

// Amanatides-Woo DDA over cells floor(x), floor(y), from the start cell to the end cell
// inclusive. At an exact corner it steps x then y, so both cells are visited. mask == 0
// never hits.
RayHit raycast(const WorldGrid& grid, float x0, float y0, float x1, float y1, uint32_t mask);
