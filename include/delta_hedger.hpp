// include/delta_hedger.hpp
#pragma once
#include "types.hpp"
#include "utils.hpp"
#include <vector>
#include <cmath>

// Record of a hedge execution
struct HedgeRecord {
    double  delta_before;
    int32_t hedge_qty;
    int64_t timestamp_ns;
    double  underlying_price;
};

// Delta hedging strategy
class DeltaHedger {
public:
    // Called by RiskManager when delta limit approached
    // Target: sub 5μs from trigger to order submission
    bool hedge(double net_delta, double underlying_price) {
        if (std::abs(net_delta) < DELTA_THRESHOLD) return false;

        // Hedge quantity: neutralize net delta
        // If net_delta = +30.5, sell 31 shares of underlying
        int32_t hedge_qty = static_cast<int32_t>(std::round(-net_delta));
        
        if (hedge_qty == 0) return false;

        // Record the hedge
        HedgeRecord record{
            .delta_before     = net_delta,
            .hedge_qty        = hedge_qty,
            .timestamp_ns     = now_ns(),
            .underlying_price = underlying_price
        };

        hedge_log_.push_back(record);

        return true;
    }

    // Dynamic hedge threshold based on gamma
    // High gamma → tighter threshold (re-hedge more frequently)
    void update_threshold(double net_gamma) {
        DELTA_THRESHOLD = BASE_THRESHOLD / (1.0 + std::abs(net_gamma) * GAMMA_SENSITIVITY);
    }

    // Get current threshold
    double get_threshold() const { return DELTA_THRESHOLD; }

    // Get hedge log
    const std::vector<HedgeRecord>& get_hedge_log() const { return hedge_log_; }

    // Clear hedge log (call periodically to free memory)
    void clear_hedge_log() { hedge_log_.clear(); }

    // Get total hedges executed
    size_t num_hedges() const { return hedge_log_.size(); }

    // Get cumulative hedge quantity
    int32_t cumulative_hedge_qty() const {
        int32_t total = 0;
        for (const auto& record : hedge_log_) {
            total += record.hedge_qty;
        }
        return total;
    }

    // Compute realized PnL from hedges
    // Sum of (hedge_qty * price_move) for all hedges
    double compute_hedge_pnl(double current_underlying_price) const {
        double pnl = 0.0;
        for (const auto& record : hedge_log_) {
            double price_move = current_underlying_price - record.underlying_price;
            // If we hedged short (hedge_qty > 0), we profit from price drops
            // If we hedged long (hedge_qty < 0), we profit from price rises
            pnl -= static_cast<double>(record.hedge_qty) * price_move;
        }
        return pnl;
    }

    // Statistics on hedging efficiency
    struct HedgeStats {
        int32_t total_hedges;
        int32_t total_qty_hedged;
        double  avg_delta_at_hedge;
        double  max_delta_at_hedge;
        double  min_delta_at_hedge;
        int64_t time_span_ns;
    };

    HedgeStats compute_stats() const {
        if (hedge_log_.empty()) {
            return {0, 0, 0.0, 0.0, 0.0, 0};
        }

        HedgeStats stats{};
        stats.total_hedges = hedge_log_.size();
        stats.total_qty_hedged = cumulative_hedge_qty();
        stats.max_delta_at_hedge = hedge_log_[0].delta_before;
        stats.min_delta_at_hedge = hedge_log_[0].delta_before;

        double sum_delta = 0.0;
        for (const auto& record : hedge_log_) {
            sum_delta += record.delta_before;
            stats.max_delta_at_hedge = std::max(stats.max_delta_at_hedge, record.delta_before);
            stats.min_delta_at_hedge = std::min(stats.min_delta_at_hedge, record.delta_before);
        }

        stats.avg_delta_at_hedge = sum_delta / hedge_log_.size();
        stats.time_span_ns = hedge_log_.back().timestamp_ns - hedge_log_[0].timestamp_ns;

        return stats;
    }

private:
    static constexpr double BASE_THRESHOLD    = 1.0;   // Re-hedge at 1 delta
    static constexpr double GAMMA_SENSITIVITY = 0.1;

    double DELTA_THRESHOLD = BASE_THRESHOLD;
    std::vector<HedgeRecord> hedge_log_;
};
