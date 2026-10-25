#pragma once
#include <cstdint>

constexpr int CHUNK = 64;

// Floor division / modulo: rounds towards -infinity, unlike C's truncation.
inline int floor_div(int a, int b) {
	int q = a / b;
	if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
	return q;
}

inline int floor_mod(int a, int b) { return a - floor_div(a, b) * b; }

struct ChunkCoord {
	int x, y;
	bool operator==(const ChunkCoord& o) const { return x == o.x && y == o.y; }
};

inline ChunkCoord chunk_of(int wx, int wy) { return {floor_div(wx, CHUNK), floor_div(wy, CHUNK)}; }

// Packs the two 32-bit coordinate bit patterns; unique per (x, y) pair. The shift is done on
// the unsigned value: shifting a negative x past INT64_MAX would be UB, so build it in
// uint64_t and convert back.
inline int64_t chunk_key(ChunkCoord c) {
	return (int64_t)(((uint64_t)(uint32_t)c.x << 32) | (uint32_t)c.y);
}
