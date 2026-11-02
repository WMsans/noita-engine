#pragma once

#include <cstdint>

#include "cell.h"
#include "material.h"
#include "world_grid.h"

// A cell while it moves. wx/wy is its world position when the update started and the only
// hash key, so a choice never depends on how far the cell has already travelled.
struct Mover {
	Neighborhood& nb;
	const MaterialTable& mt;
	const CompiledMaterial& mat;
	uint32_t tick, seed;
	int wx, wy;
};

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

// The cell's chemistry for this tick, in stage order: reaction, heat exchange, phase
// change, burn, decay.
void run_chemistry(Chem& c);
