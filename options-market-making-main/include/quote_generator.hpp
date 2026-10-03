// include/quote_generator.hpp
#pragma once
#include "types.hpp"
#include "utils.hpp"
#include <algorithm>
#include <cmath>

struct SpreadConfig {
    double base_spread_vol_pct    = 0.005;
    double adverse_selection_mult = 2.0;
    double inventory_skew_factor  = 0.1;
    double max_spread_mult        = 5.0;
    double min_spread_abs         = 0.50;
};

class QuoteGenerator {
public:
    explicit QuoteGenerator(const SpreadConfig& cfg = SpreadConfig{}) : config_(cfg) {}

    Quote generate(const OptionState& opt, const Position& pos) {
        double base_spread = config_.base_spread_vol_pct * opt.greeks.vega * opt.iv;
        base_spread = std::max(base_spread, config_.min_spread_abs);

        double moneyness = std::abs(std::log(opt.spot / (opt.key.strike_x100 / 100.0)));
        if (moneyness < 0.02) base_spread *= config_.adverse_selection_mult;

        base_spread = std::min(base_spread, base_spread * config_.max_spread_mult);

        double skew = compute_skew(pos.quantity, opt.greeks.delta);
        double bid = opt.tv - base_spread / 2.0 - skew;
        double ask = opt.tv + base_spread / 2.0 - skew;

        return Quote{
            .key          = opt.key,
            .bid          = std::max(0.0, bid),
            .ask          = std::max(0.0, ask),
            .bid_size     = compute_size(pos, Side::BUY),
            .ask_size     = compute_size(pos, Side::SELL),
            .tv           = opt.tv,
            .spread_width = base_spread,
            .generated_ns = now_ns(),
            .valid        = bid < ask && base_spread > 0
        };
    }

private:
    double compute_skew(int32_t inventory, double delta) {
        double normalized = static_cast<double>(inventory) / MAX_INVENTORY;
        return config_.inventory_skew_factor * normalized * delta * 100.0;
    }

    uint32_t compute_size(const Position& pos, Side side) {
        if (side == Side::BUY  && pos.quantity >= MAX_INVENTORY) return 0;
        if (side == Side::SELL && pos.quantity <= -MAX_INVENTORY) return 0;
        double ratio = std::abs(pos.quantity) / static_cast<double>(MAX_INVENTORY);
        return static_cast<uint32_t>(BASE_LOT_SIZE * (1.0 - 0.8 * ratio));
    }

    static constexpr int32_t  MAX_INVENTORY  = 100;
    static constexpr uint32_t BASE_LOT_SIZE  = 50;
    SpreadConfig config_;
};
