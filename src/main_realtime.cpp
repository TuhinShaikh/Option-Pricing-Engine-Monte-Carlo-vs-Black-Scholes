// src/main_realtime.cpp
#include "types.hpp"
#include "vol_surface.hpp"
#include "tv_engine.hpp"
#include "quote_generator.hpp"
#include "risk_manager.hpp"
#include "pnl_engine.hpp"
#include "spsc_queue.hpp"
#include "utils.hpp"
#include <iostream>
#include <string>
#include <sstream>
#include <cmath>

// simple JSON‑line parser for this format:
// {"spot":24000,"strike":23500,"type":"CE","expiry_days":30,"bid":470,"ask":480,"iv":0.15}

static double safe_stod(const std::string& s) {
    try { return std::stod(s); }
    catch (...) { return 0.0; }
}

int main() {
    VolSurface vol;
    vol.update({{{30.0/365.0, {0.0225, 0.3, -0.2, 0.0, 0.1}}}});
    TVEngine tv_engine(vol, 0.065);
    QuoteGenerator quote_gen;
    RiskManager::FillQueue fill_queue;
    RiskManager::HedgeQueue hedge_queue;
    RiskManager risk_mgr(fill_queue, hedge_queue);
    PnLEngine pnl_engine;

    std::cerr << "[MarketMaking] Waiting for NSE option data on stdin...\n";

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        // crude JSON extraction (assume single‑line JSON)
        double spot = 24000, strike = 24000, bid = 0, ask = 0, iv = 0;
        int expiry_days = 30;
        std::string type_str = "CE";

        auto extract = [&](const std::string& key) -> std::string {
            size_t pos = line.find("\"" + key + "\"");
            if (pos == std::string::npos) return "";
            pos = line.find(":", pos);
            if (pos == std::string::npos) return "";
            pos = line.find_first_not_of(": \t", pos);
            if (pos == std::string::npos) return "";
            size_t end = line.find_first_of(",}", pos);
            return line.substr(pos, end - pos);
        };

        spot        = safe_stod(extract("spot"));
        strike      = safe_stod(extract("strike"));
        bid         = safe_stod(extract("bid"));
        ask         = safe_stod(extract("ask"));
        iv          = safe_stod(extract("iv"));
        expiry_days = static_cast<int>(safe_stod(extract("expiry_days")));
        type_str    = extract("type");
        if (type_str.empty()) type_str = "CE";

        OptionType opt_type = (type_str == "PE" || type_str == "PUT") ? OptionType::PUT : OptionType::CALL;

        // Build option state
        OptionState opt;
        opt.spot               = spot;
        opt.key.strike_x100    = static_cast<uint32_t>(strike * 100);
        opt.key.expiry_epoch   = static_cast<uint32_t>(expiry_days);  // dummy
        opt.key.type           = opt_type;
        opt.key.underlying_id  = 0;
        opt.bid_market         = bid;
        opt.ask_market         = ask;

        // Use either market IV or fallback to vol surface
        double T = expiry_days / 365.0;
        double iv_used = (iv > 0.01) ? iv : vol.get_iv(std::log(spot/strike), T);
        opt.iv = iv_used;

        // Theoretical value & Greeks
        opt.tv = tv_engine.compute_tv(spot, strike, T, iv_used, opt_type);
        opt.greeks = tv_engine.compute_greeks(spot, strike, T, iv_used, opt_type);
        opt.last_update_ns = now_ns();

        // Generate quote
        Position empty_pos;
        Quote q = quote_gen.generate(opt, empty_pos);

        // Simulate a fill if our quote crosses the market
        if (q.valid && q.bid >= ask && ask > 0) {
            Fill f{opt.key, Side::SELL, 1, q.bid, now_ns(), 1001};
            risk_mgr.on_fill(f, opt);
        } else if (q.valid && q.ask <= bid && bid > 0) {
            Fill f{opt.key, Side::BUY, 1, q.ask, now_ns(), 1002};
            risk_mgr.on_fill(f, opt);
        }

        // Output JSON quote line
        std::cout << "{"
                  << "\"spot\":" << spot
                  << ",\"strike\":" << strike
                  << ",\"type\":\"" << type_str << "\""
                  << ",\"tv\":" << opt.tv
                  << ",\"bid\":" << q.bid
                  << ",\"ask\":" << q.ask
                  << ",\"spread\":" << q.spread_width
                  << ",\"delta\":" << opt.greeks.delta
                  << ",\"gamma\":" << opt.greeks.gamma
                  << ",\"vega\":" << opt.greeks.vega
                  << ",\"theta\":" << opt.greeks.theta
                  << ",\"iv\":" << opt.iv
                  << "}\n" << std::flush;
    }
    return 0;
}