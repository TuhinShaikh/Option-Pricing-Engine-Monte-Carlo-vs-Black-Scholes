#!/usr/bin/env python3
from nsepython import option_chain, nse_quote
import json, sys, time

SYMBOL = "NIFTY"

while True:
    try:
        # Get current spot price
        quote = nse_quote(SYMBOL)
        spot = quote['priceInfo']['lastPrice']
        
        # Fetch option chain for the nearest expiry (you can also specify expiry date)
        chain = option_chain(SYMBOL)
        for record in chain:
            strike = float(record['strikePrice'])
            for otype in ['CE', 'PE']:
                side = record.get(otype, {})
                if not side:
                    continue
                bid = side.get('bid', 0) or 0
                ask = side.get('ask', 0) or 0
                iv = side.get('impliedVolatility', 0) or 0
                msg = {
                    "spot": spot,
                    "strike": strike,
                    "type": otype,
                    "expiry_days": 30,
                    "bid": bid,
                    "ask": ask,
                    "iv": iv / 100.0 if iv > 1 else iv
                }
                sys.stdout.write(json.dumps(msg) + "\n")
                sys.stdout.flush()
        time.sleep(2)
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        time.sleep(5)
