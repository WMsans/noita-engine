#include "sim_rules.h"

#include <algorithm>
#include <utility>

#include "cell.h"
#include "rng.h"

namespace {

// Hash salts: one per random decision, so two decisions at the same position differ.
constexpr uint32_t SALT_DIAGONAL = 1;
constexpr uint32_t SALT_SIDEWAYS = 2;
constexpr uint32_t SALT_GAS_JITTER = 3;

constexpr int MAX_SPEED = 15;       // velocity ceiling, so one fall never exceeds 15 cells
constexpr int MAX_DISPERSION = 16;  // the per-axis per-tick move limit from the brief

// A cell while it moves. wx/wy is its world position when the update started and the only
// hash key, so a choice never depends on how far the cell has already travelled.
struct Mover {
	Neighborhood& nb;
	const MaterialTable& mt;
	const CompiledMaterial& mat;
	uint32_t tick, seed;
	int wx, wy;
};

// Displacement rule: Empty is always passable. A Liquid or Gas is passable to a powder or a
// liquid when it is lighter than the mover, and to a gas when it is heavier (gases rise
// through denser fluids). Solids are never passable.
bool passable(const Mover& m, const Cell& target) {
	const CompiledMaterial& t = m.mt.get(target.mat);
	if (t.cls == MatClass::Empty) return true;
	if (t.cls != MatClass::Liquid && t.cls != MatClass::Gas) return false;
	return m.mat.cls == MatClass::Gas ? t.density > m.mat.density : t.density < m.mat.density;
}

// Moves the cell at (lx,ly) one cell by (dx,dy) when the target is passable, swapping the
// two cells. Both get stamped: the displaced cell moved too, which is what keeps a tick
// independent of the order chunks are processed in.
bool step(Mover& m, int& lx, int& ly, int dx, int dy) {
	const int tx = lx + dx, ty = ly + dy;
	Cell* self = m.nb.cell(lx, ly);
	Cell* target = m.nb.cell(tx, ty);
	if (!self || !target) return false;  // a missing chunk acts solid
	if (!passable(m, *target)) return false;

	std::swap(*self, *target);
	const uint8_t stamp = (uint8_t)m.tick;
	target->stamp = stamp;  // the mover is at the target now
	self->stamp = stamp;    // the displaced cell
	m.nb.mark(lx, ly);
	m.nb.mark(tx, ty);
	lx = tx;
	ly = ty;
	return true;
}

// Steps up to `count` cells in one direction; returns how many steps happened.
int run(Mover& m, int& lx, int& ly, int dx, int dy, int count) {
	int moved = 0;
	while (moved < count && step(m, lx, ly, dx, dy)) ++moved;
	return moved;
}

int dir_for(const Mover& m, uint32_t salt) {
	return (hash5(m.seed, m.tick, m.wx, m.wy, salt) & 1) ? 1 : -1;
}

// v = clamp(velocity + 1, 1, 15)
int next_speed(const Cell& c) { return std::min(cell_velocity(c) + 1, MAX_SPEED); }

int spread_of(const Mover& m) { return std::min<int>(m.mat.dispersion, MAX_DISPERSION); }

// The mover's cell after one or more steps. Never null: we either stayed put (the pointer
// we started with) or stepped into a target that `step` proved exists.
Cell* here(Mover& m, int lx, int ly) { return m.nb.cell(lx, ly); }

void move_powder(Mover& m, int& lx, int& ly) {
	const int v = next_speed(*here(m, lx, ly));
	const int diag = dir_for(m, SALT_DIAGONAL);
	if (run(m, lx, ly, 0, 1, v) > 0 || step(m, lx, ly, diag, 1) || step(m, lx, ly, -diag, 1)) {
		set_cell_velocity(*here(m, lx, ly), v);
		return;
	}
	set_cell_velocity(*here(m, lx, ly), 0);  // settled
}

void move_liquid(Mover& m, int& lx, int& ly) {
	const int v = next_speed(*here(m, lx, ly));
	const int diag = dir_for(m, SALT_DIAGONAL);
	if (run(m, lx, ly, 0, 1, v) > 0 || step(m, lx, ly, diag, 1) || step(m, lx, ly, -diag, 1)) {
		set_cell_velocity(*here(m, lx, ly), v);
		return;
	}
	// Nothing below: spread sideways, keeping the fall speed for the next ledge.
	const int side = dir_for(m, SALT_SIDEWAYS);
	const int spread = spread_of(m);
	if (run(m, lx, ly, side, 0, spread) == 0) run(m, lx, ly, -side, 0, spread);
}

void move_gas(Mover& m, int& lx, int& ly) {
	const int v = next_speed(*here(m, lx, ly));
	const int diag = dir_for(m, SALT_DIAGONAL);
	const int side = dir_for(m, SALT_SIDEWAYS);
	const int spread = spread_of(m);
	// Jitter: on half the ticks the gas drifts sideways before it rises.
	if (hash_unit(hash5(m.seed, m.tick, m.wx, m.wy, SALT_GAS_JITTER)) < 0.5f &&
	    run(m, lx, ly, side, 0, spread) > 0)
		return;
	if (run(m, lx, ly, 0, -1, v) > 0 || step(m, lx, ly, diag, -1) || step(m, lx, ly, -diag, -1)) {
		set_cell_velocity(*here(m, lx, ly), v);
		return;
	}
	if (run(m, lx, ly, side, 0, spread) == 0) run(m, lx, ly, -side, 0, spread);
}

}  // namespace

void update_cell(Neighborhood& nb, int lx, int ly, uint32_t tick, uint32_t seed,
                 const MaterialTable& mt) {
	Cell* self = nb.cell(lx, ly);
	if (!self) return;
	if (self->mat == MAT_AIR) return;
	if (self->stamp == (uint8_t)tick) return;  // already moved/updated this tick

	const CompiledMaterial& def = mt.get(self->mat);
	if (def.cls == MatClass::Powder || def.cls == MatClass::Liquid || def.cls == MatClass::Gas) {
		// The cell is only ever handed to us from nb's centre chunk, so its world position
		// (the hash key) comes from that chunk's coord.
		const ChunkCoord centre = nb.c[1][1]->coord;
		Mover m{nb, mt, def, tick, seed, centre.x * CHUNK + lx, centre.y * CHUNK + ly};
		if (def.cls == MatClass::Powder) move_powder(m, lx, ly);
		else if (def.cls == MatClass::Liquid) move_liquid(m, lx, ly);
		else move_gas(m, lx, ly);
	}
	// Solid: never moves.

	// Task 5 inserts chemistry here, on the cell's final position (lx, ly): reactions, heat
	// exchange, phase change, burning and lifetime all run after the move.
}
