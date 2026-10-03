// include/vol_surface.hpp
#pragma once
#include "utils.hpp"
#include <vector>
#include <shared_mutex>
#include <mutex>
#include <cmath>
#include <optional>
#include <algorithm>
#include <numeric>

struct SVIParams {
    double a;       // Overall variance level
    double b;       // Angle/slope parameter
    double rho;     // Correlation/rotation parameter ∈ (-1, 1)
    double m;       // Location of minimum variance (ATM)
    double sigma;   // Smoothness of vertex (curvature)
    
    bool is_valid() const {
        return b >= 0 && rho > -1.0 && rho < 1.0 && sigma > 0 && std::isfinite(a);
    }
};

// SVI (Stochastic Volatility Inspired) Parametrization
class SVIFitter {
public:
    // Fit SVI to observed (log_moneyness, total_variance) pairs using Levenberg-Marquardt
    static SVIParams fit(
        const std::vector<double>& log_moneyness,
        const std::vector<double>& total_variance,
        const std::vector<double>& weights = {}
    ) {
        if (log_moneyness.size() < 5) {
            return {0.0225, 0.3, -0.2, 0.0, 0.1};  // fallback
        }

        SVIParams p = initial_guess(log_moneyness, total_variance);
        
        // Simple gradient descent (production: use proper Levenberg-Marquardt)
        double lambda = 0.01;
        for (int iter = 0; iter < 100; ++iter) {
            double sse = compute_sse(log_moneyness, total_variance, p, weights);
            
            // Try improving each parameter
            SVIParams p_new = p;
            bool improved = false;
            
            // Adjust each parameter slightly
            for (int param = 0; param < 5; ++param) {
                double delta = get_delta(p, param);
                apply_delta(p_new, param, delta);
                
                if (p_new.is_valid()) {
                    double sse_new = compute_sse(log_moneyness, total_variance, p_new, weights);
                    if (sse_new < sse) {
                        p = p_new;
                        improved = true;
                        break;
                    }
                }
                p_new = p;
            }
            
            if (!improved || sse < 1e-8) break;
        }
        
        // Ensure valid parameters
        p.rho = clamp(p.rho, -0.99, 0.99);
        p.sigma = std::max(p.sigma, 0.01);
        p.b = std::max(p.b, 0.01);
        
        return p;
    }

    // Total variance at log-moneyness k using SVI formula
    static double total_variance(double k, const SVIParams& params) {
        double diff = k - params.m;
        double sqrt_term = std::sqrt(diff * diff + params.sigma * params.sigma);
        return params.a + params.b * (params.rho * diff + sqrt_term);
    }

    // Implied volatility from total variance
    static double implied_vol(double k, double T, const SVIParams& params) {
        double w = total_variance(k, params);
        if (w <= 0 || T <= 0) return 0.15;  // fallback
        return std::sqrt(w / T);
    }

    // Check butterfly arbitrage (local vol must be positive)
    static bool is_butterfly_free(const SVIParams& params, double T, int num_points = 100) {
        for (int i = 0; i <= num_points; ++i) {
            double k = -3.0 + 6.0 * i / num_points;
            
            // Numerical second derivative: d²w/dk²
            double w_center = total_variance(k, params);
            double w_left = total_variance(k - 0.01, params);
            double w_right = total_variance(k + 0.01, params);
            double d2w = (w_right - 2.0 * w_center + w_left) / 0.0001;
            
            // Dupire's condition (simplified)
            if (d2w < -1e-6) return false;
        }
        return true;
    }

    // Check calendar spread arbitrage (variance increasing with time)
    static bool is_calendar_spread_free(
        const SVIParams& near,
        const SVIParams& far,
        double T_near,
        double T_far
    ) {
        if (T_near >= T_far) return true;  // Not applicable
        
        for (int i = 0; i <= 50; ++i) {
            double k = -3.0 + 6.0 * i / 50.0;
            double w_near = total_variance(k, near);
            double w_far = total_variance(k, far);
            
            // Total variance should increase (or stay flat) with time
            if (w_near > w_far + 1e-6) return false;
        }
        return true;
    }

private:
    static SVIParams initial_guess(
        const std::vector<double>& log_moneyness,
        const std::vector<double>& total_variance
    ) {
        // Find ATM (k ≈ 0) variance
        double atm_w = 0.0;
        double min_w = total_variance[0];
        int atm_idx = 0;
        
        for (size_t i = 0; i < log_moneyness.size(); ++i) {
            if (std::abs(log_moneyness[i]) < 0.05) {
                atm_w = total_variance[i];
                atm_idx = i;
            }
            min_w = std::min(min_w, total_variance[i]);
        }
        
        return {
            min_w,           // a
            0.3,             // b
            -0.2,            // rho (usually negative for equity)
            log_moneyness[atm_idx],  // m
            0.1              // sigma
        };
    }

