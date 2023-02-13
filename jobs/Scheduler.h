#pragma once

#include <memory>
#include <vector>
#include <atomic>
#include <barrier>
#include <iostream>

namespace std {
	class thread;
}

namespace jobs {

	struct Job;
	class JobChunkAllocator;
	class JobGraph;

	class Scheduler {
	public:
		using Size = uint32_t;
		using AtomicSize = std::atomic<Size>;
		static_assert(AtomicSize::is_always_lock_free, "Scheduler will work without this, but may not be lock-free. It wants to be lock-free.");

		// Spawns [desired_worker_amount - 1] threads. (The calling thread will be a worker as well)
		Scheduler(Size desired_worker_amount, Size desired_allocation_chunk_amount);
		~Scheduler();
		// Sets the dependency graph to be run. Can be changed between calls to run().
		void set_job_graph(const JobGraph* graph);
		// Runs the currently set dependency graph. Blocks until all Jobs are completed (The calling thread participates in the work as well).
		void run();
		void write_statistics(std::ostream& out_stream) const;
		void reset_statistics();
		Size get_worker_amount() const { return worker_amount; }

	private:
		struct Worker;

		enum class State : Size {
			Wait,
			Work,
			Quit
		};
		using AtomicState = std::atomic<State>;
		static_assert(AtomicState::is_always_lock_free, "Scheduler will work without this, but may not be lock-free. It wants to be lock-free.");

		void thread_loop(Size worker_index);
		void create_worker(Size index);
		void run_worker(Size index);
		void work_loop(Worker& worker);

		Size worker_amount;
		std::vector<std::unique_ptr<Worker>> workers;
		std::vector<std::thread> threads;
		std::unique_ptr<JobChunkAllocator> chunk_allocator;
		const JobGraph* job_graph = nullptr;
		// Barrier to sync all workers at the beginning and end of a single run.
		std::barrier<> sync_point;
		AtomicState state = State::Wait;
		// Number of workers that are currently stealing. (When all workers are stealing, it means there is no more work to do)
		AtomicSize stealer_amount;
		// Number of workers that are working or stealing. Used as a double-check to make sure all workers agree on whether all work is done.
		AtomicSize active_amount;
	};

}