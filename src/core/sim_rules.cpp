#include "sim_rules.h"

#include <algorithm>
#include <cmath>
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

// --- Chemistry ---------------------------------------------------------------

// The 4-neighbour order reactions use as their hash salt, one index per direction.
constexpr int NDX[4] = {1, -1, 0, 0};
constexpr int NDY[4] = {0, 0, 1, -1};

constexpr uint32_t SALT_REACTION = 0;  // + neighbour index (0..3)
constexpr uint32_t SALT_HEAT = 4;
constexpr uint32_t SALT_FIRE_CHANCE = 5;
constexpr uint32_t SALT_FIRE_DIR = 6;

constexpr float FIRE_SPREAD_CHANCE = 0.3f;
constexpr int BURN_STOP_MARGIN = 50;  // a burning cell below ignite_temp - 50 goes out
constexpr int TEMP_MIN = -273;
constexpr int TEMP_MAX = 5000;

int16_t clamp_temp(int t) {
	return (int16_t)(t < TEMP_MIN ? TEMP_MIN : t > TEMP_MAX ? TEMP_MAX : t);
}

// Nearest whole degree, ties away from zero: a 0.4 degree exchange is dropped, and any
// |diff| >= 2 always moves a temperature by at least 1.
int round_degrees(float d) { return (int)std::lround(d); }

// A cell while its chemistry runs, at the position it ended the tick on: the move helpers
// update lx/ly in place, and wx/wy (the only hash key) follow from that final position.
struct Chem {
	Neighborhood& nb;
	const MaterialTable& mt;
	Cell& self;
	uint32_t tick, seed;
	int lx, ly;
	int wx, wy;
};

// The id of the "fire" material, resolved once per table: id_of() scans names, which must
// not run per cell per tick. A table's address alone is not a safe key: the same address
// can hold a different table later (callers recompile into a reused MaterialTable object,
// and a fresh table can reuse a freed table's address), so a hit is only trusted while the
// cached id still names fire, and the name is resolved again when it does not. Every thread
// resolves the same name to the same id, so the cache cannot change results.
MatId fire_id(const MaterialTable& mt) {
	struct Entry {
		const MaterialTable* table;
		std::size_t size;
		MatId fire;
	};
	thread_local Entry cache{nullptr, 0, NO_MAT};
	if (cache.table != &mt || cache.size != mt.size() || mt.name_of(cache.fire) != "fire")
		cache = Entry{&mt, mt.size(), mt.id_of("fire")};
	return cache.fire;
}

// Reactions: at most one per cell per tick, on the neighbour index as the hash salt. A cell
// that has a candidate but rolls a miss stays awake, so a rare reaction (acid) is retried.
void react(Chem& c) {
	if (!c.mt.get(c.self.mat).has_reactions) return;
	bool candidate = false;
	for (int i = 0; i < 4; ++i) {
		const int nx = c.lx + NDX[i], ny = c.ly + NDY[i];
		Cell* n = c.nb.cell(nx, ny);
		if (!n) continue;
		const Reaction* r = c.mt.reaction(c.self.mat, n->mat);
		if (!r) continue;
		// The temperature condition is read from the cell being updated, before any change.
		if (c.self.temp < r->min_temp || c.self.temp > r->max_temp) continue;
		candidate = true;
		if (hash_unit(hash5(c.seed, c.tick, c.wx, c.wy, SALT_REACTION + (uint32_t)i)) >=
		    r->probability)
			continue;
		const MatId self_into = r->a_into, other_into = r->b_into;
		c.self = spawn_cell(self_into, c.mt, c.seed, c.wx, c.wy);
		*n = spawn_cell(other_into, c.mt, c.seed, c.wx + NDX[i], c.wy + NDY[i]);
		// The product spawned into the neighbour is stamped: if the scan reaches its row or
		// chunk later this tick it must not run chemistry (or count its lifetime) a second
		// time, so the result does not depend on scan or chunk order.
		n->stamp = (uint8_t)c.tick;
		c.nb.mark(c.lx, c.ly);
		c.nb.mark(nx, ny);
		return;
	}
	if (candidate) c.nb.mark(c.lx, c.ly);
}

// Heat exchange with one hash-chosen 4-neighbour. Air holds no heat: next to Empty a cell
// only relaxes towards AMBIENT_TEMP. A fixed_temp neighbour (lava, fire) is a pure source or
// sink and keeps its own temperature.
void exchange_heat(Chem& c) {
	const CompiledMaterial& def = c.mt.get(c.self.mat);
	if (def.cls == MatClass::Empty || def.fixed_temp) return;

	const int i = (int)(hash5(c.seed, c.tick, c.wx, c.wy, SALT_HEAT) & 3u);
	const int nx = c.lx + NDX[i], ny = c.ly + NDY[i];
	Cell* n = c.nb.cell(nx, ny);
	if (!n) return;  // an unloaded chunk holds no heat either

	const CompiledMaterial& ndef = c.mt.get(n->mat);
	if (ndef.cls == MatClass::Empty) {
		const int d = round_degrees((AMBIENT_TEMP - c.self.temp) * def.conductivity * 0.05f);
		if (d == 0) return;
		c.self.temp = clamp_temp(c.self.temp + d);
		c.nb.mark(c.lx, c.ly);  // the air's own temperature is not stored, so only self wakes
		return;
	}

	const float cond = std::min(def.conductivity, ndef.conductivity);
	const int d = round_degrees((n->temp - c.self.temp) * cond * 0.5f);
	if (d == 0) return;
	c.self.temp = clamp_temp(c.self.temp + d);
	if (!ndef.fixed_temp) n->temp = clamp_temp(n->temp - d);
	c.nb.mark(c.lx, c.ly);
	c.nb.mark(nx, ny);
}

