#include <iostream>
#include <chrono>
#include <iomanip>
#include <vector>
#include <numeric>
#include <algorithm>
#include <ultrahft/common/timestamp.hpp>

struct TimerResult {
    std::string name;
    uint64_t iterations;
    double avg_ns;
    double min_ns;
    double max_ns;
    double overhead_ns;
    double ops_per_sec;
};

void print_result(const TimerResult& result) {
    std::cout << std::left << std::setw(40) << result.name
              << std::setw(12) << std::fixed << std::setprecision(2) << result.avg_ns
              << std::setw(12) << result.min_ns
              << std::setw(12) << result.max_ns
              << std::setw(12) << result.overhead_ns
              << std::setw(15) << result.ops_per_sec << "\n";
}

// Measure overhead of timing itself
double measure_timing_overhead() {
    const int samples = 100000;
    std::vector<uint64_t> times;
    times.reserve(samples);
    
    for (int i = 0; i < samples; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        auto end = std::chrono::high_resolution_clock::now();
        times.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    }
    
    std::sort(times.begin(), times.end());
    return static_cast<double>(times[samples / 2]); // median
}

TimerResult benchmark_high_resolution_clock(size_t num_samples) {
    std::vector<uint64_t> times;
    times.reserve(num_samples);
    double overhead = measure_timing_overhead();
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 0; i < num_samples; ++i) {
        auto sample = std::chrono::high_resolution_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            sample.time_since_epoch()
        ).count();
        times.push_back(ns);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    
    std::sort(times.begin(), times.end());
    double min_val = times[0];
    double max_val = times[times.size() - 1];
    
    return {
        "high_resolution_clock sampling",
        num_samples,
        static_cast<double>(duration) / num_samples,
        min_val,
        max_val,
        overhead,
        (num_samples * 1e9) / duration
    };
}

TimerResult benchmark_steady_clock(size_t num_samples) {
    std::vector<uint64_t> times;
    times.reserve(num_samples);
    double overhead = measure_timing_overhead();
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 0; i < num_samples; ++i) {
        auto sample = std::chrono::steady_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            sample.time_since_epoch()
        ).count();
        times.push_back(ns);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    
    std::sort(times.begin(), times.end());
    double min_val = times[0];
    double max_val = times[times.size() - 1];
    
    return {
        "steady_clock sampling",
        num_samples,
        static_cast<double>(duration) / num_samples,
        min_val,
        max_val,
        overhead,
        (num_samples * 1e9) / duration
    };
}

TimerResult benchmark_duration_cast(size_t num_ops) {
    std::vector<std::chrono::nanoseconds> durations;
    durations.reserve(num_ops);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 0; i < num_ops; ++i) {
        auto now = std::chrono::high_resolution_clock::now();
        auto ns_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()
        );
        durations.push_back(ns_duration);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    
    return {
        "duration_cast<nanoseconds>",
        num_ops,
        static_cast<double>(total_duration) / num_ops,
        0.0,
        0.0,
        0.0,
        (num_ops * 1e9) / total_duration
    };
}

TimerResult benchmark_timestamp_diff(size_t num_ops) {
    std::vector<uint64_t> diffs;
    diffs.reserve(num_ops);
    
    auto prev = std::chrono::high_resolution_clock::now();
    auto start = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 0; i < num_ops; ++i) {
        auto now = std::chrono::high_resolution_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::nanoseconds>(now - prev).count();
        diffs.push_back(diff);
        prev = now;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    
    std::sort(diffs.begin(), diffs.end());
    double min_diff = diffs[0];
    double max_diff = diffs[diffs.size() - 1];
    double avg_diff = std::accumulate(diffs.begin(), diffs.end(), 0.0) / num_ops;
    
    return {
        "timestamp_diff calculation",
        num_ops,
        avg_diff,
        min_diff,
        max_diff,
        0.0,
        (num_ops * 1e9) / total_duration
    };
}

TimerResult benchmark_timer_resolution() {
    const int samples = 100000;
    std::vector<uint64_t> deltas;
    deltas.reserve(samples);
    
    auto prev = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < samples; ++i) {
        auto now = std::chrono::high_resolution_clock::now();
        auto delta = std::chrono::duration_cast<std::chrono::nanoseconds>(now - prev).count();
        if (delta > 0) {
            deltas.push_back(delta);
        }
        prev = now;
    }
    
    std::sort(deltas.begin(), deltas.end());
    
    return {
        "timer resolution (min delta)",
        samples,
        static_cast<double>(deltas[deltas.size() / 2]),
        static_cast<double>(deltas[0]),
        static_cast<double>(deltas[deltas.size() - 1]),
        0.0,
        0.0
    };
}

int main() {
    const size_t SMALL = 100000;
    const size_t MEDIUM = 1000000;
    
    std::cout << "\n=== Timer and Timestamp Benchmark ===\n\n";
    std::cout << std::left << std::setw(40) << "Operation"
              << std::setw(12) << "Avg (ns)"
              << std::setw(12) << "Min (ns)"
              << std::setw(12) << "Max (ns)"
              << std::setw(12) << "Overhead (ns)"
              << std::setw(15) << "Ops/sec\n";
    std::cout << std::string(100, '-') << "\n";
    
    print_result(benchmark_high_resolution_clock(SMALL));
    print_result(benchmark_high_resolution_clock(MEDIUM));
    
    print_result(benchmark_steady_clock(SMALL));
    print_result(benchmark_steady_clock(MEDIUM));
    
    print_result(benchmark_duration_cast(MEDIUM));
    
    print_result(benchmark_timestamp_diff(SMALL));
    
    auto resolution = benchmark_timer_resolution();
    std::cout << std::left << std::setw(40) << resolution.name
              << std::setw(12) << std::fixed << std::setprecision(3) << resolution.avg_ns
              << std::setw(12) << std::fixed << std::setprecision(3) << resolution.min_ns
              << std::setw(12) << std::fixed << std::setprecision(3) << resolution.max_ns
              << std::setw(12) << "-"
              << std::setw(15) << "-\n";
    
    std::cout << std::string(100, '-') << "\n";
    std::cout << "\nBenchmark complete!\n";
    
    return 0;
}
