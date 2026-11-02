#include "rules_common.h"

#include <algorithm>
#include <cstddef>
#include <cmath>

#include "cell.h"
#include "rng.h"

namespace {

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
		if (!n || (n->flags & FLAG_BODY)) continue;  // body pixels never react
		const Reaction* r = c.mt.reaction(c.self.mat, n->mat);
		if (!r) continue;
		// The temperature condition is read from the cell being updated, before any change.
		if (c.self.temp < r->min_temp || c.self.temp > r->max_temp) continue;
		candidate = true;
		if (hash_unit(hash5(c.seed, c.tick, c.wx, c.wy, SALT_REACTION + (uint32_t)i)) >=
		    r->probability)
			continue;
		// An unset *_into is air, as in burn and decay: NO_MAT is not a material id, and a cell
		// holding it is invisible to a name lookup and indexes the renderer's palette out of
		// bounds. The shipped acid reactions and water + fire rely on this.
		const MatId self_into = r->a_into == NO_MAT ? MAT_AIR : r->a_into;
		const MatId other_into = r->b_into == NO_MAT ? MAT_AIR : r->b_into;
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
	if (!n || (n->flags & FLAG_BODY)) return;  // an unloaded chunk holds no heat, nor does a body pixel

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

// Chemistry, in stage order. A reaction can replace the material, so every later stage is
// allowed to read a different material than the one movement saw.
void run_chemistry(Chem& c) {
	react(c);         // first: its products take their own defaults, mark both cells
	exchange_heat(c);  // then heat, so a freshly made cell conducts with its new material
	phase_change(c);   // thresholds are read after the exchange, so a tick can both cool and boil
	burn(c);           // ignition uses the post-exchange temperature and can outlive the phase
	decay(c);          // last: burning owns `life` while FLAG_BURNING is set
}
