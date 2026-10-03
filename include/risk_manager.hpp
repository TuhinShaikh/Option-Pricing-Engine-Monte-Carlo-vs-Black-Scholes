// include/risk_manager.hpp
#pragma once
#include "types.hpp"
#include "spsc_queue.hpp"
#include "utils.hpp"
#include <shared_mutex>
#include <unordered_map>

#include <mutex>
class RiskManager {
public:
    using FillQueue  = SPSCQueue<Fill,  65536>;
    using HedgeQueue = SPSCQueue<HedgeOrder, 8192>;

    RiskManager(FillQueue& fills, HedgeQueue& hedges)
        : fill_queue_(fills), hedge_queue_(hedges) {}

    bool on_fill(const Fill& fill, const OptionState& opt) {
        std::unique_lock lock(mutex_);
        auto& pos = positions_[fill.key];
        int32_t sign = (fill.side == Side::BUY) ? +1 : -1;
        pos.quantity += sign * static_cast<int32_t>(fill.quantity);
        pos.position_greeks.delta = opt.greeks.delta * pos.quantity;
        pos.position_greeks.gamma = opt.greeks.gamma * pos.quantity;
        pos.position_greeks.vega  = opt.greeks.vega  * pos.quantity;
        pos.position_greeks.theta = opt.greeks.theta * pos.quantity;

        portfolio_.net_delta.fetch_add(sign * fill.quantity * opt.greeks.delta);
        portfolio_.net_gamma.fetch_add(sign * fill.quantity * opt.greeks.gamma);
        portfolio_.net_vega .fetch_add(sign * fill.quantity * opt.greeks.vega);
        portfolio_.net_theta.fetch_add(sign * fill.quantity * opt.greeks.theta);

        if (portfolio_.delta_breached()) {
            HedgeOrder order{fill.side == Side::BUY ? Side::SELL : Side::BUY,
                             static_cast<uint32_t>(std::abs(static_cast<int32_t>(portfolio_.net_delta.load()))),
                             now_ns(), true};
            hedge_queue_.push(order);
        }
        return !portfolio_.gamma_breached() && !portfolio_.vega_breached();
    }

    PortfolioGreeks& greeks() { return portfolio_; }

private:
    FillQueue&   fill_queue_;
    HedgeQueue&  hedge_queue_;
    std::unordered_map<OptionKey, Position, OptionKeyHash> positions_;
    PortfolioGreeks portfolio_;
    std::shared_mutex mutex_;
};
