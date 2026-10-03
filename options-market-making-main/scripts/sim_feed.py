#!/usr/bin/env python3
import json, sys, time, random, math

spot = 24000.0
while True:
    spot += random.gauss(0, 20)
    spot = max(spot, 23500)
    spot = min(spot, 24500)
    
    for strike in range(23400, 24600, 100):
        for otype in ["CE", "PE"]:
            moneyness = spot / strike
            iv = 0.15 + 0.02 * math.exp(-abs(moneyness - 1) * 50)
            msg = {
                "spot": round(spot, 2),
                "strike": strike,
                "type": otype,
                "expiry_days": 30,
                "bid": 0,
                "ask": 0,
                "iv": round(iv, 4)
            }
            sys.stdout.write(json.dumps(msg) + "\n")
            sys.stdout.flush()
    time.sleep(0.5)
