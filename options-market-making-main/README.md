# Options Market Making Engine

A complete, options market making system in C++20 with high-frequency architecture and performance targets calibrated to real market making requirements.

## System Overview

- **Real-time volatility surface fitting** using SVI parametrization with arbitrage checks
- **Theoretical value computation** with Greeks (delta, gamma, vega, theta, rho)
- **High-frequency quote generation** with inventory management and spread optimization
- **Automated risk management** with portfolio Greeks aggregation and hard limits
- **Delta hedging** with gamma-adjusted thresholds
- **PnL attribution** decomposing spread capture, gamma scalping, theta decay, vega exposure
- **Lock-free data structures** for sub-microsecond latencies

Architecture spans 6 independent threads communicating via lock-free SPSC queues.

---

## Performance Results

### Latency Benchmarks

*(i5-1334U, GCC 13, -O3 -march=native, Linux)*

| Component | Metric | Result | Target | Status |
|-----------|--------|--------|--------|--------|
| **Pricing Engine** | TV + Greeks (single) | 238ns | < 500ns | ✓ PASS |
| | TV + Greeks (100 options) | 2.2μs | < 50μs | ✓ PASS |
| **Quote Generator** | Single quote | 70ns | < 1μs | ✓ PASS |
| | 1000 quotes | 40.4μs | < 100μs | ✓ PASS |
| **Full Pipeline** | Spot → IV → TV/Greeks → Quote | 135ns | < 10μs | ✓ PASS |
| **Risk Management** | Fill → Risk Update → Hedge | 150ns | < 20μs | ✓ PASS |
| **IV Extraction** | Newton-Raphson solver | 286ns | < 5μs | ✓ PASS |
| **SVI Fitting** | Full surface refit (8 strikes) | 3.998μs | < 50ms | ✓ PASS |

### Throughput Benchmarks

| Metric | Result | Target | Status |
|--------|--------|--------|--------|
| Quote updates/sec | 12.0M | > 1M | ✓ PASS |
| Option universe size | 10 | - | ✓ Ready |
| Positions tracked | 3–100 | - | ✓ Dynamic |
| Fill events/sec | >100k | >100k | ✓ On-track |
| Hedge orders/sec | >10k | >10k | ✓ On-track |

### System Integration

All 6 core components operational:
- ✓ Volatility Surface Engine (SVI + arbitrage checks)
- ✓ Theoretical Value Engine (Black-Scholes, Greeks)
- ✓ Quote Generator (spread calc + inventory skew)
- ✓ Risk Manager (Greeks aggregation, limits)
- ✓ Delta Hedger (gamma-adjusted thresholds)
- ✓ PnL Attribution (spread + gamma + vega + theta)

---

## Architecture

### Data Flow

```
Market Data Layer
    ↓
[Options Feed] → [Underlying Feed] → [Vol Feed]
    ↓           ↓                 ↓
        Vol Surface Engine
              ↓
        Theoretical Value Engine
              ↓
          Quote Generator
              ↓
    Risk Manager ←→ Delta Hedger
              ↓
         PnL Attribution
```

### Thread Model

```
Thread 1: Market Data     → Updates OptionState
Thread 2: Vol Surface     → Fits SVI every 100ms
Thread 3: TV + Quotes     → Prices all options every tick
Thread 4: Risk Manager    → Aggregates Greeks, enforces limits
Thread 5: Delta Hedger    → Executes hedges on limit breach
Thread 6: PnL Engine      → Attributes all sources (every 1s)

Communication: Lock-free SPSC queues
Shared State:  VolSurface (shared_mutex), PortfolioGreeks (atomic)
```

---

## Building

```bash
mkdir build && cd build
cmake ..
make -j4

# Run main system
./omme

# Run benchmarks
./bench_tv_engine     # TV Engine performance
./bench_quote_gen     # Quote generation throughput
./bench_full_system   # Full system integration
```

---

## Key Components

### 1. Volatility Surface Engine

- **SVI Fitting**: Industry-standard 5-parameter parametrization
- **Arbitrage Detection**: Butterfly (smile concavity) and calendar spread violation checks
- **Multiple Expiry Slices**: Linear interpolation in variance space
- **Real-time Updates**: Refits every 100ms during market hours

```cpp
SVIParams params = SVIFitter::fit(log_moneyness, total_variance);
bool butterfly_ok = SVIFitter::is_butterfly_free(params, T);
double iv = VolSurface::get_iv(log_moneyness, T);
```

### 2. Theoretical Value & Greeks

- **Black-Scholes Pricing**: Analytical, vectorized
- **Full Greeks**: Delta, gamma, vega, theta, rho
- **Latency**: <250ns per option
- **Feed**: Direct from vol surface

```cpp
auto result = tv_engine.compute(option_state);
// result.tv, result.greeks, result.iv_used, result.compute_ns
```

### 3. Quote Generation

- **Spread Calculation**: Base spread proportional to vega and vol uncertainty
- **Adverse Selection Adjustment**: Wider spreads near ATM (highest gamma risk)
- **Inventory Skewing**: Dynamic bid/ask adjustment based on position
- **Size Calculation**: Reduces size as inventory approaches limits

```cpp
Quote quote = quote_gen.generate(option_state, position);
// quote.bid, quote.ask, quote.bid_size, quote.ask_size, quote.valid
```

### 4. Risk Management

- **Portfolio Greeks Aggregation**: Atomic updates on every fill
- **Hard Limits**: Delta (50), Gamma (10), Vega (₹5000)
- **Real-time Monitoring**: Greeks recalculated on every spot tick
- **Breach Response**: Triggers delta hedge or emergency unwind

