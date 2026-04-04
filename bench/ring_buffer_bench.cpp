#include <iostream>
#include <chrono>
#include <iomanip>
#include <vector>
#include <ultrahft/core/ring_buffer.hpp>

using namespace ultrahft::core;

struct BenchmarkResult {
    std::string name;
    uint64_t iterations;
    double total_ns;
    double avg_ns;
    double ops_per_sec;
};

void print_result(const BenchmarkResult& result) {
    std::cout << std::left << std::setw(35) << result.name
              << std::setw(15) << result.iterations
              << std::setw(15) << std::fixed << std::setprecision(2) << result.avg_ns
              << std::setw(15) << result.ops_per_sec << "\n";
}

BenchmarkResult benchmark_spsc_push(size_t num_ops) {
    SpscQueue<uint64_t, 16384> queue;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 0; i < num_ops; ++i) {
        queue.push(i);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    
    return {
        "SPSC push (16K capacity)",
        num_ops,
        static_cast<double>(duration),
        static_cast<double>(duration) / num_ops,
        (num_ops * 1e9) / duration
    };
}

BenchmarkResult benchmark_spsc_pop(size_t num_ops) {
    SpscQueue<uint64_t, 16384> queue;
    
    // Fill the queue
    for (size_t i = 0; i < num_ops; ++i) {
        queue.push(i);
    }
    
    uint64_t val = 0;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 0; i < num_ops; ++i) {
        queue.pop(val);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    
    return {
        "SPSC pop (16K capacity)",
        num_ops,
        static_cast<double>(duration),
        static_cast<double>(duration) / num_ops,
        (num_ops * 1e9) / duration
    };
}

BenchmarkResult benchmark_spsc_push_pop_pair(size_t num_pairs) {
    SpscQueue<uint64_t, 8192> queue;
    uint64_t val = 0;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 0; i < num_pairs; ++i) {
        queue.push(i);
        queue.pop(val);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    
    return {
        "SPSC push+pop pair (8K capacity)",
        num_pairs,
        static_cast<double>(duration),
        static_cast<double>(duration) / num_pairs,
        (num_pairs * 1e9) / duration
    };
}

BenchmarkResult benchmark_spsc_throughput(size_t batch_size, size_t num_batches) {
    SpscQueue<uint64_t, 16384> queue;
    uint64_t val = 0;
    size_t total_ops = batch_size * num_batches;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (size_t batch = 0; batch < num_batches; ++batch) {
        // Push a batch
        for (size_t i = 0; i < batch_size; ++i) {
            queue.push(batch * batch_size + i);
        }
        // Pop the batch
        for (size_t i = 0; i < batch_size; ++i) {
            queue.pop(val);
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    
    return {
        "SPSC throughput (batch of " + std::to_string(batch_size) + ")",
        total_ops,
        static_cast<double>(duration),
        static_cast<double>(duration) / total_ops,
        (total_ops * 1e9) / duration
    };
}

int main() {
    const size_t SMALL = 10000;
    const size_t MEDIUM = 100000;
    const size_t BATCH_SIZE = 256;
    
    std::cout << "\n=== SPSC Ring Buffer (Queue) Benchmark ===\n\n";
    std::cout << std::left << std::setw(35) << "Operation"
              << std::setw(15) << "Operations"
              << std::setw(15) << "Avg (ns)"
              << std::setw(15) << "Ops/sec\n";
    std::cout << std::string(80, '-') << "\n";
    
    print_result(benchmark_spsc_push(SMALL));
    print_result(benchmark_spsc_push(MEDIUM));
    
    print_result(benchmark_spsc_pop(SMALL));
    print_result(benchmark_spsc_pop(MEDIUM));
    
    print_result(benchmark_spsc_push_pop_pair(SMALL));
    print_result(benchmark_spsc_push_pop_pair(MEDIUM));
    
    print_result(benchmark_spsc_throughput(BATCH_SIZE, 10000));
    print_result(benchmark_spsc_throughput(1024, 5000));
    
    std::cout << std::string(80, '-') << "\n";
    std::cout << "\nBenchmark complete!\n";
    
    return 0;
}
