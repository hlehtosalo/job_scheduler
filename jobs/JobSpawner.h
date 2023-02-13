#pragma once

#include <type_traits>

#include "Job.h"

namespace jobs {

	class JobAllocator;
	class JobQueue;
	class JobGraphNode;

	// Passed to Job functions to allow spawning new Jobs in a safe manner. Takes care of using the correct allocator, pushing to the correct queue,
	// and updating the dependency graph node when a sub-Job is spawned (so that Jobs in dependent nodes are not started prematurely).
	class JobSpawner {
	public:
		JobSpawner(JobAllocator& allocator, JobQueue& queue, JobGraphNode* node) : allocator(allocator), queue(queue), node(node) {}
		// If is_sub_job == true, the spawned job will be completed before the current dependency graph node is considered completed.
		// Otherwise, the spawned Job is not part of the dependency graph (but will still be completed before Scheduler::run() returns).
		template<typename Params>
		void spawn(JobFunction* function, const Params& params, bool is_sub_job) const;

	private:
		void spawn_impl(JobFunction* function, const void* params, size_t params_size, bool is_sub_job) const;

		JobAllocator& allocator;
		JobQueue& queue;
		JobGraphNode* node;
	};

	template<typename Params>
	inline void JobSpawner::spawn(JobFunction* function, const Params& params, bool is_sub_job) const {
		static_assert(sizeof(Params) <= param_buffer_size, "Params has to fit into Job::param_buffer. Data that does not fit needs to be allocated elsewhere and pointed to in Params.");
		static_assert(std::is_trivial_v<Params>, "Params has to be a trivial type. The data is memcpy'd into Job::param_buffer.");
		spawn_impl(function, &params, sizeof(Params), is_sub_job);
	}

}