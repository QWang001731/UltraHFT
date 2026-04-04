#include <iostream>
#include <chrono>
#include <vector>
#include <memory>
#include <iomanip>
#include <ultrahft/market_data/order_book.hpp>

using namespace ultrahft::market_data;

struct BenchmarkResult {
    std::string name;
    uint64_t iterations;
    double total_ns;
    double avg_ns;
    double ops_per_sec;
};

void print_result(const BenchmarkResult& result) {
    std::cout << std::left << std::setw(30) << result.name
              << std::setw(15) << result.iterations
              << std::setw(15) << std::fixed << std::setprecision(2) << result.avg_ns
              << std::setw(15) << result.ops_per_sec << "\n";
}

BenchmarkResult benchmark_add_orders(size_t num_orders) {
    std::vector<std::unique_ptr<Order>> orders;
    for (size_t i = 0; i < num_orders; ++i) {
        orders.push_back(std::make_unique<Order>(
            i, 1, (i % 2 == 0) ? Side::Buy : Side::Sell,
            100000 - (i % 500), 100 + (i % 900)
        ));
    }

    OrderBook book(1);
    auto start = std::chrono::high_resolution_clock::now();

    for (auto& order : orders) {
        book.add_order(order.get());
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    return {
        "add_order",
        num_orders,
        static_cast<double>(duration),
        static_cast<double>(duration) / num_orders,
        (num_orders * 1e9) / duration
    };
}

BenchmarkResult benchmark_cancel_orders(size_t num_orders) {
    OrderBook book(1);
    std::vector<std::unique_ptr<Order>> orders;

    for (size_t i = 0; i < num_orders; ++i) {
        orders.push_back(std::make_unique<Order>(
            i, 1, (i % 2 == 0) ? Side::Buy : Side::Sell,
            100000 - (i % 500), 100 + (i % 900)
        ));
        book.add_order(orders.back().get());
    }

    auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < num_orders; ++i) {
        book.cancel_order(i);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    return {
        "cancel_order",
        num_orders,
        static_cast<double>(duration),
        static_cast<double>(duration) / num_orders,
        (num_orders * 1e9) / duration
    };
}

BenchmarkResult benchmark_execute_orders(size_t num_orders) {
    OrderBook book(1);
    std::vector<std::unique_ptr<Order>> orders;

    for (size_t i = 0; i < num_orders; ++i) {
        orders.push_back(std::make_unique<Order>(
            i, 1, (i % 2 == 0) ? Side::Buy : Side::Sell,
            100000 - (i % 500), 1000 + (i % 9000)
        ));
        book.add_order(orders.back().get());
    }

    auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < num_orders; ++i) {
        book.execute_order(i, 100);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    return {
        "execute_order",
        num_orders,
        static_cast<double>(duration),
        static_cast<double>(duration) / num_orders,
        (num_orders * 1e9) / duration
    };
}

BenchmarkResult benchmark_best_bid_ask(size_t num_orders, size_t num_queries) {
    OrderBook book(1);

    for (size_t i = 0; i < num_orders; ++i) {
        auto order = std::make_unique<Order>(
            i, 1, (i % 2 == 0) ? Side::Buy : Side::Sell,
            100000 - (i % 500), 100 + (i % 900)
        );
        book.add_order(order.get());
        order.release();
    }

    auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < num_queries; ++i) {
        volatile auto bid = book.best_bid();
        volatile auto ask = book.best_ask();
        (void)bid;
        (void)ask;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    return {
        "best_bid/ask query",
        num_queries,
        static_cast<double>(duration),
        static_cast<double>(duration) / num_queries,
        (num_queries * 1e9) / duration
    };
}

int main() {
    const size_t SMALL = 1000;
    const size_t MEDIUM = 10000;
    const size_t LARGE = 100000;
    const size_t QUERIES = 1000000;

    std::cout << "\n=== Order Book Benchmark ===\n\n";
    std::cout << std::left << std::setw(30) << "Operation"
              << std::setw(15) << "Iterations"
              << std::setw(15) << "Avg (ns)"
              << std::setw(15) << "Ops/sec\n";
    std::cout << std::string(75, '-') << "\n";

    print_result(benchmark_add_orders(SMALL));
    print_result(benchmark_add_orders(MEDIUM));
    print_result(benchmark_add_orders(LARGE));

    print_result(benchmark_cancel_orders(SMALL));
    print_result(benchmark_cancel_orders(MEDIUM));

    print_result(benchmark_execute_orders(SMALL));
    print_result(benchmark_execute_orders(MEDIUM));

    print_result(benchmark_best_bid_ask(MEDIUM, QUERIES));

    std::cout << std::string(75, '-') << "\n";
    std::cout << "\nBenchmark complete!\n";

    return 0;
}
