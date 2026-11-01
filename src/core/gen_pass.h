#pragma once
#include <cstdint>
#include <vector>

#include "cell.h"

// A padded square of resolved terrain: world cells [x0, x0 + size) x [y0, y0 + size).
struct GenRegion {
	int x0 = 0, y0 = 0, size = 0;
	std::vector<MatId> mat;      // size*size, row-major
	std::vector<uint8_t> flags;  // OR-ed into the emitted cells

	MatId mat_at(int wx, int wy) const {
		return mat[(size_t)(wy - y0) * size + (wx - x0)];
	}
	uint8_t& flags_at(int wx, int wy) { return flags[(size_t)(wy - y0) * size + (wx - x0)]; }
};

// One generation step that may look at neighbouring chunks. A pass must leave correct
// results in every cell at least margin() in from the region edge; nearer cells may be wrong.
// The region margin is the sum of all passes' margins.
class GenPass {
public:
	virtual ~GenPass() = default;
	virtual int margin() const = 0;
	virtual void apply(GenRegion&) const = 0;
};
