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

// Inclusive chunk rectangle; empty when x1 < x0 or y1 < y0, which contains() rejects for free.
struct ChunkRect {
	int x0, y0, x1, y1;

	bool contains(ChunkCoord c) const {
		return c.x >= x0 && c.x <= x1 && c.y >= y0 && c.y <= y1;
	}

	static ChunkRect around(ChunkCoord centre, int rx, int ry) {
		return {centre.x - rx, centre.y - ry, centre.x + rx, centre.y + ry};
	}
};

// Packs the two 32-bit coordinate bit patterns; unique per (x, y) pair. The shift is done on
// the unsigned value: shifting a negative x past INT64_MAX would be UB, so build it in
// uint64_t and convert back.
inline int64_t chunk_key(ChunkCoord c) {
	return (int64_t)(((uint64_t)(uint32_t)c.x << 32) | (uint32_t)c.y);
}
