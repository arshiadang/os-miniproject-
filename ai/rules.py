"""
rules.py - shared definitions for the SomaiyaSat AI router.

The satellite must decide what to send in the next time slot:
    TX_TTC      - telemetry / housekeeping (most important)
    TX_SSTV     - an SSTV image (needs good signal, enough battery and 36 s)
    TX_VOICE    - Codec2 / M17 voice or data
    SAFE_BEACON - battery is low, send only a short beacon
    IDLE        - signal too weak, send nothing

rule_action() is the RULE-BASED BASELINE. It follows the priority order in the
KJS-SRS-01 use case: TT&C > SSTV > voice/data. The decision tree in
train_model.py learns to make these decisions from data.
"""

FEATURES = ["snr_db", "battery_pct", "time_left_s", "q_ttc", "q_sstv", "q_voice"]
ACTIONS = ["TX_TTC", "TX_SSTV", "TX_VOICE", "SAFE_BEACON", "IDLE"]


def rule_action(r):
    if r["battery_pct"] < 20:
        return "SAFE_BEACON"
    if r["snr_db"] < 3:
        return "IDLE"
    if r["q_ttc"] > 0:
        return "TX_TTC"
    if r["q_sstv"] > 0 and r["battery_pct"] >= 50 and r["snr_db"] >= 10 and r["time_left_s"] >= 36:
        return "TX_SSTV"
    if r["q_voice"] > 0 and r["snr_db"] >= 6:
        return "TX_VOICE"
    return "IDLE"
