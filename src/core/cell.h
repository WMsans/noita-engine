#pragma once
#include <cstdint>

using MatId = uint16_t;
constexpr MatId MAT_AIR = 0;

// One simulated pixel. Keep this 8 bytes: chunks store them densely.
struct Cell {
	MatId mat;
	int16_t temp;  // degrees C, clamped to [-273, 5000]
	uint8_t life;  // burn / lifetime counter, 0 = none
	// FLAG_BURNING, FLAG_BODY (reserved, plan 4); bits 4..7 = fall velocity 0..15
	uint8_t flags;
	uint8_t shade;  // per-pixel color variation 0..255
	uint8_t stamp;  // low 8 bits of tick in which this cell last moved/updated
};
static_assert(sizeof(Cell) == 8, "Cell must stay 8 bytes");

constexpr uint8_t FLAG_BURNING = 1 << 0;
constexpr uint8_t FLAG_BODY = 1 << 1;  // reserved for plan 4

constexpr int16_t AMBIENT_TEMP = 20;
