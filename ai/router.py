#!/usr/bin/env python3
"""
router.py - the onboard AI router. (Child 2 of Exp 4 runs this.)

Reads data/telemetry.csv, asks the decision tree what to send at each time
step, and applies one SAFETY RULE: if battery < 20 % the satellite may only
send a beacon, whatever the AI says. Writes data/decisions.csv.

Exit codes (checked by exp4/mission.c):
    0 = OK
    2 = telemetry file missing  -> parent switches to SAFE MODE
    3 = model missing           -> run ai/train_model.py first

Run:  python3 ai/router.py
"""
import csv
import os
import pickle
import sys

from rules import FEATURES, rule_action

if not os.path.exists("data/telemetry.csv"):
    print("[router] ERROR: data/telemetry.csv not found", file=sys.stderr)
    sys.exit(2)
if not os.path.exists("data/model.pkl"):
    print("[router] ERROR: data/model.pkl not found - run: python3 ai/train_model.py", file=sys.stderr)
    sys.exit(3)

with open("data/model.pkl", "rb") as f:
    model = pickle.load(f)

rows, bad = [], 0
with open("data/telemetry.csv") as f:
    for line_no, raw in enumerate(csv.DictReader(f), start=2):
        try:
            r = {k: float(raw[k]) for k in FEATURES}
            r["t_s"] = int(raw["t_s"])
            rows.append(r)
        except (ValueError, KeyError, TypeError):
            bad += 1
            print(f"[router] WARNING: skipped bad row {line_no}", file=sys.stderr)
if not rows:
    print("[router] ERROR: no valid rows", file=sys.stderr)
    sys.exit(2)

predictions = model.predict([[r[k] for k in FEATURES] for r in rows])

overrides, agree, counts = 0, 0, {}
with open("data/decisions.csv", "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["t_s", "snr_db", "battery_pct", "ai_action", "final_action", "overridden"])
    for r, ai in zip(rows, predictions):
        final = ai
        if r["battery_pct"] < 20 and ai != "SAFE_BEACON":      # safety rule
            final = "SAFE_BEACON"
            overrides += 1
        if final == rule_action(r):
            agree += 1
        counts[final] = counts.get(final, 0) + 1
        w.writerow([r["t_s"], r["snr_db"], r["battery_pct"], ai, final, "yes" if final != ai else "no"])

print(f"[router] {len(rows)} decisions -> data/decisions.csv")
print(f"[router] bad rows skipped: {bad} | safety overrides: {overrides} | "
      f"match with rules: {agree}/{len(rows)}")
print("[router] actions:", ", ".join(f"{k}={v}" for k, v in sorted(counts.items())))
