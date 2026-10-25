#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// Fixed-size worker pool for the chunk-parallel tick. `workers` counts the caller: a pool of
// 1 creates no threads at all and runs every job inline. Destroy the pool only when no
// parallel_for call is in flight.
class JobPool {
public:
	explicit JobPool(int workers);
	JobPool(const JobPool&) = delete;
	JobPool& operator=(const JobPool&) = delete;
	~JobPool();

	// Calls fn(0) .. fn(count-1) exactly once each, spread over the pool's threads and this
	// one, and returns only after all of them have returned. Must not be called from inside
	// `fn`, and only one call may be in flight at a time.
	void parallel_for(int count, const std::function<void(int)>& fn);

	int workers() const { return worker_count_; }
	static int default_workers();

private:
	void run_current_job();
	void worker_loop();

	const int worker_count_;
	std::vector<std::thread> threads_;

	std::mutex mutex_;              // guards the job description below and the generation
	std::condition_variable wake_;  // caller -> workers: a new generation is ready
	std::condition_variable done_;  // workers -> caller: the last worker finished
	const std::function<void(int)>* job_ = nullptr;
	int job_count_ = 0;
	std::atomic<int> next_{0};  // work-stealing cursor into [0, job_count_)
	uint64_t generation_ = 0;
	int pending_ = 0;  // workers still running the current generation
	bool stop_ = false;
};
