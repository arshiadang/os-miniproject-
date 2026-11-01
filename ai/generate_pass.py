#!/usr/bin/env python3
"""
generate_pass.py - creates SYNTHETIC telemetry for one 10-minute ground pass.
(Child 1 of Exp 4 runs this.)

One row every 10 s: signal strength rises and falls as the satellite flies
over the ground station, battery slowly drains, and data waits in queues.

Run:
    python3 ai/generate_pass.py                 normal pass
    python3 ai/generate_pass.py --battery 15    low-battery pass
    python3 ai/generate_pass.py --fail          simulate sensor failure (no file)
    python3 ai/generate_pass.py --battery 150   impossible reading -> rejected (no file)
"""
import csv
import math
import random
import sys

battery = 85.0
if "--battery" in sys.argv:
    battery = float(sys.argv[sys.argv.index("--battery") + 1])
if not 0 <= battery <= 100:
    print(f"[generate_pass] INVALID SENSOR READING: battery {battery}% is impossible", file=sys.stderr)
    sys.exit(1)
if "--fail" in sys.argv:
    print("[generate_pass] SENSOR FAILURE - no telemetry written", file=sys.stderr)
    sys.exit(1)

random.seed(7)
q_ttc, q_sstv, q_voice = 1, 2, 3
rows = []
for t in range(0, 600, 10):
    elevation = 80 * math.sin(math.pi * t / 600)          # degrees above horizon
    snr = -5 + 30 * elevation / 90 + random.gauss(0, 1.5)  # better signal when overhead
    if t % 60 == 0:
        q_ttc += 1                                         # new housekeeping frame each minute
    if random.random() < 0.10:
        q_sstv += 1
    if random.random() < 0.15:
        q_voice += 1
    rows.append({"t_s": t, "snr_db": round(snr, 1), "battery_pct": round(battery, 1),
                 "time_left_s": 600 - t, "q_ttc": q_ttc, "q_sstv": q_sstv, "q_voice": q_voice})
    battery -= 0.3
    # send something from a queue if the link is usable (keeps queues realistic)
    if snr >= 3:
        if q_ttc:
            q_ttc -= 1
        elif q_sstv and snr >= 10:
            q_sstv -= 1
        elif q_voice:
            q_voice -= 1

with open("data/telemetry.csv", "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
    w.writeheader()
    w.writerows(rows)
print(f"[generate_pass] wrote {len(rows)} rows -> data/telemetry.csv (SYNTHETIC)")
