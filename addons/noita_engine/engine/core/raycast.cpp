#include "raycast.h"

#include <cmath>
#include <limits>

#include "world_grid.h"

namespace {

uint32_t class_bit(MatClass cls) {
	switch (cls) {
		case MatClass::Solid: return RAY_SOLID;
		case MatClass::Powder: return RAY_POWDER;
		case MatClass::Liquid: return RAY_LIQUID;
		case MatClass::Gas: return RAY_GAS;
		default: return 0;
	}
}

// One axis of the DDA: the cell step, and the ray parameter t (0..1) of the next border.
struct Axis {
	int step = 0;
	double next = std::numeric_limits<double>::infinity();
	double delta = std::numeric_limits<double>::infinity();

	Axis(double from, double to, int cell) {
		const double d = to - from;
		if (d > 0) {
			step = 1;
			next = (cell + 1 - from) / d;
			delta = 1.0 / d;
		} else if (d < 0) {
			step = -1;
			next = (from - cell) / -d;
			delta = 1.0 / -d;
		}
	}
};

}  // namespace

RayHit raycast(const WorldGrid& grid, float x0, float y0, float x1, float y1, uint32_t mask) {
	RayHit out;
	if (mask == 0) return out;
	int x = (int)std::floor(x0), y = (int)std::floor(y0);

	auto test = [&](int nx, int ny) {
		Cell c{};
		if (!grid.get(x, y, c)) {
			out.unloaded = true;
		} else if (!(class_bit(grid.materials().get(c.mat).cls) & mask)) {
			return false;
		} else {
			out.mat = c.mat;
		}
		out.hit = true;
		out.x = x;
		out.y = y;
		out.nx = nx;
		out.ny = ny;
		return true;
	};

	if (test(0, 0)) return out;
	Axis ax(x0, x1, x), ay(y0, y1, y);
	while (true) {
		if (ax.next <= ay.next) {  // ties step x first
			if (ax.next > 1.0) break;
			x += ax.step;
			ax.next += ax.delta;
			if (test(-ax.step, 0)) return out;
		} else {
			if (ay.next > 1.0) break;
			y += ay.step;
			ay.next += ay.delta;
			if (test(0, -ay.step)) return out;
		}
	}
	return out;
}
