#include "particles.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "rng.h"
#include "world_grid.h"

namespace {

// Hash salts for explode's dice, distinct from the sim's.
constexpr uint32_t SALT_THROW = 101;
constexpr uint32_t SALT_SPEED = 102;
constexpr uint32_t SALT_ANGLE = 103;
constexpr uint32_t SALT_SPARK = 104;
constexpr uint32_t SALT_LIFE = 105;
constexpr uint32_t SALT_RAG = 106;
constexpr uint32_t SALT_GAS = 107;
constexpr uint32_t SALT_LOBE = 110;   // 110..112
constexpr uint32_t SALT_FLASH = 120;  // 120.., three per flash spark

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
		if (!passable(cell_of(x), cell_of(y)) || !grid.put(cell_of(x), cell_of(y), cell)) ++stats_.dropped;
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

// Loaded, not a body pixel, and air or gas. Also where a particle may land: a gas cell it
// lands in is overwritten (gas is short-lived, and a particle stopped inside smoke would
// otherwise find nowhere to land).
bool ParticlePool::passable(int x, int y) const {
	Cell c{};
	if (!grid.get(x, y, c) || (c.flags & FLAG_BODY)) return false;
	const MatClass cls = grid.materials().get(c.mat).cls;
	return cls == MatClass::Empty || cls == MatClass::Gas;
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

// First passable cell among: the current cell, LAND_SEARCH cells back along the path, straight
// up, then square rings out to LAND_SEARCH. The rings matter where ejecta pile up against a
// crater wall and fill every cell on each other's path.
bool ParticlePool::land(const Particle& p) {
	const int cx = cell_of(p.x), cy = cell_of(p.y);
	auto try_put = [&](int x, int y) { return passable(x, y) && grid.put(x, y, p.cell); };
	if (try_put(cx, cy)) return true;
	const float speed = std::max(std::fabs(p.vx), std::fabs(p.vy));
	if (speed > 0) {
		const float bx = -p.vx / speed, by = -p.vy / speed;  // one cell per step on the major axis
		for (int k = 1; k <= LAND_SEARCH; ++k)
			if (try_put(cell_of(p.x + bx * k), cell_of(p.y + by * k))) return true;
	}
	for (int k = 1; k <= LAND_SEARCH; ++k)
		if (try_put(cx, cy - k)) return true;
	for (int r = 1; r <= LAND_SEARCH; ++r)
		for (int dy = -r; dy <= r; ++dy)
			for (int dx = -r; dx <= r; dx += (dy == -r || dy == r) ? 1 : 2 * r)
				if (try_put(cx + dx, cy + dy)) return true;
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

ExplodeResult explode(WorldGrid& grid, ParticlePool& pool, int cx, int cy, int radius,
                      float power, uint32_t salt, const ExplodeStyle& style) {
	ExplodeResult result;
	if (radius <= 0) return result;
	const MaterialTable& mt = grid.materials();
	const uint32_t seed = grid.seed();
	const SparkColor& spark_color = style.spark_color;
	const float noise = std::clamp(finite(style.rim_noise), 0.0f, 1.0f);

	// Three sine lobes at random phases give the rim a different bulge per blast, and a per-cell
	// dice frays the last cell or two. The centre always goes.
	float phase[3];
	for (int k = 0; k < 3; ++k) phase[k] = hash_unit(hash5(seed, salt, cx, cy, SALT_LOBE + k)) * 6.2831853f;
	auto inside = [&](int dx, int dy) {
		if (!in_blast(dx, dy, radius)) return false;
		if (noise <= 0 || (dx == 0 && dy == 0)) return true;
		const float a = std::atan2((float)dy, (float)dx);
		const float lobe = 0.5f + 0.25f * std::sin(3 * a + phase[0]) + 0.15f * std::sin(5 * a + phase[1]) +
		                   0.1f * std::sin(9 * a + phase[2]);  // 0..1
		const float edge = radius * (1.0f - noise * lobe) + 0.5f -
		                   1.5f * hash_unit(hash5(seed, salt, cx + dx, cy + dy, SALT_RAG));
		return (float)(dx * dx + dy * dy) <= edge * edge;
	};

	// Clear everything first and throw afterwards, so a full pool's write-back lands in the
	// crater rather than being cleared again.
	struct Loose {
		int dx, dy;
		Cell cell;
	};
	std::vector<Loose> loose, gone;  // gone: vanished cells, kept only to spark
	std::vector<std::pair<int, int>> cleared;
	for (int dy = -radius; dy <= radius; ++dy)
		for (int dx = -radius; dx <= radius; ++dx) {
			if (!inside(dx, dy)) continue;
			const int x = cx + dx, y = cy + dy;
			Cell c{};
			if (!grid.get(x, y, c) || c.mat == MAT_AIR || (c.flags & FLAG_BODY)) continue;
			++result.removed;
			grid.set(x, y, MAT_AIR);
			cleared.push_back({dx, dy});
			const MatClass cls = mt.get(c.mat).cls;
			const bool is_loose = cls == MatClass::Powder || cls == MatClass::Liquid;
			if (is_loose && hash_unit(hash5(seed, salt, x, y, SALT_THROW)) < EXPLODE_THROW_CHANCE) {
				loose.push_back({dx, dy, c});
			} else {
				++result.vanished;
				if (spark_color) gone.push_back({dx, dy, c});
			}
		}

	// Outward from the centre, full `power` there and none at the rim, plus a lift.
	auto velocity = [&](const Loose& l, float& vx, float& vy) {
		const int x = cx + l.dx, y = cy + l.dy;
		const float d = std::sqrt((float)(l.dx * l.dx + l.dy * l.dy));
		float ux, uy;
		if (d > 0) {
			ux = l.dx / d;
			uy = l.dy / d;
		} else {
			const float angle = hash_unit(hash5(seed, salt, x, y, SALT_ANGLE)) * 6.2831853f;
			ux = std::cos(angle);
			uy = std::sin(angle);
		}
		const float speed = power * std::max(0.0f, 1.0f - d / radius) *
		                    (0.5f + 0.5f * hash_unit(hash5(seed, salt, x, y, SALT_SPEED)));
		vx = ux * speed;
		vy = uy * speed - 0.25f * power;
	};

	for (const Loose& l : loose) {
		float vx, vy;
		velocity(l, vx, vy);
		if (pool.emit(cx + l.dx + 0.5f, cy + l.dy + 0.5f, vx, vy, l.cell)) {
			++result.thrown;
		} else {
			++result.vanished;
			if (spark_color) gone.push_back(l);
		}
	}

	// Gases go in after the throw: both are passable, so the ejecta still fly through them.
	for (const auto& [dx, dy] : cleared) {
		const int x = cx + dx, y = cy + dy;
		const float roll = hash_unit(hash5(seed, salt, x, y, SALT_GAS));
		MatId gas = NO_MAT;
		if (style.fire != NO_MAT && roll < style.fire_chance) {
			gas = style.fire;
		} else if (style.smoke != NO_MAT && roll < style.fire_chance + style.smoke_chance) {
			gas = style.smoke;
		}
		Cell c{};
		if (gas != NO_MAT && grid.get(x, y, c) && c.mat == MAT_AIR) grid.set(x, y, gas);
	}

	for (const Loose& g : gone) {
		const int x = cx + g.dx, y = cy + g.dy;
		if (hash_unit(hash5(seed, salt, x, y, SALT_SPARK)) >= EXPLODE_SPARK_CHANCE) continue;
		VisualParticle p;
		p.x = x + 0.5f;
		p.y = y + 0.5f;
		velocity(g, p.vx, p.vy);
		p.rgba = spark_color(g.cell);
		p.lifetime = p.life =
		    (uint16_t)(EXPLODE_SPARK_LIFE + hash_unit(hash5(seed, salt, x, y, SALT_LIFE)) * EXPLODE_SPARK_LIFE);
		if (!pool.emit_visual(p)) return result;  // full: every later spark would fail too
		++result.sparks;
	}

	// The flash: fast, light and over the terrain, out to about twice the radius.
	for (int i = 0; i < style.flash_sparks; ++i) {
		const float angle = hash_unit(hash5(seed, salt, cx, cy, SALT_FLASH + 3 * (uint32_t)i)) * 6.2831853f;
		const float speed = power * (0.3f + 0.7f * hash_unit(hash5(seed, salt, cx, cy, SALT_FLASH + 3 * (uint32_t)i + 1)));
		VisualParticle p;
		p.x = cx + 0.5f;
		p.y = cy + 0.5f;
		p.vx = std::cos(angle) * speed;
		p.vy = std::sin(angle) * speed;
		p.gravity_scale = 0.3f;
		p.collides = false;
		p.rgba = style.flash_rgba;
		p.lifetime = p.life = (uint16_t)(EXPLODE_FLASH_LIFE + hash_unit(hash5(seed, salt, cx, cy, SALT_FLASH + 3 * (uint32_t)i + 2)) *
		                                                   EXPLODE_FLASH_LIFE);
		if (!pool.emit_visual(p)) break;
		++result.sparks;
	}
	return result;
}
