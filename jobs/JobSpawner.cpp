#include "JobSpawner.h"

#include <atomic>
#include <cassert>
#include <cstring>

#include "JobAllocator.h"
#include "JobQueue.h"
#include "JobGraph.h"

namespace jobs {

	void JobSpawner::spawn_impl(JobFunction* function, const void* params, size_t params_size, bool is_sub_job) const {
		Job* job = allocator.allocate();
		assert(job);
		std::memcpy(job->param_buffer, params, params_size);
		job->function = function;
		if (is_sub_job) {
			job->node = node;
			node->job_added();
		}
		else {
			job->node = nullptr;
		}
		const bool pushed = queue.push(job);
		assert(pushed);
	}

}