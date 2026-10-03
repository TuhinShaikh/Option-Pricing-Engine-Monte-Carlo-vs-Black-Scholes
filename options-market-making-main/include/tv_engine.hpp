// include/tv_engine.hpp
#pragma once
#include "types.hpp"
#include "vol_surface.hpp"
#include "utils.hpp"
#include <cmath>

struct TVResult {
    double tv;
    Greeks greeks;
    double iv_used;
    int64_t compute_ns;
};

class TVEngine {
public:
    explicit TVEngine(const VolSurface& vol, double rate = 0.065)
        : vol_surface_(vol), rate_(rate) {}

    // Compute Greeks from Black-Scholes
    Greeks compute_greeks(double S, double K, double T, double sigma, OptionType type) const {
        if (T <= 0 || sigma <= 0) {
            double intrinsic = (type == OptionType::CALL) ? std::max(S - K, 0.0) : std::max(K - S, 0.0);
            double delta = (type == OptionType::CALL) ? ((S >= K) ? 1.0 : 0.0) : ((S <= K) ? -1.0 : 0.0);
            return {delta, 0.0, 0.0, 0.0, 0.0};
        }
        double sqrt_T = std::sqrt(T);
        double d1 = (std::log(S / K) + (rate_ + 0.5 * sigma * sigma) * T) / (sigma * sqrt_T);
        double d2 = d1 - sigma * sqrt_T;
        double pdf_d1 = std::exp(-0.5 * d1 * d1) / std::sqrt(2.0 * M_PI);
        double Nd1 = normal_cdf(d1);
        double Nd2 = normal_cdf(d2);

        Greeks g;
        g.delta = (type == OptionType::CALL) ? Nd1 : Nd1 - 1.0;
        g.gamma = pdf_d1 / (S * sigma * sqrt_T);
        g.vega  = S * pdf_d1 * sqrt_T * 0.01;  // Per 1% vol move
        g.rho   = (type == OptionType::CALL) ? K * T * std::exp(-rate_ * T) * Nd2 * 0.01
                                              : -K * T * std::exp(-rate_ * T) * (1.0 - Nd2) * 0.01;
        g.theta = (type == OptionType::CALL)
            ? (-(S * pdf_d1 * sigma) / (2.0 * sqrt_T) - rate_ * K * std::exp(-rate_ * T) * Nd2) / 365.0
            : (-(S * pdf_d1 * sigma) / (2.0 * sqrt_T) + rate_ * K * std::exp(-rate_ * T) * (1.0 - Nd2)) / 365.0;
        return g;
    }

    // Compute theoretical value using Black-Scholes
    double compute_tv(double S, double K, double T, double sigma, OptionType type) const {
        if (T <= 0) return (type == OptionType::CALL) ? std::max(S - K, 0.0) : std::max(K - S, 0.0);
        double sqrt_T = std::sqrt(T);
        double d1 = (std::log(S / K) + (rate_ + 0.5 * sigma * sigma) * T) / (sigma * sqrt_T);
        double d2 = d1 - sigma * sqrt_T;
        double Nd1 = normal_cdf(d1);
        double Nd2 = normal_cdf(d2);
        if (type == OptionType::CALL) return S * Nd1 - K * std::exp(-rate_ * T) * Nd2;
        return K * std::exp(-rate_ * T) * (1.0 - Nd2) - S * (1.0 - Nd1);
    }

    // Full compute: TV + Greeks + timestamp
    TVResult compute(const OptionState& opt) {
        int64_t t0 = now_ns();

        double K = strike_from_key(opt.key.strike_x100);
        double T = days_to_years(time_to_expiry_days(opt.key.expiry_epoch));

        // Get IV from surface at this option's strike/expiry
        double log_moneyness = std::log(opt.spot / K);
        double iv = vol_surface_.get_iv(log_moneyness, T);

        // Compute TV and Greeks using Black-Scholes
        double tv = compute_tv(opt.spot, K, T, iv, opt.key.type);
        Greeks greeks = compute_greeks(opt.spot, K, T, iv, opt.key.type);

        return {tv, greeks, iv, now_ns() - t0};
    }

    // Set risk-free rate
    void set_rate(double r) { rate_ = r; }
    double get_rate() const { return rate_; }

private:
    const VolSurface& vol_surface_;
    double rate_;  // Risk-free rate
};
