#include "Scheduler.h"

#include <thread>
#include <random>
#include <algorithm>
#include <cassert>

#include "Config.h"
#include "Job.h"
#include "JobQueue.h"
#include "JobAllocator.h"
#include "JobGraph.h"
#include "Statistics.h"

namespace jobs {

	struct Scheduler::Worker {
		Worker(Size index, Size worker_amount, JobChunkAllocator& chunk_allocator)
			: job_allocator(chunk_allocator)
			, random_generator(0xbabe + index)
			, steal_distribution(1u + index, std::max(worker_amount - 1u, 1u) + index)
			, statistics(index) {}

		JobAllocator job_allocator;
		JobQueue job_queue;
		std::minstd_rand random_generator;
		std::uniform_int_distribution<Size> steal_distribution;
		WorkerStatistics statistics;
	};

	Scheduler::Scheduler(Size desired_worker_amount, Size desired_allocation_chunk_amount) : worker_amount(std::max(desired_worker_amount, 1u)), workers(worker_amount), sync_point(worker_amount) {
		chunk_allocator.reset(new JobChunkAllocator(std::max(desired_allocation_chunk_amount, worker_amount)));
		threads.reserve(worker_amount - 1);
		for (Size i = 1; i != worker_amount; i++) {
			threads.emplace_back(&Scheduler::thread_loop, this, i);
		}
		create_worker(0);
	}

	Scheduler::~Scheduler() {
		state.store(State::Quit, std::memory_order::seq_cst);
		state.notify_all();
		for (std::thread& thread : threads) {
			thread.join();
		}
	}

	void Scheduler::set_job_graph(const JobGraph* graph) {
		job_graph = graph;
	}

	void Scheduler::run() {
		assert(job_graph);
		state.store(State::Work, std::memory_order::seq_cst);
		state.notify_all();
		stealer_amount.store(0, std::memory_order::seq_cst);
		active_amount.store(worker_amount, std::memory_order::seq_cst);

		run_worker(0);

		chunk_allocator->reset(); // JobChunkAllocator::reset(), not unique_ptr::reset().
	}

	void Scheduler::write_statistics(std::ostream& out_stream) const {
		for (auto& worker : workers) {
			worker->statistics.write(out_stream);
		}
	}

	void Scheduler::reset_statistics() {
		for (auto& worker : workers) {
			worker->statistics.reset();
		}
	}

	void Scheduler::thread_loop(Size worker_index) {
		create_worker(worker_index);
		for (;;) {
			state.wait(State::Wait, std::memory_order::seq_cst);
			if (state.load(std::memory_order::seq_cst) == State::Quit) {
				break;
			}
			run_worker(worker_index);
		}
	}

	void Scheduler::create_worker(Size index) {
		workers[index].reset(new Worker(index, worker_amount, *chunk_allocator));
	}

	void Scheduler::run_worker(Size index) {
		sync_point.arrive_and_wait();
		Worker& worker = *workers[index];
		const Timer timer;

		// Start by running the root jobs of all root nodes (nodes that do not depend on other nodes).
		for (Size i = index; const Job* root_job = job_graph->get_root_job(i); i += worker_amount) {
			root_job->run(worker.job_allocator, worker.job_queue, worker.statistics.info);
			worker.statistics.add_own_job();
		}
		worker.statistics.add_work_timing(timer);

		// Run jobs as long as there is work to do.
		work_loop(worker);

		if (index == 0) {
			// Safe to set the state in between the sync_point barriers.
			state.store(State::Wait, std::memory_order::seq_cst);
		}
		worker.statistics.add_total_timing(timer);
		sync_point.arrive_and_wait();
		worker.job_queue.reset();
		worker.job_allocator.reset();
	}

	void Scheduler::work_loop(Worker& worker) {
		for (;;) {
			// Run all jobs in the worker's own queue.
			{
				const Timer timer;
				while (const Job* own_job = worker.job_queue.pop()) {
					own_job->run(worker.job_allocator, worker.job_queue, worker.statistics.info);
					worker.statistics.add_own_job();
				}
				worker.statistics.add_work_timing(timer);
			}

			// Start stealing work from other workers.
			stealer_amount.fetch_add(1, std::memory_order::relaxed);
			for (;;) {
				// Steal from another worker selected at random.
				const Size target_index = worker.steal_distribution(worker.random_generator) % worker_amount;
				if (const Job* stolen_job = workers[target_index]->job_queue.steal()) {
					// Successfully stole a job; first notify potentially waiting workers that there might be more work to be stolen soon.
					if (stealer_amount.fetch_sub(1, std::memory_order::relaxed) == worker_amount) {
						stealer_amount.notify_all();
					}
					const Timer timer;
					stolen_job->run(worker.job_allocator, worker.job_queue, worker.statistics.info);
					worker.statistics.add_stolen_job();
					worker.statistics.add_work_timing(timer);
					// Go back to working on own queue.
					break;
				}
				worker.statistics.add_failed_steal_attempt();

				// If everyone is stealing, it probably means there is no work left. Get ready to finish the run.
				if (stealer_amount.load(std::memory_order::relaxed) >= worker_amount) {
					// The last worker to enter here notifies others that work is indeed done.
					if (active_amount.fetch_sub(1, std::memory_order::seq_cst) == 1) {
						// worker_amount + 1 is used here to mean that everyone is done.
						stealer_amount.store(worker_amount + 1, std::memory_order::seq_cst);
						stealer_amount.notify_all();
					}

					// Wait until stealer_amount changes, either to worker_amount + 1 (all done), or to a smaller value (another worker managed to steal and may now produce more work).
					stealer_amount.wait(worker_amount, std::memory_order::seq_cst);
					if (stealer_amount.load(std::memory_order::seq_cst) > worker_amount) {
						return;
					}

					worker.statistics.add_false_wait();
					active_amount.fetch_add(1, std::memory_order::seq_cst);
				}

				// Yield to reduce contention; honest work is prioritized over stealing.
				std::this_thread::yield();
			}
		}
	}

}