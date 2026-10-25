#pragma once
#include <array>
#include <atomic>
#include <cassert>

#include "cell.h"
#include "coords.h"

// Inclusive local-coordinate rectangle; empty while x1 < x0. The empty sentinel sits
// outside [0, CHUNK) on both sides, so plain min/max include() also fills an empty rect.
struct DirtyRect {
	int x0 = CHUNK, y0 = CHUNK, x1 = -1, y1 = -1;

	bool empty() const { return x1 < x0; }

	void include(int lx, int ly) {
		assert(lx >= 0 && lx < CHUNK && ly >= 0 && ly < CHUNK);  // debug only
		if (lx < x0) x0 = lx;
		if (lx > x1) x1 = lx;
		if (ly < y0) y0 = ly;
		if (ly > y1) y1 = ly;
	}

	void reset() { *this = DirtyRect(); }
};

struct Chunk {
	ChunkCoord coord{0, 0};
	std::array<Cell, CHUNK * CHUNK> cells;
	DirtyRect cur, next;  // cur = processed this tick; next = accumulated for the next one
	std::atomic_flag next_lock = ATOMIC_FLAG_INIT;  // guards `next`; other chunks' workers write it
	std::atomic<bool> render_dirty{true};

	Cell& at(int lx, int ly) {
		assert(lx >= 0 && lx < CHUNK && ly >= 0 && ly < CHUNK);  // debug only
		return cells[(size_t)ly * CHUNK + lx];
	}

	// Spinlocked: a neighbour chunk's worker may mark `next` at the same moment we do.
	void mark_next(int lx, int ly) {
		while (next_lock.test_and_set(std::memory_order_acquire)) {
		}
		next.include(lx, ly);
		next_lock.clear(std::memory_order_release);
	}
};
