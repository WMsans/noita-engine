#pragma once
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "chunk.h"
#include "coords.h"
#include "generator.h"

struct StreamStats {
	int generated = 0;
	double gen_ms_total = 0;
};

// Background chunk generator. The caller (main thread) publishes the whole set of chunks it
// still wants before each frame; the workers pick the nearest unclaimed one, so the chunks
// around the follow target arrive first. All methods except the constructor and destructor
// belong to the main thread.
class ChunkStreamer {
public:
	// threads < 1 is treated as 1. Each worker lowers its own OS priority on start. `gen` must
	// outlive the streamer and be safe for concurrent generate().
	ChunkStreamer(const ChunkGenerator& gen, uint32_t seed, int threads);
	~ChunkStreamer();  // stops and joins; in-flight work finishes and is discarded
	ChunkStreamer(const ChunkStreamer&) = delete;
	ChunkStreamer& operator=(const ChunkStreamer&) = delete;

	// Replaces the whole request: `missing` are coords the caller wants and hasn't loaded.
	// Workers take the request entry nearest to `centre` (squared distance in chunks) that is
	// neither in flight nor ready.
	void request(const std::vector<ChunkCoord>& missing, ChunkCoord centre);
	// Up to `max` finished chunks that are in the latest request; finished chunks no longer
	// requested are dropped here.
	std::vector<std::unique_ptr<Chunk>> take_ready(int max);
	void wait_idle();  // blocks until every requested coord is ready (none queued or in flight)
	int pending() const;      // requested coords not yet ready (queued + in flight)
	StreamStats take_stats();  // since the previous call, then reset

private:
	enum class State { Queued, InFlight, Ready };
	struct Entry {
		ChunkCoord coord;
		State state;
		std::unique_ptr<Chunk> ready;
	};

	void worker_loop();
	int pick_locked() const;  // index of the nearest unclaimed entry, -1 if none
	bool idle_locked() const;

	const ChunkGenerator& gen_;
	const uint32_t seed_;
	std::vector<std::thread> threads_;

	mutable std::mutex mutex_;          // guards every member below
	std::condition_variable work_;      // main -> workers: the request changed
	std::condition_variable drained_;   // workers -> main: nothing queued or in flight
	std::vector<Entry> req_;            // the latest request, in state order it was published
	std::set<int64_t> delivered_;       // coords handed out by take_ready, keyed by chunk_key
	ChunkCoord centre_{0, 0};
	StreamStats stats_;
	bool stop_ = false;
};
