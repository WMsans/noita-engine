#pragma once
#include <cstdint>

#include "material.h"
#include "world_grid.h"

// Runs one cell's rules: movement now, chemistry from Task 5 on.
// lx and ly are local to nb's centre chunk (so they start in [0, CHUNK) and may leave that
// range during the call, because a mover can travel up to 16 cells).
void update_cell(Neighborhood& nb, int lx, int ly, uint32_t tick, uint32_t seed,
                 const MaterialTable& mt);
