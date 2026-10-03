// benchmarks/bench_quote_gen.cpp
#include "../include/quote_generator.hpp"
#include "../include/tv_engine.hpp"
#include "../include/vol_surface.hpp"
#include "../include/utils.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <vector>
#include <algorithm>
#include <numeric>
#include <functional>

using namespace std::chrono;

struct BenchmarkResult {
    std::string name;
    int64_t     min_ns;
    int64_t     max_ns;
    int64_t     avg_ns;
    int64_t     p50_ns;
    int64_t     p99_ns;
    int         iterations;
};

BenchmarkResult run_benchmark(const std::string& name, std::function<void()> fn, int iterations = 100000) {
    std::vector<int64_t> times;
    times.reserve(iterations);

    for (int i = 0; i < 100; ++i) fn();

    for (int i = 0; i < iterations; ++i) {
        auto t0 = high_resolution_clock::now();
        fn();
        auto t1 = high_resolution_clock::now();
        times.push_back(duration_cast<nanoseconds>(t1 - t0).count());
    }

    std::sort(times.begin(), times.end());

    BenchmarkResult result{
        .name = name,
        .min_ns = times.front(),
        .max_ns = times.back(),
        .avg_ns = std::accumulate(times.begin(), times.end(), 0LL) / iterations,
        .p50_ns = times[iterations / 2],
        .p99_ns = times[iterations * 99 / 100],
        .iterations = iterations
    };

    return result;
}

void print_result(const BenchmarkResult& r) {
    std::cout << std::left << std::setw(45) << r.name
              << " | avg: " << format_ns(r.avg_ns)
              << " | p99: " << format_ns(r.p99_ns)
              << " | max: " << format_ns(r.max_ns) << "\n";
}

int main() {
    std::cout << "═══════════════════════════════════════════════════════════════════════════════\n";
    std::cout << " Quote Generator Benchmarks\n";
    std::cout << "═══════════════════════════════════════════════════════════════════════════════\n\n";

    // Setup
    VolSurface vol_surface;
    vol_surface.update({{{30.0/365.0, {0.0225, 0.3, -0.2, 0.0, 0.1}}}});

    TVEngine tv_engine(vol_surface);
    QuoteGenerator quote_gen;

    // Create sample option state
    OptionState opt{
        .key = {0, 2400000, 20000, OptionType::CALL},
        .spot = 24000.0,
        .tv = 470.0,
        .iv = 0.20,
        .bid_market = 470.0,
        .ask_market = 485.0,
        .greeks = {0.65, 0.0005, 50.0, -0.02, 0.1},
        .last_update_ns = now_ns()
    };

    Position empty_pos;
    Position long_inventory;
    long_inventory.quantity = 50;
    long_inventory.position_greeks = {32.5, 0.025, 2500.0, -1.0, 5.0};

    Position short_inventory;
    short_inventory.quantity = -50;
    short_inventory.position_greeks = {-32.5, -0.025, -2500.0, 1.0, -5.0};

    // Benchmark: quote generation at flat inventory
    auto bench_quote_flat = [&]() {
        volatile Quote q = quote_gen.generate(opt, empty_pos);
        (void)q;
    };

    // Benchmark: quote generation with long inventory
    auto bench_quote_long = [&]() {
        volatile Quote q = quote_gen.generate(opt, long_inventory);
        (void)q;
    };

    // Benchmark: quote generation with short inventory
    auto bench_quote_short = [&]() {
        volatile Quote q = quote_gen.generate(opt, short_inventory);
        (void)q;
    };

    // Benchmark: 1000 quotes (full market)
    std::vector<OptionState> options;
    std::vector<Position> positions;
    for (int i = 0; i < 1000; ++i) {
        OptionState o = opt;
        o.key.strike_x100 = 2300000 + i * 1000;
        options.push_back(o);

        Position p;
        p.quantity = (rand() % 200) - 100;
        positions.push_back(p);
    }

    auto bench_1k_quotes = [&]() {
        int total_quotes = 0;
        for (size_t i = 0; i < options.size(); ++i) {
            volatile Quote q = quote_gen.generate(options[i], positions[i]);
            if (q.valid) total_quotes++;
        }
        volatile int result = total_quotes;
        (void)result;
    };

    std::cout << "Quote Generation Benchmarks\n";
    std::cout << "───────────────────────────────────────────────────────────────────────────────\n";

    auto r1 = run_benchmark("Single quote (flat inventory)", bench_quote_flat, 100000);
    print_result(r1);

    auto r2 = run_benchmark("Single quote (long inventory)", bench_quote_long, 100000);
    print_result(r2);

    auto r3 = run_benchmark("Single quote (short inventory)", bench_quote_short, 100000);
    print_result(r3);

    auto r4 = run_benchmark("1000 quotes (full market)", bench_1k_quotes, 1000);
    print_result(r4);

    std::cout << "\nThroughput Analysis\n";
    std::cout << "───────────────────────────────────────────────────────────────────────────────\n";
    double quotes_per_sec = 1e9 / r1.avg_ns;
    std::cout << "Single quote generation rate: " << (quotes_per_sec / 1e6) << "M quotes/sec\n";
    std::cout << "Target: > 1M quotes/sec\n";

    if (quotes_per_sec > 1e6) {
        std::cout << "✓ PASS: Meeting throughput target\n";
    } else {
        std::cout << "✗ FAIL: Below throughput target\n";
    }

    std::cout << "\n✓ All benchmarks complete\n";
    return 0;
}