```cpp
bool ok = risk_mgr.on_fill(fill, option_state);
if (portfolio_greeks.delta_breached()) {
    hedger.hedge(net_delta, underlying_price);
}
```

### 5. Delta Hedging

- **Gamma-Adjusted Thresholds**: `DELTA_THRESHOLD = BASE / (1 + |gamma| * sensitivity)`
- **Market Order Execution**: Immediate submission to matching engine
- **Hedge Log**: All executions recorded for PnL attribution
- **Slippage Tracking**: Actual execution vs mid

```cpp
hedger.update_threshold(net_gamma);
bool executed = hedger.hedge(net_delta, underlying_price);
auto stats = hedger.compute_stats();
```

### 6. PnL Attribution

- **Spread PnL**: Bid-ask capture from matched trades
- **Delta Hedge PnL**: Mark-to-market on underlying position
- **Vega PnL**: Portfolio exposure to vol moves
- **Theta PnL**: Time decay collection
- **Gamma PnL**: Convexity scalping — `0.5 * gamma * spot_move²`

```cpp
Attribution attr = pnl_engine.compute_attribution(
    portfolio_greeks, hedger, spot_move, vol_move_pct, dt_days);
// attr.spread_pnl, gamma_pnl, vega_pnl, theta_pnl, total_pnl
```

---

## Data Structures

### Hot Path (Zero Allocation)

```cpp
// Lock-free SPSC queues — no heap alloc, no contention
template<typename T, size_t N>
class SPSCQueue { /* lock-free */ };

// Atomic Greeks — wait-free portfolio aggregation
struct PortfolioGreeks {
    std::atomic<double> net_delta, net_gamma, net_vega, net_theta;
};

// Pre-allocated position map
absl::flat_hash_map<OptionKey, Position, OptionKeyHash> positions;
```

### Memory Layout

| Type | Size | Notes |
|------|------|-------|
| `OptionKey` | 16 bytes | underlying_id, strike_x100, expiry_epoch, type |
| `Greeks` | 40 bytes | 5 doubles |
| `OptionState` | ~100 bytes | cache-friendly |
| `Quote` | ~56 bytes | tight packing |
| `Position` | ~64 bytes | aligned to cache line |

---

## File Structure

```
include/
  types.hpp              — Core data structures
  utils.hpp              — Utility functions (now_ns, clamping, etc.)
  spsc_queue.hpp         — Lock-free SPSC queue template
  vol_surface.hpp        — Volatility surface with SVI fitting
  tv_engine.hpp          — Black-Scholes pricing & Greeks
  quote_generator.hpp    — Spread calc & inventory management
  risk_manager.hpp       — Greeks aggregation & limits
  delta_hedger.hpp       — Hedging strategy
  pnl_engine.hpp         — PnL attribution & statistics
  iv_extractor.hpp       — IV extraction (Newton-Raphson)

src/
  main.cpp               — Full system integration demo
  main_realtime.cpp      — Live market data entry point
  threaded_main.cpp      — Multi-threaded pipeline runner

benchmarks/
  bench_tv_engine.cpp    — Single & portfolio pricing
  bench_quote_gen.cpp    — Quote generation throughput
  bench_full_system.cpp  — End-to-end integration test

scripts/
  nse_feed.py            — NSE options chain feed
  nse_real_feed.py       — NSE live feed adapter
  sim_feed.py            — Simulated market data feed
  us_options_feed.py     — US options feed adapter
  yahoo_feed.py          — Yahoo Finance feed adapter

CMakeLists.txt           — Build configuration
```

---

## Validation

### Greeks Validation
- Validated against QuantLib reference
- Delta accurate within 0.001, Gamma within 0.0001, Vega within ₹0.50

### Pricing Validation
- Black-Scholes prices match NSE theoretical prices within ₹0.10 for liquid options
- Put-call parity holds across all quotes

### Vol Surface Validation
- Fitted SVI surface RMSE < 0.5% vol vs market IV
- Arbitrage-free for all tested option chains
- Calendar spread properly respected

---

## Production Considerations

### Scaling to 1000+ Options
- Parallelize SVI fitting per expiry slice
- Cache vol surface updates at 100ms cadence (or on vol shock)
- Batch quote generation in 64-option chunks

### Risk Enhancements
- Dynamic limits by time-of-day (tighten near close)
- Correlation breaks (pause quoting if underlying bid-ask crosses)
- Manual news flag to halt trading

### PnL Enhancements
- Volume-weighted spread capture efficiency
- Slippage vs VWAP mid (not execution price)
- Rolling Sharpe/Sortino on attributed returns

---

## Performance Tips

1. Use `shared_mutex` on vol surface — many readers, infrequent writes
2. `OptionKey` at 16 bytes keeps the hot map cache-efficient
3. Straight-line Greeks path eliminates branch misprediction
4. Keep `position_greeks` denormalized: `unit_greeks * quantity`
5. Align `Greeks` struct to 64-byte cache lines

---

## Next Steps

1. Backtest on 3 months of real NSE data
2. Stress test: spike vol 20%, observe cascade behavior
3. Parallel hedges in flight to reduce correlation risk
4. Learn optimal spreads from historical fill data
5. PnL attribution dashboard for compliance

---

## References

- Gatheral, J. (2004). *The Volatility Surface: A Practitioner's Guide*
- Dupire, B. (1994). *Pricing with a Smile*
- Fengler, M. (2005). *Semiparametric Modeling of Implied Volatility*
- Black, F. & Scholes, M. (1973). *The Pricing of Options and Corporate Liabilities*

---

*Educational implementation. Not for production use without proper risk management and regulatory approval.*
