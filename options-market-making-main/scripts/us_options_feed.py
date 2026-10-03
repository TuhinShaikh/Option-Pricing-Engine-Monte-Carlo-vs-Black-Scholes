#!/usr/bin/env python3
import yfinance as yf
import json, sys, time

TICKER = "AAPL"  # Apple options — liquid, real data

while True:
    try:
        ticker = yf.Ticker(TICKER)
        spot = ticker.fast_info['lastPrice']
        expiries = ticker.options
        if not expiries:
            time.sleep(2)
            continue
        expiry = expiries[0]
        chain = ticker.option_chain(expiry)
        for opt_type, chain_df in [('CE', chain.calls), ('PE', chain.puts)]:
            for _, row in chain_df.iterrows():
                strike = float(row['strike'])
                bid = float(row.get('bid', 0) or 0)
                ask = float(row.get('ask', 0) or 0)
                iv = float(row.get('impliedVolatility', 0) or 0)
                msg = {
                    "spot": spot,
                    "strike": strike,
                    "type": opt_type,
                    "expiry_days": (expiry_date - today).days if 'expiry_date' in dir() else 30,
                    "bid": bid,
                    "ask": ask,
                    "iv": iv / 100.0 if iv > 1 else iv
                }
                sys.stdout.write(json.dumps(msg) + "\n")
                sys.stdout.flush()
        time.sleep(5)
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        time.sleep(10)
