#include "Job.h"

#include <cassert>

#include "JobAllocator.h"
#include "JobQueue.h"
#include "JobGraph.h"
#include "JobSpawner.h"

namespace jobs {

	void Job::run(JobAllocator& allocator, JobQueue& queue, WorkerInfo& worker_info) const {
		assert(function);
		function(param_buffer, JobSpawner(allocator, queue, node), worker_info);
		if (node) {
			node->job_completed(queue);
		}
	}

}