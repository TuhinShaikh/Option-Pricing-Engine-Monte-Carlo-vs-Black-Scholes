// include/pnl_engine.hpp
#pragma once
#include "types.hpp"
#include "delta_hedger.hpp"
#include "utils.hpp"
#include <vector>
#include <deque>
#include <numeric>
#include <cmath>
#include <algorithm>

// PnL attribution breakdown
struct Attribution {
    double spread_pnl;         // Captured bid-ask spread
    double delta_hedge_pnl;    // Slippage and cost of hedging
    double vega_pnl;           // Directional vol exposure gain/loss
    double theta_pnl;          // Time decay collected
    double gamma_pnl;          // Gamma scalping (convexity)
    double total_pnl;          // Sum of all components
    double unrealized_pnl;     // Mark-to-market on open positions
    int64_t timestamp_ns;
};

// Performance statistics
struct PnLStatistics {
    double sharpe_ratio;              // Annualized
    double sortino_ratio;             // Downside deviation only
    double max_drawdown;              // Peak-to-trough
    double avg_daily_pnl;
    double pnl_std_dev;
    double win_rate;                  // % of positive days
    double fill_rate;                 // Fills / quotes sent
    double avg_spread_capture;        // Actual capture vs theoretical spread
    int64_t observation_period_ns;
};

// PnL engine: tracks and attributes all sources of profit/loss
class PnLEngine {
public:
    explicit PnLEngine(size_t max_history = 100000)
        : max_history_(max_history) {}

    // Record a fill event (buy or sell)
    void record_fill(const Fill& fill, double fill_price) {
        fills_.push_back({
            fill.fill_ns,
            fill.side,
            fill.quantity,
            fill_price,
            fill.order_id
        });

        if (fills_.size() > max_history_) {
            fills_.erase(fills_.begin());
        }
    }

    // Record a quote event (for fill rate calculation)
    void record_quote(const Quote& quote) {
        if (quote.valid) {
            total_quotes_sent_++;
        }
    }

    // Compute PnL attribution at a point in time
    // This is called periodically (e.g., every second)
    Attribution compute_attribution(
        const PortfolioGreeks&       portfolio_greeks,
        const DeltaHedger&           hedger,
        double                       spot_move,        // Price move since last call
        double                       vol_move_pct,     // Vol move in percentage points
        double                       dt_days           // Time elapsed in days
    ) {
        Attribution attr{};
        attr.timestamp_ns = now_ns();

        // Spread PnL: mark realized trades
        attr.spread_pnl = compute_spread_pnl();

        // Delta hedge PnL: mark-to-market on underlying position
        attr.delta_hedge_pnl = hedger.compute_hedge_pnl(0.0);  // simplified

        // Vega PnL: portfolio vega exposure times vol move
        double net_vega = portfolio_greeks.net_vega.load();
        attr.vega_pnl = net_vega * vol_move_pct;

        // Theta PnL: portfolio theta times time elapsed
        double net_theta = portfolio_greeks.net_theta.load();
        attr.theta_pnl = net_theta * dt_days;

        // Gamma PnL: 0.5 * net_gamma * spot_move²
        double net_gamma = portfolio_greeks.net_gamma.load();
        attr.gamma_pnl = 0.5 * net_gamma * spot_move * spot_move;

        // Total PnL is sum of all components
        attr.total_pnl = attr.spread_pnl + attr.delta_hedge_pnl
                       + attr.vega_pnl + attr.theta_pnl + attr.gamma_pnl;

        // Track daily PnL for statistics
        daily_pnl_.push_back(attr.total_pnl);
        if (daily_pnl_.size() > max_history_) {
            daily_pnl_.pop_front();
        }

        // Update peak for drawdown calculation
        cumulative_pnl_ += attr.total_pnl;
        peak_pnl_ = std::max(peak_pnl_, cumulative_pnl_);
        max_drawdown_ = std::min(max_drawdown_, (cumulative_pnl_ - peak_pnl_) / (peak_pnl_ + 1e-6));

        return attr;
    }

    // Compute rolling statistics
    PnLStatistics compute_statistics(int lookback_days = 30) const {
        PnLStatistics stats{};

        if (daily_pnl_.empty()) {
            return stats;
        }

        // Average daily PnL
        double sum_pnl = 0.0;
        for (double pnl : daily_pnl_) {
            sum_pnl += pnl;
        }
        stats.avg_daily_pnl = sum_pnl / daily_pnl_.size();

        // PnL standard deviation
        double variance = 0.0;
        for (double pnl : daily_pnl_) {
            double diff = pnl - stats.avg_daily_pnl;
            variance += diff * diff;
        }
        stats.pnl_std_dev = std::sqrt(variance / daily_pnl_.size());

        // Annualized Sharpe (assuming 252 trading days per year)
        stats.sharpe_ratio = (stats.avg_daily_pnl / (stats.pnl_std_dev + 1e-6)) * std::sqrt(252.0);

        // Sortino ratio (only penalize downside)
        double downside_variance = 0.0;
        int losing_days = 0;
        for (double pnl : daily_pnl_) {
            if (pnl < 0) {
                downside_variance += pnl * pnl;
                losing_days++;
            }
            if (pnl > 0) {
                stats.win_rate += 1.0;
            }
        }
        stats.win_rate /= daily_pnl_.size();

        double downside_std = std::sqrt(downside_variance / (losing_days + 1));
        stats.sortino_ratio = (stats.avg_daily_pnl / (downside_std + 1e-6)) * std::sqrt(252.0);

        // Max drawdown
        stats.max_drawdown = max_drawdown_;

        // Fill rate
        stats.fill_rate = (total_fills_ > 0) ? static_cast<double>(total_fills_) / total_quotes_sent_ : 0.0;

        // Spread capture (simplified - needs more market data)
        stats.avg_spread_capture = 0.80;  // placeholder

        stats.observation_period_ns = daily_pnl_.size() * 86400'000'000'000LL;  // rough estimate

        return stats;
    }

    // Get number of fills recorded
    size_t num_fills() const { return fills_.size(); }

    // Get cumulative PnL
    double get_cumulative_pnl() const { return cumulative_pnl_; }

    // Get current peak
    double get_peak_pnl() const { return peak_pnl_; }

    // Get current drawdown
    double get_max_drawdown() const { return max_drawdown_; }

    // Clear history
    void clear() {
        fills_.clear();
        daily_pnl_.clear();
        cumulative_pnl_ = 0.0;
        peak_pnl_ = 0.0;
        max_drawdown_ = 0.0;
        total_fills_ = 0;
        total_quotes_sent_ = 0;
    }

private:
    struct FillRecord {
        int64_t  timestamp_ns;
        Side     side;
        uint32_t quantity;
        double   price;
        uint64_t order_id;
    };

    std::vector<FillRecord> fills_;
    std::deque<double>      daily_pnl_;
    double                  cumulative_pnl_    = 0.0;
    double                  peak_pnl_          = 0.0;
    double                  max_drawdown_      = 0.0;
    size_t                  total_fills_       = 0;
    size_t                  total_quotes_sent_ = 0;
    size_t                  max_history_;

    // Compute spread PnL from matched buy/sell pairs
    double compute_spread_pnl() {
        // Simplified: sum of (sell_price - buy_price) for matched pairs
        // In production, use a proper matching algorithm
        double pnl = 0.0;
        // Placeholder implementation
        return pnl;
    }
};
