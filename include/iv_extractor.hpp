// include/iv_extractor.hpp
#pragma once
#include "tv_engine.hpp"
#include "utils.hpp"
#include <optional>
#include <cmath>

// IV extraction using Newton-Raphson solver
class IVExtractor {
public:
    explicit IVExtractor(double rate = 0.065)
        : rate_(rate), tolerance_(1e-7), max_iterations_(100) {}

    // Extract implied volatility from market price using Newton-Raphson
    // market_price: observed market price of the option
    // S: spot price of underlying
    // K: strike price
    // T: time to expiry in years
    // type: CALL or PUT
    // Returns: optional IV (nullopt if failed to converge)
    std::optional<double> extract_iv(
        double market_price,
        double S,
        double K,
        double T,
        OptionType type
    ) {
        // Initial guess: use Brenner-Subrahmanyam approximation
        double iv_guess = initial_guess(market_price, S, K, T, type);

        if (iv_guess <= 0.001 || iv_guess >= 5.0) {
            return std::nullopt;  // Bad initial guess
        }

        // Newton-Raphson iteration
        for (int iter = 0; iter < max_iterations_; ++iter) {
            // Compute BS price and vega at current IV guess
            double tv = compute_tv(S, K, T, iv_guess, type);
            double vega = compute_vega(S, K, T, iv_guess, type);

            // Check for vega collapse (Newton-Raphson becomes unstable)
            if (std::abs(vega) < 1e-10) {
                // Fall back to bisection
                return bisection_solve(market_price, S, K, T, type, iv_guess * 0.5, iv_guess * 2.0);
            }

            // Newton-Raphson step
            double diff = tv - market_price;
            double iv_new = iv_guess - diff / vega;

            // Clamp to reasonable range
            iv_new = clamp(iv_new, 0.001, 5.0);

            // Check convergence
            if (std::abs(iv_new - iv_guess) < tolerance_) {
                return iv_new;
            }

            iv_guess = iv_new;
        }

        // Failed to converge - return last estimate or nullopt
        return std::nullopt;
    }

    // Set solver parameters
    void set_tolerance(double tol) { tolerance_ = tol; }
    void set_max_iterations(int max_iter) { max_iterations_ = max_iter; }

private:
    double rate_;
    double tolerance_;
    int max_iterations_;

    // Brenner-Subrahmanyam approximation for initial IV guess
    double initial_guess(double C, double S, double K, double T, OptionType type) {
        double moneyness = S / K;
        
        if (type == OptionType::CALL) {
            // For ATM calls: sigma ≈ sqrt(2π/T) * (C/S)
            if (std::abs(moneyness - 1.0) < 0.01) {
                return std::sqrt(2.0 * M_PI / T) * (C / S);
            }
            // For OTM calls, adjust
            double intrinsic = std::max(S - K, 0.0);
            double time_value = C - intrinsic;
            if (time_value <= 0) return 0.15;
            return std::sqrt(2.0 * M_PI / T) * (time_value / S);
        } else {  // PUT
            double intrinsic = std::max(K - S, 0.0);
            double time_value = C - intrinsic;
            if (time_value <= 0) return 0.15;
            return std::sqrt(2.0 * M_PI / T) * (time_value / K);
        }
    }

    // Black-Scholes price
    double compute_tv(double S, double K, double T, double sigma, OptionType type) {
        if (T <= 0 || sigma <= 0) {
            return (type == OptionType::CALL) ? std::max(S - K, 0.0) : std::max(K - S, 0.0);
        }

        double sqrt_T = std::sqrt(T);
        double d1 = (std::log(S / K) + (rate_ + 0.5 * sigma * sigma) * T) / (sigma * sqrt_T);
        double d2 = d1 - sigma * sqrt_T;

        double Nd1 = normal_cdf(d1);
        double Nd2 = normal_cdf(d2);

        if (type == OptionType::CALL) {
            return S * Nd1 - K * std::exp(-rate_ * T) * Nd2;
        } else {
            return K * std::exp(-rate_ * T) * (1.0 - Nd2) - S * (1.0 - Nd1);
        }
    }

    // Vega (derivative of price w.r.t. sigma)
    double compute_vega(double S, double K, double T, double sigma, OptionType type) {
        if (T <= 0 || sigma <= 0) return 0.0;

        double sqrt_T = std::sqrt(T);
        double d1 = (std::log(S / K) + (rate_ + 0.5 * sigma * sigma) * T) / (sigma * sqrt_T);
        double pdf_d1 = normal_pdf(d1);

        // vega = S * pdf(d1) * sqrt(T) * 0.01 (per 1% change in vol)
        return S * pdf_d1 * sqrt_T * 0.01;
    }

    // Bisection solver (fallback for degenerate cases)
    std::optional<double> bisection_solve(
        double market_price,
        double S,
        double K,
        double T,
        OptionType type,
        double iv_low,
        double iv_high
    ) {
        for (int iter = 0; iter < 100; ++iter) {
            double iv_mid = (iv_low + iv_high) / 2.0;
            double tv_mid = compute_tv(S, K, T, iv_mid, type);
            double diff = tv_mid - market_price;

            if (std::abs(diff) < tolerance_) {
                return iv_mid;
            }

            if (diff > 0) {
                iv_high = iv_mid;
            } else {
                iv_low = iv_mid;
            }

            if (iv_high - iv_low < tolerance_) {
                return iv_mid;
            }
        }

        return std::nullopt;
    }
};
