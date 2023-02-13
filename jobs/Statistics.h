#pragma once

#include <chrono>
#include <iostream>

namespace jobs {

	class Timer {
	public:
		Timer() : start_time(std::chrono::steady_clock::now()) {}
		std::chrono::nanoseconds get_elapsed() const { return std::chrono::steady_clock::now() - start_time; }

	private:
		const std::chrono::steady_clock::time_point start_time;
	};

	// Passed to Job functions to provide logging/debugging information. See also UserJobLogger below.
	class WorkerInfo {
	public:
		WorkerInfo(uint32_t index) : worker_index(index) {}
		uint32_t get_worker_index() const { return worker_index; }

	private:
		friend class UserJobLogger;
		friend class WorkerStatistics;

		uint32_t worker_index;
		uint32_t user_job_amount = 0;
		std::chrono::nanoseconds user_job_duration = std::chrono::nanoseconds::zero();
	};

	// RAII-style class for logging a user job inside a Job function. (A user job is a job that does actual user-space work, as opposed to e.g. just spawning new jobs.)
	// Other statistics are kept track of automatically, but only the Job function knows when it's doing user-space work.
	class UserJobLogger {
	public:
		UserJobLogger(WorkerInfo& worker_info) : worker_info(worker_info) {}
		UserJobLogger(const UserJobLogger&) = delete;
		UserJobLogger(UserJobLogger&&) = delete;
		UserJobLogger& operator=(const UserJobLogger&) = delete;
		UserJobLogger& operator=(UserJobLogger&&) = delete;
		~UserJobLogger() {
			worker_info.user_job_amount++;
			worker_info.user_job_duration += timer.get_elapsed();
		}

	private:
		WorkerInfo& worker_info;
		const Timer timer;
	};

	// Contains statistics for a single worker. These get written to a stream by Scheduler::write_statistics().
	class WorkerStatistics {
	public:
		WorkerStatistics(uint32_t index) : info(index) {}
		void add_own_job() { own_job_amount++; }
		void add_stolen_job() { stolen_job_amount++; }
		void add_failed_steal_attempt() { failed_steal_amount++; }
		void add_false_wait() { false_wait_amount++; }
		void add_total_timing(const Timer& timer) { total_duration += timer.get_elapsed(); }
		void add_work_timing(const Timer& timer) { work_duration += timer.get_elapsed(); }
		void write(std::ostream& out_stream) const;
		void reset();

		WorkerInfo info;

	private:
		uint32_t own_job_amount = 0;
		uint32_t stolen_job_amount = 0;
		uint64_t failed_steal_amount = 0;
		uint64_t false_wait_amount = 0;
		std::chrono::nanoseconds total_duration = std::chrono::nanoseconds::zero();
		std::chrono::nanoseconds work_duration = std::chrono::nanoseconds::zero();
	};

	inline void WorkerStatistics::write(std::ostream& out_stream) const {
		const uint32_t total_job_amount = own_job_amount + stolen_job_amount;
		const uint32_t admin_job_amount = total_job_amount - info.user_job_amount;
		out_stream << "Worker " << info.worker_index << "\n";
		out_stream << "\tExecuted " << total_job_amount << " jobs\n";
		out_stream << "\t\t* " << own_job_amount << " own, " << stolen_job_amount << " stolen\n";
		out_stream << "\t\t* " << info.user_job_amount << " user jobs, " << admin_job_amount << " admin jobs\n";
		out_stream << "\tFailed to steal " << failed_steal_amount << " times\n";
		out_stream << "\tFalsely waited " << false_wait_amount << " times (due to incorrectly seeing all workers being done)\n";
		out_stream << "\tSpent " << std::chrono::duration<double, std::milli>(total_duration).count() << " ms in total,\n";
		out_stream << "\tof which " << std::chrono::duration<double, std::milli>(work_duration).count() << " ms working,\n";
		out_stream << "\tof which " << std::chrono::duration<double, std::milli>(info.user_job_duration).count() << " ms on user jobs\n";
	}

	inline void WorkerStatistics::reset() {
		own_job_amount = 0;
		stolen_job_amount = 0;
		failed_steal_amount = 0;
		false_wait_amount = 0;
		total_duration = std::chrono::nanoseconds::zero();
		work_duration = std::chrono::nanoseconds::zero();
		info.user_job_amount = 0;
		info.user_job_duration = std::chrono::nanoseconds::zero();
	}

}