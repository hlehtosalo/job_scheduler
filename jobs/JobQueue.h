#pragma once

#include <atomic>
#include <limits>
#include <cassert>

#include "Config.h"

namespace jobs {

	struct Job;

	// Fixed-capacity lock-free work-stealing deque. Based on the paper "Correct and Efficient Work-Stealing for Weak Memory Models"
	// by Nhat Minh Lê, Antoniu Pop, Albert Cohen and Francesco Zappa Nardelli.
	class JobQueue {
	public:
		using Size = int32_t;
		static_assert(std::is_signed_v<Size>, "Size needs to be signed for JobQueue to work correctly.");
		using AtomicSize = std::atomic<Size>;
		static constexpr Size size_max = std::numeric_limits<Size>::max();
		using AtomicJobPtr = std::atomic<Job*>;
		static_assert(AtomicSize::is_always_lock_free && AtomicJobPtr::is_always_lock_free, "JobQueue will work without this, but may not be lock-free. It wants to be lock-free.");

		void reset();
		bool push(Job* job);
		Job* pop();
		Job* steal();

	private:
		AtomicJobPtr ring_buffer[queue_capacity];
		alignas(cacheline_size) AtomicSize top = 0;
		alignas(cacheline_size) AtomicSize bottom = 0;
	};

	inline void JobQueue::reset() {
		bottom.store(0, std::memory_order::seq_cst);
		top.store(0, std::memory_order::seq_cst);
	}

	inline bool JobQueue::push(Job* job) {
		const Size local_bottom = bottom.load(std::memory_order::relaxed);
		assert(local_bottom < size_max);
		const Size local_top = top.load(std::memory_order::acquire);
		if (local_bottom - local_top == queue_capacity) {
			return false;
		}
		ring_buffer[local_bottom % queue_capacity].store(job, std::memory_order::relaxed);
		std::atomic_thread_fence(std::memory_order::release);
		bottom.store(local_bottom + 1, std::memory_order::relaxed);
		return true;
	}

	inline Job* JobQueue::pop() {
		const Size local_bottom = bottom.load(std::memory_order::relaxed) - 1;
		bottom.store(local_bottom, std::memory_order::relaxed);
		std::atomic_thread_fence(std::memory_order::seq_cst);
		Size local_top = top.load(std::memory_order::relaxed);
		if (local_bottom < local_top) {
			bottom.store(local_bottom + 1, std::memory_order::relaxed);
			return nullptr;
		}
		Job* job = ring_buffer[local_bottom % queue_capacity].load(std::memory_order::relaxed);
		if (local_bottom > local_top) {
			return job;
		}
		if (!top.compare_exchange_strong(local_top, local_top + 1, std::memory_order::seq_cst, std::memory_order::relaxed)) {
			bottom.store(local_bottom + 1, std::memory_order::relaxed);
			return nullptr;
		}
		bottom.store(local_bottom + 1, std::memory_order::relaxed);
		return job;
	}

	inline Job* JobQueue::steal() {
		Size local_top = top.load(std::memory_order::acquire);
		std::atomic_thread_fence(std::memory_order::seq_cst);
		const Size local_bottom = bottom.load(std::memory_order::acquire);
		if (local_top >= local_bottom) {
			return nullptr;
		}
		Job* job = ring_buffer[local_top % queue_capacity].load(std::memory_order::relaxed);
		if (!top.compare_exchange_strong(local_top, local_top + 1, std::memory_order::seq_cst, std::memory_order::relaxed)) {
			return nullptr;
		}
		return job;
	}

}