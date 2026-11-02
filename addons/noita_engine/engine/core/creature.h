#pragma once
#include <cstdint>

#include "cell.h"

class WorldGrid;
class ParticlePool;

// What the grid pass did to a creature's hitbox this tick.
struct GridPass {
	float x, y;  // corrected centre
	bool floor = false, ceiling = false;
	int wall = 0;                               // -1 left, +1 right, 0 none
	bool stopped_x = false, stopped_y = false;  // the sweep hit something on that axis
	int submerged = 0;                          // liquid cells in the hitbox
	MatId liquid = MAT_AIR;                     // the most common of them
};

// Solid or powder, or an unloaded chunk. Liquid, gas and stamped body pixels never block.
bool creature_blocks(const WorldGrid& grid, int x, int y);

// Moves a w x h box centred at (x0, y0) to (x1, y1): x first, then y, at most one cell per
// sub-step, so nothing is tunnelled through. A blocked horizontal sub-step first tries to rise
// 1..step_height cells (only when `can_step`). Then: a box left inside solid cells rises out
// (up to h cells), powder inside it is knocked out as particles (with a pool), liquid is
// counted, and moving faster than 60 cells/s (`speed`) through liquid throws a splash.
GridPass creature_grid_pass(WorldGrid& grid, ParticlePool* pool, float x0, float y0, float x1, float y1,
                            int w, int h, int step_height, bool can_step, float speed);
