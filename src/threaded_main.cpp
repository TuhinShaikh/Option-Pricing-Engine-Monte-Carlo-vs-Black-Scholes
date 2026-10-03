// src/threaded_main.cpp
// Full system with all 6 threads running in parallel
// Demonstrates the complete Options Market Making Engine architecture

#include "types.hpp"
#include "spsc_queue.hpp"
#include "vol_surface.hpp"
#include "tv_engine.hpp"
#include "quote_generator.hpp"
#include "risk_manager.hpp"
#include "delta_hedger.hpp"
#include "pnl_engine.hpp"
#include "iv_extractor.hpp"
#include "utils.hpp"
#include <iostream>
#include <thread>
#include <vector>
#include <iomanip>
#include <chrono>
#include <atomic>

using namespace std::chrono_literals;

// Shared state
struct SharedState {
    VolSurface          vol_surface;
    TVEngine           *tv_engine = nullptr;
    QuoteGenerator      quote_gen;
    RiskManager::FillQueue   fill_queue;
    RiskManager::HedgeQueue  hedge_queue;
    RiskManager        *risk_mgr = nullptr;
    DeltaHedger         hedger;
    PnLEngine           pnl_engine;
    
    // Market data
    std::atomic<double> spot{24000.0};
    std::atomic<int64_t> market_data_updates{0};
    std::atomic<int64_t> quotes_generated{0};
    std::atomic<int64_t> fills_processed{0};
    std::atomic<int64_t> hedges_executed{0};
    std::atomic<bool>   running{true};
};

// Thread 1: Market Data Updates (simulates incoming options feed)
void thread_market_data(SharedState& state, std::vector<OptionState>& options) {
    std::cout << "[Market Data] Started\n";
    int count = 0;
    
    while (state.running && count < 100) {
        // Simulate spot price movement
        double delta_spot = (rand() % 100 - 50) * 0.01;
        state.spot.store(state.spot.load() + delta_spot, std::memory_order_release);
        
        // Update all option states
        double spot = state.spot.load();
        for (auto& opt : options) {
            double K = strike_from_key(opt.key.strike_x100);
            double T = days_to_years(time_to_expiry_days(opt.key.expiry_epoch));
            double log_m = std::log(spot / K);
            double iv = state.vol_surface.get_iv(log_m, T);
            
            opt.spot = spot;
            opt.iv = iv;
            opt.last_update_ns = now_ns();
        }
        
        state.market_data_updates++;
        std::this_thread::sleep_for(10ms);  // 100 Hz market data
        count++;
    }
    
    std::cout << "[Market Data] Stopped after " << state.market_data_updates << " updates\n";
}

// Thread 2: Vol Surface Updates (fits SVI every 100ms)
void thread_vol_surface(SharedState& state) {
    std::cout << "[Vol Surface] Started\n";
    int updates = 0;
    
    // Pre-created SVI slices for demo
    std::vector<std::pair<double, SVIParams>> slices;
    slices.push_back({30.0/365.0, {0.0225, 0.3, -0.2, 0.0, 0.1}});
    slices.push_back({60.0/365.0, {0.0225, 0.35, -0.15, 0.0, 0.12}});
    slices.push_back({90.0/365.0, {0.0225, 0.4, -0.1, 0.0, 0.14}});
    
    while (state.running && updates < 5) {
        state.vol_surface.update(slices);
        updates++;
        std::this_thread::sleep_for(100ms);  // Update every 100ms
    }
    
    std::cout << "[Vol Surface] Stopped after " << updates << " fits\n";
}

// Thread 3: Quote Generation (prices all options)
void thread_quote_generation(SharedState& state, 
                            std::vector<OptionState>& options,
                            std::vector<Position>& positions) {
    std::cout << "[Quote Gen] Started\n";
    int64_t count = 0;
    
    while (state.running && count < 500) {
        // Generate quotes for all options
        for (size_t i = 0; i < options.size(); ++i) {
            double K = strike_from_key(options[i].key.strike_x100);
            double T = days_to_years(time_to_expiry_days(options[i].key.expiry_epoch));
            double log_m = std::log(options[i].spot / K);
            double iv = state.vol_surface.get_iv(log_m, T);
            
            options[i].tv = state.tv_engine->compute_tv(
                options[i].spot, K, T, iv, options[i].key.type);
            options[i].greeks = state.tv_engine->compute_greeks(
                options[i].spot, K, T, iv, options[i].key.type);
            options[i].last_update_ns = now_ns();
            
            // Generate quote
            Quote q = state.quote_gen.generate(options[i], positions[i]);
            if (q.valid) {
                state.quotes_generated++;
            }
        }
        
        count++;
        std::this_thread::sleep_for(1ms);  // 1ms between rounds = 1000 Hz quote updates
    }
    
    std::cout << "[Quote Gen] Stopped after " << state.quotes_generated << " quotes\n";
}

// Thread 4: Risk Manager (processes fills)
void thread_risk_manager(SharedState& state, std::vector<OptionState>& options) {
    std::cout << "[Risk Manager] Started\n";
    
    while (state.running) {
        Fill fill;
        if (state.fill_queue.pop(fill)) {
            // Find the option state
            auto it = std::find_if(options.begin(), options.end(),
                [&](const OptionState& opt) { return opt.key == fill.key; });
            
            if (it != options.end()) {
                state.risk_mgr->on_fill(fill, *it);
                state.fills_processed++;
                
                // Check if delta hedge needed
                auto& pg = state.risk_mgr->greeks();
                if (pg.delta_breached()) {
                    state.hedger.hedge(pg.net_delta.load(), state.spot.load());
                    state.hedges_executed++;
                }
            }
        }
        
        std::this_thread::sleep_for(1us);  // High frequency monitoring
    }
    
    std::cout << "[Risk Manager] Stopped after " << state.fills_processed << " fills\n";
}

