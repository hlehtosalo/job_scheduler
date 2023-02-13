#pragma once

#include <atomic>
#include <vector>
#include <limits>
#include <cassert>

#include "Config.h"
#include "Job.h"

namespace jobs {

	// Used internally by the allocators to make things simpler.
	struct JobChunk {
		Job buffer[allocation_chunk_size];
	};

	// Lock-free linear allocator of JobChunks. Used by a set of thread-local JobAllocators.
	class JobChunkAllocator {
	public:
		using Size = uint32_t;
		using AtomicSize = std::atomic<Size>;
		static constexpr Size size_max = std::numeric_limits<Size>::max();
		static_assert(AtomicSize::is_always_lock_free, "JobChunkAllocator will work without this, but may not be lock-free. It wants to be lock-free.");

		JobChunkAllocator(Size chunk_amount) : chunks(chunk_amount) {}
		JobChunkAllocator(const JobChunkAllocator&) = delete;
		JobChunkAllocator(JobChunkAllocator&&) = delete;
		JobChunkAllocator& operator=(const JobChunkAllocator&) = delete;
		JobChunkAllocator& operator=(JobChunkAllocator&&) = delete;
		JobChunk* allocate();
		void reset();

	private:
		std::vector<JobChunk> chunks;
		AtomicSize next_index = 0;
	};

	inline JobChunk* JobChunkAllocator::allocate() {
		const Size index = next_index.fetch_add(1, std::memory_order::relaxed);
		assert(index < size_max);
		if (index >= chunks.size()) {
			return nullptr;
		}
		return chunks.data() + index;
	}

	inline void JobChunkAllocator::reset() {
		next_index.store(0, std::memory_order::seq_cst);
	}

	// Linear allocator of Jobs. Each worker thread should have one, don't share between threads. When it runs out of Jobs, gets a new chunk from given JobChunkAllocator.
	class JobAllocator {
	public:
		JobAllocator(JobChunkAllocator& chunk_allocator) : chunk_allocator(chunk_allocator) {}
		JobAllocator(const JobAllocator&) = delete;
		JobAllocator(JobAllocator&&) = delete;
		JobAllocator& operator=(const JobAllocator&) = delete;
		JobAllocator& operator=(JobAllocator&&) = delete;
		Job* allocate();
		void reset();

	private:
		JobChunk* chunk = nullptr;
		uint32_t next_index = 0;
		JobChunkAllocator& chunk_allocator;
	};

	inline Job* JobAllocator::allocate() {
		if (!chunk) {
			chunk = chunk_allocator.allocate();
			if (!chunk) {
				return nullptr;
			}
			next_index = 0;
		}
		Job* job = chunk->buffer + next_index;
		if (++next_index == allocation_chunk_size) {
			chunk = nullptr;
		}
		return job;
	}

	inline void JobAllocator::reset() {
		chunk = nullptr;
	}

}