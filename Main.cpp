#include <iostream>
#include <vector>
#include <thread>
#include <algorithm>
#include <numeric>
#include <chrono>

#include "jobs/Scheduler.h"
#include "jobs/JobGraph.h"
#include "jobs/JobSpawner.h"
#include "jobs/Statistics.h"

uint32_t slow_hash(uint32_t x) {
    for (uint32_t i = 0; i != 32; i++) {
        x += 831461;
        x *= 125897;
        x ^= (x << 16);
    }
    return x;
}

struct ParallelGenerateParams {
    uint64_t* results;
    uint32_t first;
    uint32_t amount;
};

void parallel_generate(const void* param_buffer, const jobs::JobSpawner& job_spawner, jobs::WorkerInfo& worker_info) {
    const ParallelGenerateParams* params = static_cast<const ParallelGenerateParams*>(param_buffer);

    if (params->amount <= 1024) {
        const jobs::UserJobLogger logger(worker_info);
        const uint32_t end = params->first + params->amount;
        for (uint32_t i = params->first; i != end; i++) {
            params->results[i] = slow_hash(i);
        }
        return;
    }
    const uint32_t left_amount = params->amount / 2;
    const ParallelGenerateParams left_params{ params->results, params->first, left_amount };
    job_spawner.spawn(parallel_generate, left_params, true);
    const ParallelGenerateParams right_params{ params->results, params->first + left_amount, params->amount - left_amount };
    job_spawner.spawn(parallel_generate, right_params, true);
}

struct ParallelSumParams {
    const uint64_t* numbers;
    uint64_t* results;
    uint32_t first_batch;
    uint32_t batch_amount;
    uint32_t batch_size;
};

void parallel_sum(const void* param_buffer, const jobs::JobSpawner& job_spawner, jobs::WorkerInfo& worker_info) {
    const ParallelSumParams* params = static_cast<const ParallelSumParams*>(param_buffer);

    if (params->batch_amount == 1) {
        const jobs::UserJobLogger logger(worker_info);
        const uint64_t* begin = params->numbers + params->first_batch * params->batch_size;
        params->results[params->first_batch] = std::accumulate(begin, begin + params->batch_size, 0ull);
        return;
    }
    const uint32_t left_amount = params->batch_amount / 2;
    const ParallelSumParams left_params{ params->numbers, params->results, params->first_batch, left_amount, params->batch_size };
    job_spawner.spawn(parallel_sum, left_params, true);
    const ParallelSumParams right_params{ params->numbers, params->results, params->first_batch + left_amount, params->batch_amount - left_amount, params->batch_size };
    job_spawner.spawn(parallel_sum, right_params, true);
}

int main() {
    jobs::Scheduler scheduler(std::thread::hardware_concurrency(), 32);
    std::cout << "Running scheduler with " << scheduler.get_worker_amount() << " worker threads (including main thread).\n\n";

    const uint32_t batch_amount = 1024;
    const uint32_t batch_size = 1024;
    const uint32_t number_amount = batch_amount * batch_size;
    std::vector<uint64_t> numbers(number_amount);
    std::vector<uint64_t> batch_results(batch_amount);

    std::cout << "***Scheduler benchmark***\n";
    std::cout << "Generating " << number_amount << " pseudorandom numbers using a quite expensive hash function,\nand calculating their sum.\n\n";

    // Single-thread benchmark
    const jobs::Timer benchmark_timer;
    // Generate numbers
    for (uint32_t i = 0; i != number_amount; i++) {
        numbers[i] = slow_hash(i);
    }
    // Calculate sum of numbers
    const uint64_t benchmark_result = std::accumulate(numbers.begin(), numbers.end(), 0ull);
    const std::chrono::duration<double, std::milli> benchmark_duration = benchmark_timer.get_elapsed();
    std::cout << "Single-thread benchmark: " << benchmark_duration.count() << " ms\n";

    // Scheduler job graph setup
    uint64_t scheduler_result = 0;
    jobs::JobGraph job_graph;
    // Node to generate numbers
    const ParallelGenerateParams generate_params{ numbers.data(), 0u, number_amount };
    jobs::JobGraphNode* generate_node = job_graph.new_node(parallel_generate, generate_params);
    // Node to calculate batch sums, depends on generate_node
    const ParallelSumParams batch_sum_params{ numbers.data(), batch_results.data(), 0u, batch_amount, batch_size };
    jobs::JobGraphNode* batch_sum_node = job_graph.new_node(parallel_sum, batch_sum_params, { generate_node });
    // Node to calculate sum of batch sums, depends on batch_sum_node
    const ParallelSumParams result_sum_params{ batch_results.data(), &scheduler_result, 0u, 1u, batch_amount };
    job_graph.new_node(parallel_sum, result_sum_params, { batch_sum_node });
    // Set the graph as current
    scheduler.set_job_graph(&job_graph);

    // Run scheduler
    const jobs::Timer scheduler_timer;
    scheduler.run();
    const std::chrono::duration<double, std::milli> scheduler_duration = scheduler_timer.get_elapsed();
    std::cout << "Scheduler run: " << scheduler_duration.count() << " ms\n";

    std::cout << "Ratio (benchmark time / scheduler time): " << (benchmark_duration.count() / scheduler_duration.count()) << "\n\n";

    std::cout << "Benchmark calculation result: " << benchmark_result << "\n";
    std::cout << "Scheduler calculation result: " << scheduler_result << "\n";
    if (scheduler_result == benchmark_result) {
        std::cout << "Correct result!\n\n";
    }
    else {
        std::cout << "Incorrect result!\n\n";
    }

    std::cout << "\t***Details***\n";
    scheduler.write_statistics(std::cout);

    return 0;
}