// Thread 5: Delta Hedger (executes hedges)
void thread_delta_hedger(SharedState& state) {
    std::cout << "[Delta Hedger] Started\n";
    
    while (state.running) {
        HedgeOrder order;
        if (state.hedge_queue.pop(order)) {
            // In real system: submit market order to matching engine
            // For demo: just log
        }
        
        std::this_thread::sleep_for(1us);  // Sub-microsecond checks
    }
    
    std::cout << "[Delta Hedger] Stopped\n";
}

// Thread 6: PnL Tracking (stats every second)
void thread_pnl_tracking(SharedState& state) {
    std::cout << "[PnL Engine] Started\n";
    int updates = 0;
    
    while (state.running && updates < 10) {
        auto& pg = state.risk_mgr->greeks();
        auto attr = state.pnl_engine.compute_attribution(
            pg, state.hedger, 1.0, 0.0, 1.0/252.0);
        
        updates++;
        std::this_thread::sleep_for(1s);  // Update every second
    }
    
    std::cout << "[PnL Engine] Stopped after " << updates << " updates\n";
}

int main() {
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║     Options Market Making Engine - Multi-threaded Demonstration        ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";

    // Initialize shared state
    SharedState state;
    state.vol_surface.update({{{30.0/365.0, {0.0225, 0.3, -0.2, 0.0, 0.1}}}});
    
    TVEngine tv_engine(state.vol_surface, 0.065);
    state.tv_engine = &tv_engine;
    
    RiskManager::FillQueue fill_queue;
    RiskManager::HedgeQueue hedge_queue;
    RiskManager risk_mgr(fill_queue, hedge_queue);
    state.risk_mgr = &risk_mgr;

    // Create sample options
    std::vector<OptionState> options;
    std::vector<Position> positions;
    
    double strikes[] = {21600, 22800, 24000, 25200, 26400};
    int expiry_epoch = 20000;
    
    for (double K : strikes) {
        for (OptionType type : {OptionType::CALL, OptionType::PUT}) {
            OptionState opt{
                .key = {0, strike_to_key(K), expiry_epoch, type},
                .spot = 24000.0,
                .bid_market = 0.0,
                .ask_market = 0.0
            };
            options.push_back(opt);
            positions.push_back(Position{});
        }
    }

    std::cout << "Initialized " << options.size() << " options\n";
    std::cout << "Starting 6 worker threads...\n";
    std::cout << "\n";
    std::cout << "Thread architecture:\n";
    std::cout << "  [1] Market Data      → OptionState updates (100 Hz)\n";
    std::cout << "  [2] Vol Surface      → SVI fitting (100 ms)\n";
    std::cout << "  [3] Quote Gen        → TV/Greeks/Quotes (1000 Hz)\n";
    std::cout << "  [4] Risk Manager     → Fill processing & hedging\n";
    std::cout << "  [5] Delta Hedger     → Hedge execution\n";
    std::cout << "  [6] PnL Engine       → Attribution tracking (1 Hz)\n";
    std::cout << "\n";
    std::cout << "Communication: Lock-free SPSC queues\n";
    std::cout << "─────────────────────────────────────────────────────────────────────────\n";
    std::cout << "\n";

    // Launch all threads
    std::thread t1(thread_market_data, std::ref(state), std::ref(options));
    std::thread t2(thread_vol_surface, std::ref(state));
    std::thread t3(thread_quote_generation, std::ref(state), std::ref(options), std::ref(positions));
    std::thread t4(thread_risk_manager, std::ref(state), std::ref(options));
    std::thread t5(thread_delta_hedger, std::ref(state));
    std::thread t6(thread_pnl_tracking, std::ref(state));

    // Monitor for 15 seconds
    for (int i = 0; i < 15; ++i) {
        std::this_thread::sleep_for(1s);
        
        std::cout << "[" << std::setfill('0') << std::setw(2) << i << "s] "
                  << "Market updates: " << state.market_data_updates
                  << " | Quotes: " << state.quotes_generated
                  << " | Fills: " << state.fills_processed
                  << " | Hedges: " << state.hedges_executed
                  << " | Spot: ₹" << std::fixed << std::setprecision(2) << state.spot.load()
                  << "\n";
    }

    std::cout << "\n";
    std::cout << "Stopping all threads...\n";
    state.running = false;

    t1.join();
    t2.join();
    t3.join();
    t4.join();
    t5.join();
    t6.join();

    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                     Final Statistics                                  ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════════════╝\n";
    
    auto& pg = state.risk_mgr->greeks();
    std::cout << "\nPortfolio Greeks:\n";
    std::cout << "  Δ: " << std::fixed << std::setprecision(2) << pg.net_delta.load() << "\n";
    std::cout << "  Γ: " << pg.net_gamma.load() << "\n";
    std::cout << "  ν: " << pg.net_vega.load() << "\n";

    std::cout << "\nSystem Throughput:\n";
    std::cout << "  Market data updates/sec: " << state.market_data_updates / 15 << "\n";
    std::cout << "  Quotes generated/sec: " << state.quotes_generated / 15 << "\n";
    std::cout << "  Fills processed/sec: " << state.fills_processed / 15 << "\n";
    std::cout << "  Hedges executed/sec: " << state.hedges_executed / 15 << "\n";

    std::cout << "\n";
    std::cout << "✓ Multi-threaded system test complete\n";
    std::cout << "\n";

    return 0;
}