// Phase change. The new material brings its own defaults (lifetime, flags, shade) but not
// its base_temp: the cell keeps the temperature that pushed it over the threshold.
void phase_change(Chem& c) {
	const CompiledMaterial& def = c.mt.get(c.self.mat);
	MatId into = NO_MAT;
	if (def.high_into != NO_MAT && c.self.temp >= def.high_temp) into = def.high_into;
	else if (def.low_into != NO_MAT && c.self.temp <= def.low_temp) into = def.low_into;
	if (into == NO_MAT) return;
	const int16_t temp = c.self.temp;
	c.self = spawn_cell(into, c.mt, c.seed, c.wx, c.wy);
	c.self.temp = temp;
	c.nb.mark(c.lx, c.ly);
}

// Ignition and burning. While FLAG_BURNING is set, `life` is the burn timer: a cell that
// ignites this tick already burns this tick, so a lit cell heats its neighbours at once.
void burn(Chem& c) {
	const CompiledMaterial& def = c.mt.get(c.self.mat);
	if (!(c.self.flags & FLAG_BURNING)) {
		if (def.ignite_temp == NO_TEMP || c.self.temp < def.ignite_temp) return;
		c.self.flags |= FLAG_BURNING;
		c.self.life = def.burn_life;
	} else if (def.ignite_temp != NO_TEMP &&
	           c.self.temp < (int)def.ignite_temp - BURN_STOP_MARGIN) {
		c.self.flags &= (uint8_t)~FLAG_BURNING;
		c.nb.mark(c.lx, c.ly);
		return;
	}

	if (def.burn_heat > c.self.temp) c.self.temp = def.burn_heat;

	// Spread: sometimes put fire into one hash-chosen Empty neighbour.
	const MatId fire = fire_id(c.mt);
	if (fire != NO_MAT &&
	    hash_unit(hash5(c.seed, c.tick, c.wx, c.wy, SALT_FIRE_CHANCE)) < FIRE_SPREAD_CHANCE) {
		const int i = (int)(hash5(c.seed, c.tick, c.wx, c.wy, SALT_FIRE_DIR) & 3u);
		const int nx = c.lx + NDX[i], ny = c.ly + NDY[i];
		Cell* n = c.nb.cell(nx, ny);
		if (n && c.mt.get(n->mat).cls == MatClass::Empty) {
			*n = spawn_cell(fire, c.mt, c.seed, c.wx + NDX[i], c.wy + NDY[i]);
			n->stamp = (uint8_t)c.tick;  // as in react: a neighbour spawned this tick is done
			c.nb.mark(nx, ny);
		}
	}

	// The ignition tick counts against burn_life, so wood with burn_life 60 is air 60 ticks
	// after it lights. Guarding on 1 keeps a burn_life 0 cell from wrapping the uint8.
	if (c.self.life > 1) {
		--c.self.life;
	} else {
		const MatId into = def.burns_into == NO_MAT ? MAT_AIR : def.burns_into;
		c.self = spawn_cell(into, c.mt, c.seed, c.wx, c.wy);
	}
	c.nb.mark(c.lx, c.ly);
}

// Lifetime. `life` starts at the material's lifetime, so 1 is the last tick; going below
// would wrap the uint8 counter. (life is shared with the burn timer: a material must not
// define both a burn_life and a lifetime, and none does.)
void decay(Chem& c) {
	const CompiledMaterial& def = c.mt.get(c.self.mat);
	if (def.lifetime == 0) return;
	if (c.self.life > 1) {
		--c.self.life;
	} else {
		const MatId into = def.decays_into == NO_MAT ? MAT_AIR : def.decays_into;
		c.self = spawn_cell(into, c.mt, c.seed, c.wx, c.wy);
	}
	c.nb.mark(c.lx, c.ly);
}

}  // namespace

void update_cell(Neighborhood& nb, int lx, int ly, uint32_t tick, uint32_t seed,
                 const MaterialTable& mt) {
	Cell* self = nb.cell(lx, ly);
	if (!self) return;
	if (self->mat == MAT_AIR) return;
	if (self->stamp == (uint8_t)tick) return;  // already moved/updated this tick

	const ChunkCoord centre = nb.c[1][1]->coord;
	const CompiledMaterial& def = mt.get(self->mat);
	if (def.cls == MatClass::Powder || def.cls == MatClass::Liquid || def.cls == MatClass::Gas) {
		// The cell is only ever handed to us from nb's centre chunk, so its world position
		// (the hash key) comes from that chunk's coord.
		Mover m{nb, mt, def, tick, seed, centre.x * CHUNK + lx, centre.y * CHUNK + ly};
		if (def.cls == MatClass::Powder) move_powder(m, lx, ly);
		else if (def.cls == MatClass::Liquid) move_liquid(m, lx, ly);
		else move_gas(m, lx, ly);
	}
	// Solid: never moves.

	// Chemistry, in stage order, on the cell's final position for this tick: the move helpers
	// update lx/ly in place, and a swap leaves `self` pointing at whatever took its place, so
	// the cell is re-fetched. A reaction can replace the material, so every later stage is
	// allowed to read a different material than the one movement saw.
	self = nb.cell(lx, ly);
	if (!self) return;
	Chem c{nb, mt, *self, tick, seed, lx, ly, centre.x * CHUNK + lx, centre.y * CHUNK + ly};
	react(c);         // first: its products take their own defaults, mark both cells
	exchange_heat(c);  // then heat, so a freshly made cell conducts with its new material
	phase_change(c);   // thresholds are read after the exchange, so a tick can both cool and boil
	burn(c);           // ignition uses the post-exchange temperature and can outlive the phase
	decay(c);          // last: burning owns `life` while FLAG_BURNING is set
}
