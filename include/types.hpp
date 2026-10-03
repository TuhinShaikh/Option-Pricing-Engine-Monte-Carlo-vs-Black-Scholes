// include/types.hpp
#pragma once
#include <cstdint>
#include <cmath>
#include <chrono>
#include <atomic>
#include "utils.hpp"

enum class OptionType : uint8_t { CALL = 0, PUT = 1 };
enum class Side       : uint8_t { BUY  = 0, SELL = 1 };

struct OptionKey {
    uint32_t underlying_id;
    uint32_t strike_x100;      // 2400000 = ₹24,000.00
    uint32_t expiry_epoch;     // days since epoch
    OptionType type;

    bool operator==(const OptionKey& o) const = default;
};

struct OptionKeyHash {
    size_t operator()(const OptionKey& k) const noexcept {
        size_t h = k.underlying_id;
        h ^= k.strike_x100   + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= k.expiry_epoch  + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= static_cast<uint8_t>(k.type) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

struct Greeks {
    double delta;
    double gamma;
    double vega;
    double theta;
    double rho;
};

struct OptionState {
    OptionKey key;
    double    spot;
    double    tv;
    double    iv;
    double    bid_market;
    double    ask_market;
    Greeks    greeks;
    int64_t   last_update_ns;
};

struct Quote {
    OptionKey key;
    double    bid;
    double    ask;
    uint32_t  bid_size;
    uint32_t  ask_size;
    double    tv;
    double    spread_width;
    int64_t   generated_ns;
    bool      valid;
};

struct Fill {
    OptionKey key;
    Side      side;
    uint32_t  quantity;
    double    fill_price;
    int64_t   fill_ns;
    uint64_t  order_id;
};

struct HedgeOrder {
    Side      side;
    uint32_t  quantity;
    int64_t   timestamp_ns;
    bool      is_hedge;
};

struct Position {
    int32_t quantity = 0;
    double  avg_entry_price = 0.0;
    double  realized_pnl = 0.0;
    Greeks  position_greeks;
};

struct PortfolioGreeks {
    std::atomic<double> net_delta{0.0};
    std::atomic<double> net_gamma{0.0};
    std::atomic<double> net_vega{0.0};
    std::atomic<double> net_theta{0.0};
    double              net_rho{0.0};

    static constexpr double DELTA_LIMIT = 50.0;
    static constexpr double GAMMA_LIMIT = 10.0;
    static constexpr double VEGA_LIMIT  = 5000.0;

    bool delta_breached() const { return std::abs(net_delta.load()) > DELTA_LIMIT; }
    bool gamma_breached() const { return std::abs(net_gamma.load()) > GAMMA_LIMIT; }
    bool vega_breached()  const { return std::abs(net_vega.load())  > VEGA_LIMIT;  }
    bool any_breached()   const { return delta_breached() || gamma_breached() || vega_breached(); }
};
