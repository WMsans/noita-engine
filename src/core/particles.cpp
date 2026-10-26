#include "particles.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "world_grid.h"

namespace {

float finite(float v) { return std::isfinite(v) ? v : 0.0f; }
float clamp_speed(float v) { return std::clamp(finite(v), -MAX_PARTICLE_SPEED, MAX_PARTICLE_SPEED); }
// floor, never a cast: truncation would put x = -0.5 in cell 0.
int cell_of(float v) { return (int)std::floor(v); }

// Swap-with-last removal of every element `keep` rejects; order is not stable.
template <class T, class F>
void retain(std::vector<T>& items, F&& keep) {
	for (size_t i = 0; i < items.size();) {
		if (keep(items[i])) {
			++i;
		} else {
			items[i] = items.back();
			items.pop_back();
		}
	}
}

}  // namespace

ParticlePool::ParticlePool(WorldGrid& grid, int capacity) : grid(grid), capacity_(std::max(0, capacity)) {
	material_.reserve((size_t)capacity_);
	visual_.reserve((size_t)capacity_);
}

bool ParticlePool::emit(float x, float y, float vx, float vy, const Cell& cell) {
	x = finite(x);
	y = finite(y);
	if (full()) {
		if (!is_air(cell_of(x), cell_of(y)) || !grid.put(cell_of(x), cell_of(y), cell)) ++stats_.dropped;
		return false;
	}
	material_.push_back({x, y, clamp_speed(vx), clamp_speed(vy), cell});
	return true;
}

bool ParticlePool::emit_visual(VisualParticle p) {
	if (full() || p.life == 0) return false;
	p.x = finite(p.x);
	p.y = finite(p.y);
	p.vx = clamp_speed(p.vx);
	p.vy = clamp_speed(p.vy);
	p.gravity_scale = finite(p.gravity_scale);
	visual_.push_back(p);
	return true;
}

// Loaded, not a body pixel, and air or gas.
bool ParticlePool::passable(int x, int y) const {
	Cell c{};
	if (!grid.get(x, y, c) || (c.flags & FLAG_BODY)) return false;
	const MatClass cls = grid.materials().get(c.mat).cls;
	return cls == MatClass::Empty || cls == MatClass::Gas;
}

bool ParticlePool::is_air(int x, int y) const {
	Cell c{};
	return grid.get(x, y, c) && c.mat == MAT_AIR && !(c.flags & FLAG_BODY);
}

// Moves in sub-steps of at most one cell per axis, so nothing is jumped. A sub-step whose
// cell is not passable is blocked, even the cell already occupied (the grid can change
// under a particle). Returns false at the first blocked sub-step, at the last free spot.
bool ParticlePool::advance(float& x, float& y, float vx, float vy) const {
	const int n = std::max(1, (int)std::ceil(std::max(std::fabs(vx), std::fabs(vy))));
	const float sx = vx / n, sy = vy / n;
	for (int i = 0; i < n; ++i) {
		const float nx = x + sx, ny = y + sy;
		if (!passable(cell_of(nx), cell_of(ny))) return false;
		x = nx;
		y = ny;
	}
	return true;
}

// First air among: the current cell, LAND_SEARCH cells back along the path, then straight up.
bool ParticlePool::land(const Particle& p) {
	const int cx = cell_of(p.x), cy = cell_of(p.y);
	auto try_put = [&](int x, int y) { return is_air(x, y) && grid.put(x, y, p.cell); };
	if (try_put(cx, cy)) return true;
	const float speed = std::max(std::fabs(p.vx), std::fabs(p.vy));
	if (speed > 0) {
		const float bx = -p.vx / speed, by = -p.vy / speed;  // one cell per step on the major axis
		for (int k = 1; k <= LAND_SEARCH; ++k)
			if (try_put(cell_of(p.x + bx * k), cell_of(p.y + by * k))) return true;
	}
	for (int k = 1; k <= LAND_SEARCH; ++k)
		if (try_put(cx, cy - k)) return true;
	return false;
}

// False when the particle leaves the pool: landed, or dropped.
bool ParticlePool::update(Particle& p) {
	p.vy = clamp_speed(p.vy + PARTICLE_GRAVITY);
	if (advance(p.x, p.y, p.vx, p.vy)) return true;
	if (!land(p)) ++stats_.dropped;
	return false;
}

bool ParticlePool::update(VisualParticle& p) const {
	if (--p.life == 0) return false;
	p.vy = clamp_speed(p.vy + PARTICLE_GRAVITY * p.gravity_scale);
	if (p.collides) return advance(p.x, p.y, p.vx, p.vy);
	p.x += p.vx;
	p.y += p.vy;
	return true;
}

void ParticlePool::step() {
	const auto start = std::chrono::steady_clock::now();
	retain(material_, [&](Particle& p) { return update(p); });
	retain(visual_, [&](VisualParticle& p) { return update(p); });
	stats_.material = (int)material_.size();
	stats_.visual = (int)visual_.size();
	stats_.particle_ms =
	    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
