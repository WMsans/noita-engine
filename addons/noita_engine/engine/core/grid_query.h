#pragma once
#include <cstdint>

#include "cell.h"

class WorldGrid;
class ParticlePool;

// Which sides of a box touch a blocking cell, as bits.
enum BoxSide : int { SIDE_FLOOR = 1, SIDE_CEILING = 2, SIDE_LEFT = 4, SIDE_RIGHT = 8 };

// Largest w or h a batched query accepts; the bindings reject anything outside 1..this.
constexpr int MAX_QUERY_BOX = 64;

// A cell of the class selected by the RAY_* mask of raycast.h. An unloaded chunk always
// blocks, a stamped body pixel never does.
bool cell_blocks(const WorldGrid& grid, int x, int y, uint32_t mask);

// Any cell of the w x h box centred at (x, y) blocks. Box edges on a cell boundary cover no
// extra cell.
bool box_blocked(const WorldGrid& grid, float x, float y, int w, int h, uint32_t mask);

// SIDE_* bits for the row right under (floor) and right above (ceiling) the box, and the
// columns right of and left of it, one cell out.
int box_sides(const WorldGrid& grid, float x, float y, int w, int h, uint32_t mask);

// Where a sweep ended and what it touched.
struct BoxSweep {
	float x = 0, y = 0;
	bool stopped_x = false, stopped_y = false;
	int sides = 0;  // SIDE_* at the final position
};

// Moves the w x h box from (x0, y0) to (x1, y1): x first, then y, at most one cell per
// sub-step, so nothing is tunnelled through. A blocked sub-step leaves the box flush against
// the obstacle on that axis.
BoxSweep sweep_box(const WorldGrid& grid, float x0, float y0, float x1, float y1, int w, int h,
                   uint32_t mask);

// Cell counts inside the w x h box centred at (x, y). Body pixels are skipped; liquid_mat is
// the most common liquid, MAT_AIR when there is none.
struct BoxScan {
	int solid = 0, powder = 0, liquid = 0;
	MatId liquid_mat = MAT_AIR;
};
BoxScan scan_box(const WorldGrid& grid, float x, float y, int w, int h);

// Replaces up to max_cells cells matching `mask` with air, top row first then left to right,
// and emits each as a particle at (cx + 0.5, cy + spawn_dy) in cells and cells/tick. With
// `outward` the x velocity becomes |vx| with the cell's side of the box centre as its sign
// (left half negative); without it the velocity is used as given. Returns the cells displaced.
int displace_box(WorldGrid& grid, ParticlePool& pool, float x, float y, int w, int h, uint32_t mask,
                 float vx, float vy, int max_cells, bool outward, float spawn_dy);
