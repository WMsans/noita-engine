#pragma once
#include <cstdint>

// Stateless position+tick hashing: the same query always gives the same bits, so
// sim results do not depend on evaluation order. Murmur3 fmix32 rounds.
inline uint32_t hash5(uint32_t seed, uint32_t tick, int32_t x, int32_t y, uint32_t salt) {
	uint32_t h = seed;
	const uint32_t parts[4] = {tick, (uint32_t)x, (uint32_t)y, salt};
	for (uint32_t v : parts) {
		h ^= v;
		h *= 0x85ebca6bu;
		h ^= h >> 13;
		h *= 0xc2b2ae35u;
		h ^= h >> 16;
	}
	return h;
}

// Top 24 bits -> [0, 1). 2^24 is exact in float, so the result never rounds up to 1.
inline float hash_unit(uint32_t h) { return (h >> 8) * (1.0f / 16777216.0f); }
