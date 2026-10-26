#include "creature.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "material.h"
#include "particles.h"
#include "world_grid.h"

namespace {

constexpr float EDGE = 1e-4f;  // box edges on a cell boundary don't cover the next cell
constexpr float SPLASH_SPEED = 60.0f;  // cells/s
constexpr float KNOCK_SPEED = 2.0f;    // cells/tick, powder knocked out of a box

int first_cell(float edge) { return (int)std::floor(edge + EDGE); }
int last_cell(float edge) { return (int)std::floor(edge - EDGE); }

// The box's cells, as an inclusive range.
struct Cells {
	int x0, y0, x1, y1;
	Cells(float x, float y, int w, int h)
	    : x0(first_cell(x - w * 0.5f)), y0(first_cell(y - h * 0.5f)), x1(last_cell(x + w * 0.5f)),
	      y1(last_cell(y + h * 0.5f)) {}
};

template <class F>
bool any_cell(float x, float y, int w, int h, F&& test) {
	const Cells c(x, y, w, h);
	for (int cy = c.y0; cy <= c.y1; ++cy)
		for (int cx = c.x0; cx <= c.x1; ++cx)
			if (test(cx, cy)) return true;
	return false;
}

bool is_solid(const WorldGrid& grid, int x, int y) {
	Cell c{};
	if (!grid.get(x, y, c)) return true;
	return !(c.flags & FLAG_BODY) && grid.materials().get(c.mat).cls == MatClass::Solid;
}

}  // namespace

bool creature_blocks(const WorldGrid& grid, int x, int y) {
	Cell c{};
	if (!grid.get(x, y, c)) return true;
	if (c.flags & FLAG_BODY) return false;
	const MatClass cls = grid.materials().get(c.mat).cls;
	return cls == MatClass::Solid || cls == MatClass::Powder;
}

GridPass creature_grid_pass(WorldGrid& grid, ParticlePool* pool, float x0, float y0, float x1, float y1,
                            int w, int h, int step_height, bool can_step, float speed) {
	GridPass r;
	float x = x0, y = y0;
	auto free_at = [&](float bx, float by) {
		return !any_cell(bx, by, w, h, [&](int cx, int cy) { return creature_blocks(grid, cx, cy); });
	};

	// Horizontal, with step-up.
	const float dx = x1 - x0;
	const int nx = std::max(1, (int)std::ceil(std::fabs(dx)));
	for (int i = 0; i < nx && dx != 0; ++i) {
		const float next = x + dx / nx;
		if (free_at(next, y)) {
			x = next;
			continue;
		}
		bool stepped = false;
		for (int k = 1; can_step && k <= step_height && !stepped; ++k)
			if (free_at(x, y - k) && free_at(next, y - k)) {
				y -= k;
				x = next;
				stepped = true;
			}
		if (stepped) continue;
		// Up against the column we just ran into.
		if (dx > 0) x = std::max(x, (float)last_cell(next + w * 0.5f) - w * 0.5f);
		else x = std::min(x, (float)(first_cell(next - w * 0.5f) + 1) + w * 0.5f);
		r.wall = dx > 0 ? 1 : -1;
		r.stopped_x = true;
		break;
	}

	// Vertical.
	const float dy = y1 - y0;
	const int ny = std::max(1, (int)std::ceil(std::fabs(dy)));
	for (int i = 0; i < ny && dy != 0; ++i) {
		const float next = y + dy / ny;
		if (free_at(x, next)) {
			y = next;
			continue;
		}
		if (dy > 0) {
			y = std::max(y, (float)last_cell(next + h * 0.5f) - h * 0.5f);
			r.floor = true;
		} else {
			y = std::min(y, (float)(first_cell(next - h * 0.5f) + 1) + h * 0.5f);
			r.ceiling = true;
		}
		r.stopped_y = true;
		break;
	}

	// Buried in solid (painted over, or shoved in): rise out, up to the box's height.
	if (any_cell(x, y, w, h, [&](int cx, int cy) { return is_solid(grid, cx, cy); }))
		for (int k = 1; k <= h; ++k)
			if (!any_cell(x, y - k, w, h, [&](int cx, int cy) { return is_solid(grid, cx, cy); })) {
				y -= k;
				break;
			}

	// Powder inside the box (it fell in, or the box was shoved into a pile) is knocked out.
	const MaterialTable& mt = grid.materials();
	std::unordered_map<MatId, int> liquids;
	const Cells cells(x, y, w, h);
	for (int cy = cells.y0; cy <= cells.y1; ++cy)
		for (int cx = cells.x0; cx <= cells.x1; ++cx) {
			Cell c{};
			if (!grid.get(cx, cy, c) || (c.flags & FLAG_BODY)) continue;
			const MatClass cls = mt.get(c.mat).cls;
			if (cls == MatClass::Liquid) {
				++r.submerged;
				++liquids[c.mat];
			} else if (cls == MatClass::Powder && pool) {
				grid.set(cx, cy, MAT_AIR);
				const float away = cx + 0.5f < x ? -KNOCK_SPEED : KNOCK_SPEED;
				pool->emit(cx + 0.5f, cy + 0.5f, away, -KNOCK_SPEED, c);
			}
		}
	for (const auto& [mat, count] : liquids)
		if (r.liquid == MAT_AIR || count > liquids[r.liquid]) r.liquid = mat;

	// Splash: a few liquid cells from the box's top rows thrown up and along.
	if (pool && r.submerged > 0 && speed > SPLASH_SPEED) {
		int left = std::min(3, (int)(speed / SPLASH_SPEED));
		const float along = x1 > x0 ? 1.5f : x1 < x0 ? -1.5f : 0.0f;
		for (int cy = cells.y0; cy <= cells.y1 && left > 0; ++cy)
			for (int cx = cells.x0; cx <= cells.x1 && left > 0; ++cx) {
				Cell c{};
				if (!grid.get(cx, cy, c) || (c.flags & FLAG_BODY) || mt.get(c.mat).cls != MatClass::Liquid) continue;
				grid.set(cx, cy, MAT_AIR);
				pool->emit(cx + 0.5f, cy - 0.5f, along, -3.0f, c);
				--left;
			}
	}

	// Standing still on something counts as the floor.
	if (!r.floor) {
		const int below = last_cell(y + h * 0.5f) + 1;
		for (int cx = cells.x0; cx <= cells.x1 && !r.floor; ++cx) r.floor = creature_blocks(grid, cx, below);
	}
	r.x = x;
	r.y = y;
	return r;
}
