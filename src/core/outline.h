#pragma once
#include <cstdint>
#include <vector>

#include "box2d/collision.h"
#include "chunk.h"
#include "material.h"

using Mask = std::vector<uint8_t>;  // row-major w*h, 1 = solid
using Loop = std::vector<b2Vec2>;   // closed; the first point is not repeated at the end

// collision_solid && !FLAG_BODY over the chunk's CHUNK*CHUNK cells, except that a powder
// counts only where it rests on solid ground: the cell below is solid in the mask, or, on the
// bottom row, collision-solid in `below` (null = unloaded, which counts as solid). Sand piled
// on a body or falling through the air makes no shapes.
void chunk_solid_mask(const Chunk& chunk, const Chunk* below, const MaterialTable& table, Mask& out);

// Pixel-edge outlines of the solid cells, in cell coords [0,w]x[0,h]; cells outside the mask
// are empty. Collinear points are merged. Winding: Box2D's right normal (d.y, -d.x) of every
// edge points out of the solid, which is what a chain loop needs. Cells touching only at a
// corner belong to separate loops.
std::vector<Loop> trace_loops(const Mask& mask, int w, int h);

// Douglas-Peucker on a closed loop; keeps at least 3 points.
Loop simplify_loop(const Loop& loop, float epsilon);

// Convex hull of the opaque pixels' corners, reduced to at most B2_MAX_POLYGON_VERTICES by
// repeatedly dropping the vertex whose removal loses the least area. Empty if no pixels.
Loop pixel_hull(const Mask& mask, int w, int h);

struct PixelRect {
	int x, y, w, h;
};
// Non-overlapping rectangles covering exactly the solid pixels: greedy row runs, merged down.
std::vector<PixelRect> pixel_boxes(const Mask& mask, int w, int h);
