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
    double      checksum;  // Prevents dead‑code elimination
};

BenchmarkResult run_benchmark(const std::string& name, std::function<double()> fn, int iterations = 100000) {
    std::vector<int64_t> times;
    times.reserve(iterations);

    // Warmup
    double warm_checksum = 0.0;
    for (int i = 0; i < 1000; ++i) warm_checksum += fn();

    // Actual benchmark
    double checksum = 0.0;
    for (int i = 0; i < iterations; ++i) {
        auto t0 = high_resolution_clock::now();
        checksum += fn();   // accumulator forces materialisation
        auto t1 = high_resolution_clock::now();
        times.push_back(duration_cast<nanoseconds>(t1 - t0).count());
    }

    std::sort(times.begin(), times.end());

    BenchmarkResult result{
        .name       = name,
        .min_ns     = times.front(),
        .max_ns     = times.back(),
        .avg_ns     = std::accumulate(times.begin(), times.end(), 0LL) / iterations,
        .p50_ns     = times[iterations / 2],
        .p99_ns     = times[iterations * 99 / 100],
        .iterations = iterations,
        .checksum   = checksum + warm_checksum
    };

    return result;
}

void print_result(const BenchmarkResult& r) {
    std::cout << std::left << std::setw(45) << r.name
              << " | min: " << format_ns(r.min_ns)
              << " | avg: " << format_ns(r.avg_ns)
              << " | p50: " << format_ns(r.p50_ns)
              << " | p99: " << format_ns(r.p99_ns)
              << " | max: " << format_ns(r.max_ns)
              << " | Σ:" << std::fixed << std::setprecision(0) << r.checksum << "\n";
}

int main() {
    std::cout << "═══════════════════════════════════════════════════════════════════════════════\n";
    std::cout << " TV Engine Benchmarks  (checksum‑protected against dead‑code elimination)\n";
    std::cout << "═══════════════════════════════════════════════════════════════════════════════\n\n";

    // Create vol surface with sample SVI params
    VolSurface vol_surface;
    std::vector<std::pair<double, SVIParams>> slices;
    slices.push_back({30.0/365.0, {0.0225, 0.3, -0.2, 0.0, 0.1}});
    slices.push_back({60.0/365.0, {0.0225, 0.35, -0.15, 0.0, 0.12}});
    slices.push_back({90.0/365.0, {0.0225, 0.4, -0.1, 0.0, 0.14}});
    vol_surface.update(slices);

    TVEngine tv_engine(vol_surface);

    // ── Single option TV ──
    auto bench_tv_single = [&]() -> double {
        double S = 24000.0, K = 24000.0, T = 30.0/365.0, sigma = 0.20;
        return tv_engine.compute_tv(S, K, T, sigma, OptionType::CALL);
    };

    // ── Single option Greeks ──
    auto bench_greeks_single = [&]() -> double {
        double S = 24000.0, K = 24000.0, T = 30.0/365.0, sigma = 0.20;
        Greeks g = tv_engine.compute_greeks(S, K, T, sigma, OptionType::CALL);
        return g.delta + g.gamma + g.vega + g.theta + g.rho;
    };

    // ── TV + Greeks together ──
    auto bench_tv_greeks_single = [&]() -> double {
        double S = 24000.0, K = 24000.0, T = 30.0/365.0, sigma = 0.20;
        double tv = tv_engine.compute_tv(S, K, T, sigma, OptionType::CALL);
        Greeks g  = tv_engine.compute_greeks(S, K, T, sigma, OptionType::CALL);
        return tv + g.delta + g.gamma + g.vega + g.theta + g.rho;
    };

    // ── Portfolio of 100 options ──
    auto bench_portfolio_100 = [&]() -> double {
        double S = 24000.0;
        double total = 0.0;
        for (int i = 0; i < 100; ++i) {
            double K = S * (0.9 + 0.002 * i);
            double T = (30.0 + i) / 365.0;
            double sigma = 0.15 + 0.0001 * i;
            Greeks g = tv_engine.compute_greeks(S, K, T, sigma, OptionType::CALL);
            total += g.delta + g.gamma + g.vega + g.theta + g.rho;
        }
        return total;
    };

    std::cout << "Single Option Benchmarks (100k iterations)\n";
    std::cout << "───────────────────────────────────────────────────────────────────────────────\n";

    auto r1 = run_benchmark("TV only", bench_tv_single, 100000);
    print_result(r1);

    auto r2 = run_benchmark("Greeks only", bench_greeks_single, 100000);
    print_result(r2);

    auto r3 = run_benchmark("TV + Greeks", bench_tv_greeks_single, 100000);
    print_result(r3);

    std::cout << "\nPortfolio Benchmarks\n";
    std::cout << "───────────────────────────────────────────────────────────────────────────────\n";

    auto r4 = run_benchmark("100-option portfolio Greeks", bench_portfolio_100, 10000);
    print_result(r4);

    std::cout << "\nTarget Thresholds\n";
    std::cout << "───────────────────────────────────────────────────────────────────────────────\n";
    std::cout << "Single TV + Greeks:      < 500ns  (current: " << format_ns(r3.avg_ns) << ")\n";
    std::cout << "100-option portfolio:    < 50μs   (current: " << format_ns(r4.avg_ns) << ")\n";

    std::cout << "\n✓ All benchmarks complete\n";
    return 0;
}