    static double compute_sse(
        const std::vector<double>& log_moneyness,
        const std::vector<double>& market_total_variance,
        const SVIParams& params,
        const std::vector<double>& weights
    ) {
        double sse = 0.0;
        for (size_t i = 0; i < log_moneyness.size(); ++i) {
            double predicted = total_variance(log_moneyness[i], params);
            double error = predicted - market_total_variance[i];
            double w = weights.empty() ? 1.0 : weights[i];
            sse += w * error * error;
        }
        return sse;
    }

    static double get_delta(const SVIParams& p, int param) {
        switch (param) {
            case 0: return std::abs(p.a) * 0.01;
            case 1: return std::abs(p.b) * 0.01;
            case 2: return 0.01;
            case 3: return std::abs(p.m) * 0.01;
            case 4: return std::abs(p.sigma) * 0.01;
            default: return 0.01;
        }
    }

    static void apply_delta(SVIParams& p, int param, double delta) {
        switch (param) {
            case 0: p.a += delta; break;
            case 1: p.b += delta; break;
            case 2: p.rho += delta; break;
            case 3: p.m += delta; break;
            case 4: p.sigma += delta; break;
        }
    }
};

// Volatility surface: manages multiple SVI slices for different expiries
class VolSurface {
public:
    // Get implied volatility at given log-moneyness and time to expiry (in years)
    double get_iv(double log_moneyness, double T) const {
        std::shared_lock lock(mutex_);
        if (slices_.empty()) return 0.15;  // default fallback
        
        double w = interpolate_variance(log_moneyness, T);
        if (w <= 0 || T <= 0) return 0.15;
        return std::sqrt(w / T);
    }

    // Update surface with new SVI parameters for multiple expiries
    void update(std::vector<std::pair<double, SVIParams>> new_slices) {
        std::unique_lock lock(mutex_);
        // Sort by expiry (T)
        std::sort(new_slices.begin(), new_slices.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
        slices_ = std::move(new_slices);
    }

    // Get total variance (not vol) at a point
    double get_total_variance(double log_moneyness, double T) const {
        std::shared_lock lock(mutex_);
        if (slices_.empty()) return 0.0225 * T;  // fallback
        return interpolate_variance(log_moneyness, T);
    }

    // Add/update a single expiry slice
    void update_slice(double T, const SVIParams& params) {
        std::unique_lock lock(mutex_);
        
        // Find or insert
        auto it = std::lower_bound(slices_.begin(), slices_.end(), T,
            [](const auto& pair, double t) { return pair.first < t; });
        
        if (it != slices_.end() && it->first == T) {
            it->second = params;
        } else {
            slices_.insert(it, {T, params});
        }
    }

    // Get number of slices
    size_t num_slices() const {
        std::shared_lock lock(mutex_);
        return slices_.size();
    }

    // Get min/max expiry
    std::pair<double, double> expiry_range() const {
        std::shared_lock lock(mutex_);
        if (slices_.empty()) return {0.0, 0.0};
        return {slices_.front().first, slices_.back().first};
    }

private:
    mutable std::shared_mutex mutex_;
    std::vector<std::pair<double, SVIParams>> slices_;  // (T, SVIParams) sorted by T

    // Interpolate variance between expiry slices
    // Use variance interpolation (not vol interpolation) to maintain arbitrage-freedom
    double interpolate_variance(double k, double T) const {
        if (slices_.size() == 0) return 0.0225 * T;
        if (slices_.size() == 1) {
            return SVIFitter::total_variance(k, slices_[0].second);
        }
        
        // Find bracketing expiries
        size_t i = 0;
        while (i < slices_.size() && slices_[i].first < T) ++i;
        
        if (i == 0) {
            // Before first slice: extrapolate (use first slice's variance)
            return SVIFitter::total_variance(k, slices_[0].second);
        }
        if (i == slices_.size()) {
            // After last slice: extrapolate (use last slice's variance)
            return SVIFitter::total_variance(k, slices_.back().second);
        }

        // Linear interpolation in variance space
        double T0 = slices_[i-1].first;
        double T1 = slices_[i].first;
        double w0 = SVIFitter::total_variance(k, slices_[i-1].second);
        double w1 = SVIFitter::total_variance(k, slices_[i].second);
        double weight = (T - T0) / (T1 - T0);
        return w0 * (1.0 - weight) + w1 * weight;
    }
};
