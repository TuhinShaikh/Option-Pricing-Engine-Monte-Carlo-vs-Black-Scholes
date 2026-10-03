#!/usr/bin/env python3
import requests, json, sys, time

URL = "https://www.nseindia.com/api/option-chain-indices?symbol=NIFTY"
HEADERS = {
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
    "Accept": "application/json",
    "Accept-Language": "en-US,en;q=0.9",
}

session = requests.Session()
# Warm the session with a homepage visit
session.get("https://www.nseindia.com", headers=HEADERS)

while True:
    try:
        resp = session.get(URL, headers=HEADERS, timeout=15)
        data = resp.json()
        spot = data['records']['underlyingValue']
        for expiry, details in data['records']['expiryDates'].items():
            for strike, option in details.items():
                for otype in ('CE', 'PE'):
                    o = option.get(otype)
                    if not o: continue
                    bid = o.get('bid', 0) or 0
                    ask = o.get('ask', 0) or 0
                    iv = o.get('impliedVolatility', 0) or 0
                    # NSE gives IV as percentage
                    iv_val = iv / 100.0 if iv > 0.5 else iv
                    msg = {
                        "spot": spot,
                        "strike": float(strike),
                        "type": otype,
                        "expiry_days": 30,
                        "bid": bid,
                        "ask": ask,
                        "iv": iv_val
                    }
                    sys.stdout.write(json.dumps(msg) + "\n")
                    sys.stdout.flush()
        time.sleep(3)   # be respectful
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        time.sleep(10)
