#pragma once
#include <cstdint>
#include <vector>

#include "cell.h"

class WorldGrid;

// Units: cells and ticks. Gravity and the throw chance are tuning knobs.
constexpr float PARTICLE_GRAVITY = 0.25f;    // cells/tick^2
constexpr float MAX_PARTICLE_SPEED = 16.0f;  // cells/tick per axis, the sim's per-tick move limit
constexpr int LAND_SEARCH = 4;               // cells searched back along the path, then up
constexpr float EXPLODE_THROW_CHANCE = 0.5f;
constexpr int DEFAULT_PARTICLE_CAPACITY = 20000;

// A pixel off the grid: flies until a cell blocks it, then lands as `cell`.
struct Particle {
	float x, y, vx, vy;  // position in cells, velocity in cells/tick
	Cell cell;
};

// Never material: moves like a particle, fades over its lifetime and never touches the grid.
struct VisualParticle {
	float x = 0, y = 0, vx = 0, vy = 0;
	float gravity_scale = 1.0f;
	uint32_t rgba = 0xFFFFFFFF;  // same packing as CompiledMaterial::color_rgba
	uint16_t life = 0;           // ticks left
	uint16_t lifetime = 0;       // ticks total, for the fade
	bool collides = true;        // dies on the first blocked cell; false passes through terrain
};

struct ParticleStats {
	int material = 0, visual = 0;  // pool sizes after the last step
	int64_t dropped = 0;           // material particles lost: no room to land or to pool
	double particle_ms = 0;
};

// ponytail: single-threaded; move to JobPool if particle_ms > 1 ms
class ParticlePool {
public:
	ParticlePool(WorldGrid& grid, int capacity);  // capacity counts both kinds

	// True if pooled. A full pool writes the cell back at floor(x, y) when that is free, else
	// counts it dropped. Non-finite input becomes 0 and speeds clamp to MAX_PARTICLE_SPEED.
	bool emit(float x, float y, float vx, float vy, const Cell& cell);
	bool emit_visual(VisualParticle p);  // true if pooled; life 0 is rejected
	void step();                         // one tick

	const std::vector<Particle>& material() const { return material_; }
	const std::vector<VisualParticle>& visual() const { return visual_; }
	const ParticleStats& stats() const { return stats_; }

private:
	bool full() const { return material_.size() + visual_.size() >= (size_t)capacity_; }
	bool passable(int x, int y) const;
	bool advance(float& x, float& y, float vx, float vy) const;
	bool land(const Particle& p);
	bool update(Particle& p);
	bool update(VisualParticle& p) const;

	WorldGrid& grid;
	int capacity_;
	std::vector<Particle> material_;
	std::vector<VisualParticle> visual_;
	ParticleStats stats_{};
};

struct ExplodeResult {
	int removed = 0, thrown = 0, vanished = 0;  // removed == thrown + vanished
};

// Removes every loaded, non-air, non-body cell within `radius` of (cx, cy). Solids and gases
// vanish; powders and liquids are thrown with EXPLODE_THROW_CHANCE, outward at up to `power`
// cells/tick (full at the centre, none at the rim) plus a lift, else vanish. Pixels only:
// nothing else is pushed. `salt` varies the dice per call. radius <= 0 does nothing.
ExplodeResult explode(WorldGrid& grid, ParticlePool& pool, int cx, int cy, int radius,
                      float power, uint32_t salt);
