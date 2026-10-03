// include/utils.hpp
#pragma once
#include <chrono>
#include <cmath>
#include <algorithm>

// High-resolution timestamp in nanoseconds
inline int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
}

// Convert strike_x100 back to actual strike
inline double strike_from_key(uint32_t strike_x100) {
    return static_cast<double>(strike_x100) / 100.0;
}

// Convert actual strike to strike_x100
inline uint32_t strike_to_key(double strike) {
    return static_cast<uint32_t>(strike * 100 + 0.5);
}

// Convert epoch days to days since today
inline double time_to_expiry_days(uint32_t expiry_epoch) {
    auto now = std::chrono::system_clock::now();
    auto now_time = std::chrono::system_clock::to_time_t(now);
    int64_t today_epoch = now_time / 86400;
    return static_cast<double>(expiry_epoch - today_epoch);
}

// Convert days to years (for T in Black-Scholes)
inline double days_to_years(double days) {
    return days / 365.0;
}

// Standard normal CDF
inline double normal_cdf(double x) {
    return 0.5 * (1.0 + std::erf(x / std::sqrt(2.0)));
}

// Standard normal PDF
inline double normal_pdf(double x) {
    return std::exp(-0.5 * x * x) / std::sqrt(2.0 * M_PI);
}

// Clamp value between min and max
template<typename T>
inline T clamp(T value, T min_val, T max_val) {
    return std::max(min_val, std::min(max_val, value));
}

// Format nanoseconds to readable time
inline std::string format_ns(int64_t ns) {
    if (ns < 1000) return std::to_string(ns) + "ns";
    if (ns < 1000000) return std::to_string(ns / 1000.0) + "μs";
    return std::to_string(ns / 1000000.0) + "ms";
}

// Absolute value
template<typename T>
inline T abs_val(T x) {
    return x < 0 ? -x : x;
}

// Sign of a number
template<typename T>
inline int sign(T x) {
    return (x > 0) - (x < 0);
}
