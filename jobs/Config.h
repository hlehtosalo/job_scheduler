#pragma once

#include <new>

namespace jobs {

	// Maximum number of Jobs queued in one JobQueue at any given moment. Power of 2 values provide slightly better performance due to JobQueue ring buffer implementation.
	constexpr size_t queue_capacity = 4096;

	// Number of Jobs in one inter-thread allocation. In other words, how many Jobs can be allocated thread-locally between each inter-thread allocation.
	constexpr size_t allocation_chunk_size = 2048;

	// Minimum required size of Job::param_buffer. Actual size is calculated in Job.h to make the total size of Job a multiple of cacheline_size.
	constexpr size_t min_param_buffer_size = 32;

	// Used by JobQueue and in determining the size of Job, to prevent false sharing. Change the value according to target platform if needed.
	constexpr size_t cacheline_size = std::hardware_destructive_interference_size;

}