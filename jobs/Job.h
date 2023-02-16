#pragma once

#include <type_traits>

#include "Config.h"

namespace jobs {

	class JobAllocator;
	class JobQueue;
	class JobGraphNode;
	class JobSpawner;
	class WorkerInfo;

	// Jobs use a simple function pointer to avoid virtual call overhead.
	using JobFunction = void(const void*, const JobSpawner&, WorkerInfo&);

	constexpr size_t job_core_size = sizeof(JobFunction*) + sizeof(JobGraphNode*);
	constexpr size_t min_job_size = min_param_buffer_size + job_core_size;
	constexpr size_t job_size = ((min_job_size + cacheline_size - 1) / cacheline_size) * cacheline_size;
	constexpr size_t param_buffer_size = job_size - job_core_size;

	struct alignas(cacheline_size) Job {
		void run(JobAllocator& allocator, JobQueue& queue, WorkerInfo& worker_info) const;

		uint8_t param_buffer[param_buffer_size];
		JobFunction* function;
		JobGraphNode* node;
	};

	static_assert(offsetof(Job, param_buffer) == 0, "param_buffer has to be the first member of Job, to ensure that any parameter data is properly aligned.");
	static_assert(sizeof(Job) == job_size, "job_core_size is probably incorrect. Note that it needs to include padding too.");

}