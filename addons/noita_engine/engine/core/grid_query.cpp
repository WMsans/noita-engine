#include "grid_query.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "material.h"
#include "particles.h"
#include "raycast.h"
#include "world_grid.h"

namespace {

constexpr float EDGE = 1e-4f;  // box edges on a cell boundary don't cover the next cell

int first_cell(float edge) { return (int)std::floor(edge + EDGE); }
int last_cell(float edge) { return (int)std::floor(edge - EDGE); }

// The box's cells, as an inclusive range.
struct Cells {
	int x0, y0, x1, y1;
	Cells(float x, float y, int w, int h)
	    : x0(first_cell(x - w * 0.5f)), y0(first_cell(y - h * 0.5f)), x1(last_cell(x + w * 0.5f)),
	      y1(last_cell(y + h * 0.5f)) {}
};
}  // namespace

bool cell_blocks(const WorldGrid& grid, int x, int y, uint32_t mask) {
	Cell c{};
	if (!grid.get(x, y, c)) return true;
	if (c.flags & FLAG_BODY) return false;
	return (class_bit(grid.materials().get(c.mat).cls) & mask) != 0;
}

bool box_blocked(const WorldGrid& grid, float x, float y, int w, int h, uint32_t mask) {
	const Cells c(x, y, w, h);
	for (int cy = c.y0; cy <= c.y1; ++cy)
		for (int cx = c.x0; cx <= c.x1; ++cx)
			if (cell_blocks(grid, cx, cy, mask)) return true;
	return false;
}

int box_sides(const WorldGrid& grid, float x, float y, int w, int h, uint32_t mask) {
	const Cells c(x, y, w, h);
	int sides = 0;
	for (int cx = c.x0; cx <= c.x1; ++cx) {
		if (cell_blocks(grid, cx, c.y1 + 1, mask)) sides |= SIDE_FLOOR;
		if (cell_blocks(grid, cx, c.y0 - 1, mask)) sides |= SIDE_CEILING;
	}
	for (int cy = c.y0; cy <= c.y1; ++cy) {
		if (cell_blocks(grid, c.x0 - 1, cy, mask)) sides |= SIDE_LEFT;
		if (cell_blocks(grid, c.x1 + 1, cy, mask)) sides |= SIDE_RIGHT;
	}
	return sides;
}

BoxSweep sweep_box(const WorldGrid& grid, float x0, float y0, float x1, float y1, int w, int h,
                   uint32_t mask) {
	BoxSweep r;
	float x = x0, y = y0;
	auto free_at = [&](float bx, float by) { return !box_blocked(grid, bx, by, w, h, mask); };

	// Horizontal.
	const float dx = x1 - x0;
	const int nx = std::max(1, (int)std::ceil(std::fabs(dx)));
	for (int i = 0; i < nx && dx != 0; ++i) {
		const float next = x + dx / nx;
		if (free_at(next, y)) {
			x = next;
			continue;
		}
		// Up against the column we just ran into.
		if (dx > 0) x = std::max(x, (float)last_cell(next + w * 0.5f) - w * 0.5f);
		else x = std::min(x, (float)(first_cell(next - w * 0.5f) + 1) + w * 0.5f);
		r.stopped_x = true;
		break;
	}

	// Vertical, from the position x ended at.
	const float dy = y1 - y0;
	const int ny = std::max(1, (int)std::ceil(std::fabs(dy)));
	for (int i = 0; i < ny && dy != 0; ++i) {
		const float next = y + dy / ny;
		if (free_at(x, next)) {
			y = next;
			continue;
		}
		if (dy > 0) y = std::max(y, (float)last_cell(next + h * 0.5f) - h * 0.5f);
		else y = std::min(y, (float)(first_cell(next - h * 0.5f) + 1) + h * 0.5f);
		r.stopped_y = true;
		break;
	}

	r.x = x;
	r.y = y;
	r.sides = box_sides(grid, x, y, w, h, mask);
	return r;
}

BoxScan scan_box(const WorldGrid& grid, float x, float y, int w, int h) {
	const MaterialTable& mt = grid.materials();
	const Cells c(x, y, w, h);
	BoxScan out;
	std::map<MatId, int> liquids;
	for (int cy = c.y0; cy <= c.y1; ++cy)
		for (int cx = c.x0; cx <= c.x1; ++cx) {
			Cell cell{};
			if (!grid.get(cx, cy, cell) || (cell.flags & FLAG_BODY)) continue;
			switch (mt.get(cell.mat).cls) {
				case MatClass::Solid: ++out.solid; break;
				case MatClass::Powder: ++out.powder; break;
				case MatClass::Liquid:
					++out.liquid;
					++liquids[cell.mat];
					break;
				default: break;
			}
		}
	// Most common; the ordered map breaks ties to the lower material id, not scan order.
	int best_count = 0;
	for (const auto& entry : liquids)
		if (entry.second > best_count) {
			out.liquid_mat = entry.first;
			best_count = entry.second;
		}
	return out;
}

int displace_box(WorldGrid& grid, ParticlePool& pool, float x, float y, int w, int h, uint32_t mask,
                 float vx, float vy, int max_cells, bool outward, float spawn_dy) {
	const MaterialTable& mt = grid.materials();
	const Cells c(x, y, w, h);
	int displaced = 0;
	for (int cy = c.y0; cy <= c.y1 && displaced < max_cells; ++cy)
		for (int cx = c.x0; cx <= c.x1 && displaced < max_cells; ++cx) {
			Cell cell{};
			if (!grid.get(cx, cy, cell) || (cell.flags & FLAG_BODY)) continue;
			if (!(class_bit(mt.get(cell.mat).cls) & mask)) continue;
			grid.set(cx, cy, MAT_AIR);
			float cell_vx = vx;
			if (outward) cell_vx = cx + 0.5f < x ? -std::fabs(vx) : std::fabs(vx);
			pool.emit(cx + 0.5f, cy + spawn_dy, cell_vx, vy, cell);
			++displaced;
		}
	return displaced;
}
