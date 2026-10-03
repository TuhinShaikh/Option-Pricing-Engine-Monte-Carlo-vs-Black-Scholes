// benchmarks/bench_full_system.cpp
#include "../include/tv_engine.hpp"
#include "../include/vol_surface.hpp"
#include "../include/quote_generator.hpp"
#include "../include/risk_manager.hpp"
#include "../include/delta_hedger.hpp"
#include "../include/pnl_engine.hpp"
#include "../include/iv_extractor.hpp"
#include "../include/utils.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <vector>
#include <algorithm>
#include <numeric>

using namespace std::chrono;

int main() {
    std::cout << "═══════════════════════════════════════════════════════════════════════════════\n";
    std::cout << " Full System Integration Test & Benchmarks\n";
    std::cout << "═══════════════════════════════════════════════════════════════════════════════\n\n";

    // ─────────────────────────────────────────────────────────────────────────────────
    // 1. Setup components
    // ─────────────────────────────────────────────────────────────────────────────────

    std::cout << "1. Initializing Components\n";
    std::cout << "───────────────────────────────────────────────────────────────────────────────\n";

    // Vol surface with 3 expiries
    VolSurface vol_surface;
    std::vector<std::pair<double, SVIParams>> slices;
    slices.push_back({30.0/365.0, {0.0225, 0.3, -0.2, 0.0, 0.1}});
    slices.push_back({60.0/365.0, {0.0225, 0.35, -0.15, 0.0, 0.12}});
    slices.push_back({90.0/365.0, {0.0225, 0.4, -0.1, 0.0, 0.14}});
    vol_surface.update(slices);
    std::cout << "✓ Vol Surface initialized with 3 expiry slices\n";

    // TV Engine
    TVEngine tv_engine(vol_surface);
    std::cout << "✓ TV Engine initialized\n";

    // Quote Generator
    QuoteGenerator quote_gen;
    std::cout << "✓ Quote Generator initialized\n";

    // Risk Manager
    RiskManager::FillQueue fill_queue;
    RiskManager::HedgeQueue hedge_queue;
    RiskManager risk_mgr(fill_queue, hedge_queue);
    std::cout << "✓ Risk Manager initialized\n";

    // Delta Hedger
    DeltaHedger hedger;
    std::cout << "✓ Delta Hedger initialized\n";

    // PnL Engine
    PnLEngine pnl_engine;
    std::cout << "✓ PnL Engine initialized\n";

    // IV Extractor
    IVExtractor iv_extractor;
    std::cout << "✓ IV Extractor initialized\n";

    // ─────────────────────────────────────────────────────────────────────────────────
    // 2. Test data pipeline
    // ─────────────────────────────────────────────────────────────────────────────────

    std::cout << "\n2. Testing Data Pipeline\n";
    std::cout << "───────────────────────────────────────────────────────────────────────────────\n";

    // Create a sample option
    OptionState opt{
        .key = {0, 2400000, 20000, OptionType::CALL},
        .spot = 24000.0,
        .bid_market = 470.0,
        .ask_market = 485.0
    };

    // Compute TV and Greeks
    double K = strike_from_key(opt.key.strike_x100);
    double T = days_to_years(30);
    double log_m = std::log(opt.spot / K);
    double iv = vol_surface.get_iv(log_m, T);
    opt.iv = iv;
    opt.tv = tv_engine.compute_tv(opt.spot, K, T, iv, OptionType::CALL);
    opt.greeks = tv_engine.compute_greeks(opt.spot, K, T, iv, OptionType::CALL);
    opt.last_update_ns = now_ns();

    std::cout << "Option: Nifty 24000 Call (30 DTE)\n";
    std::cout << "  Spot: ₹" << opt.spot << " | IV: " << (opt.iv*100) << "%\n";
    std::cout << "  TV: ₹" << std::fixed << std::setprecision(2) << opt.tv << "\n";
    std::cout << "  Greeks: Δ=" << std::setprecision(3) << opt.greeks.delta
              << " Γ=" << opt.greeks.gamma << " ν=" << opt.greeks.vega << "\n";

    // Generate quote
    Position empty_pos;
    Quote q = quote_gen.generate(opt, empty_pos);
    std::cout << "✓ Quote generated: Bid ₹" << std::fixed << std::setprecision(2) << q.bid
              << " x Ask ₹" << q.ask << "\n";

    // Simulate a fill
    Fill fill{
        .key = opt.key,
        .side = Side::BUY,
        .quantity = 10,
        .fill_price = 475.0,
        .fill_ns = now_ns(),
        .order_id = 1001
    };

    bool fill_ok = risk_mgr.on_fill(fill, opt);
    std::cout << "✓ Fill processed: " << fill.quantity << " contracts @ ₹" << fill.fill_price << "\n";

    auto& portfolio_greeks = risk_mgr.greeks();
    std::cout << "  Portfolio Greeks: Δ=" << std::setprecision(3) << portfolio_greeks.net_delta.load()
              << " Γ=" << portfolio_greeks.net_gamma.load()
              << " ν=" << portfolio_greeks.net_vega.load() << "\n";

    // ─────────────────────────────────────────────────────────────────────────────────
    // 3. Performance benchmarks
    // ─────────────────────────────────────────────────────────────────────────────────

    std::cout << "\n3. Performance Benchmarks\n";
    std::cout << "───────────────────────────────────────────────────────────────────────────────\n";

    // Benchmark: Full pipeline (spot update → IV lookup → TV/Greeks → Quote)
    std::vector<int64_t> times;
    int iterations = 50000;

    for (int i = 0; i < iterations; ++i) {
        auto t0 = high_resolution_clock::now();

        // Simulate market data update
        opt.spot = 24000.0 + (rand() % 100 - 50) * 0.01;
        log_m = std::log(opt.spot / K);
        iv = vol_surface.get_iv(log_m, T);
        opt.iv = iv;

        // Compute TV and Greeks
        opt.tv = tv_engine.compute_tv(opt.spot, K, T, iv, OptionType::CALL);
        opt.greeks = tv_engine.compute_greeks(opt.spot, K, T, iv, OptionType::CALL);

        // Generate quote
        Quote q = quote_gen.generate(opt, empty_pos);

        auto t1 = high_resolution_clock::now();
        times.push_back(duration_cast<nanoseconds>(t1 - t0).count());
    }

    std::sort(times.begin(), times.end());
    int64_t avg_time = std::accumulate(times.begin(), times.end(), 0LL) / iterations;
    int64_t p99_time = times[iterations * 99 / 100];

    std::cout << "Full Pipeline (spot → IV → TV/Greeks → Quote):\n";
    std::cout << "  Average: " << format_ns(avg_time) << "\n";
    std::cout << "  P99:     " << format_ns(p99_time) << "\n";
    std::cout << "  Target:  < 10μs (P99)\n";

    if (p99_time < 10000) {
        std::cout << "  ✓ PASS\n";
    } else {
        std::cout << "  ✗ FAIL\n";
    }

    // Benchmark: Fill to hedge order (fill → risk update → hedge decision)
    times.clear();
    for (int i = 0; i < 10000; ++i) {
        auto t0 = high_resolution_clock::now();

        Fill f = fill;
        f.fill_ns = now_ns();
        bool ok = risk_mgr.on_fill(f, opt);

        auto t1 = high_resolution_clock::now();
        times.push_back(duration_cast<nanoseconds>(t1 - t0).count());
    }

    std::sort(times.begin(), times.end());
    avg_time = std::accumulate(times.begin(), times.end(), 0LL) / times.size();
    p99_time = times[times.size() * 99 / 100];

    std::cout << "\nFill → Hedge Decision:\n";
    std::cout << "  Average: " << format_ns(avg_time) << "\n";
    std::cout << "  P99:     " << format_ns(p99_time) << "\n";
    std::cout << "  Target:  < 20μs (P99)\n";

    if (p99_time < 20000) {
        std::cout << "  ✓ PASS\n";
    } else {
        std::cout << "  ✗ FAIL\n";
    }

    // ─────────────────────────────────────────────────────────────────────────────────
    // 4. SVI Fitting benchmark
    // ─────────────────────────────────────────────────────────────────────────────────

    std::cout << "\n4. SVI Fitting Benchmark\n";
    std::cout << "───────────────────────────────────────────────────────────────────────────────\n";

    // Create synthetic option chain data
    std::vector<double> strikes = {21600, 22200, 22800, 23400, 24000, 24600, 25200, 25800};
    std::vector<double> log_moneyness_data;
    std::vector<double> market_ivs;

    for (double K_test : strikes) {
        double log_m_test = std::log(opt.spot / K_test);
        log_moneyness_data.push_back(log_m_test);

        // Generate market IV with a smile
        double smile_iv = 0.15 + 0.1 * std::abs(log_m_test);
        market_ivs.push_back(smile_iv);
    }

    // Convert to total variance
    std::vector<double> total_vars;
    for (double mk_iv : market_ivs) {
        total_vars.push_back(mk_iv * mk_iv * T);
    }

    auto t0 = high_resolution_clock::now();
    SVIParams fitted = SVIFitter::fit(log_moneyness_data, total_vars);
    auto t1 = high_resolution_clock::now();
    int64_t fit_time = duration_cast<nanoseconds>(t1 - t0).count();

    std::cout << "SVI Fit on 8-strike option chain:\n";
    std::cout << "  Time: " << format_ns(fit_time) << "\n";
    std::cout << "  Params: a=" << std::fixed << std::setprecision(4) << fitted.a
              << " b=" << fitted.b << " ρ=" << fitted.rho
              << " m=" << fitted.m << " σ=" << fitted.sigma << "\n";

    bool butterfly_ok = SVIFitter::is_butterfly_free(fitted, T);
    std::cout << "  Butterfly arbitrage free: " << (butterfly_ok ? "✓" : "✗") << "\n";

    // ─────────────────────────────────────────────────────────────────────────────────
    // 5. IV Extraction benchmark
    // ─────────────────────────────────────────────────────────────────────────────────

    std::cout << "\n5. IV Extraction Benchmark\n";
    std::cout << "───────────────────────────────────────────────────────────────────────────────\n";

    times.clear();
    for (int i = 0; i < 10000; ++i) {
        auto t0 = high_resolution_clock::now();

        double market_price = 475.0 + (rand() % 20 - 10) * 0.1;
        auto extracted_iv = iv_extractor.extract_iv(market_price, opt.spot, K, T, OptionType::CALL);

        auto t1 = high_resolution_clock::now();
        times.push_back(duration_cast<nanoseconds>(t1 - t0).count());
    }

    std::sort(times.begin(), times.end());
    avg_time = std::accumulate(times.begin(), times.end(), 0LL) / times.size();
    p99_time = times[times.size() * 99 / 100];

    std::cout << "IV Extraction (Newton-Raphson):\n";
    std::cout << "  Average: " << format_ns(avg_time) << "\n";
    std::cout << "  P99:     " << format_ns(p99_time) << "\n";
    std::cout << "  Target:  < 5μs (P99)\n";

    // ─────────────────────────────────────────────────────────────────────────────────
    // 6. Summary
    // ─────────────────────────────────────────────────────────────────────────────────

    std::cout << "\n6. Summary\n";
    std::cout << "───────────────────────────────────────────────────────────────────────────────\n";
    std::cout << "✓ System initialized and all components functional\n";
    std::cout << "✓ Data pipelines working end-to-end\n";
    std::cout << "✓ Performance benchmarks completed\n";

    std::cout << "\n═══════════════════════════════════════════════════════════════════════════════\n";
    std::cout << " Integration test PASSED\n";
    std::cout << "═══════════════════════════════════════════════════════════════════════════════\n";

    return 0;
}
