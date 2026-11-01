#!/usr/bin/env python3
"""
train_model.py - creates SYNTHETIC training data and trains the AI router.

Output:
    data/train.csv   - 5000 synthetic examples (labelled by the rules + 3% noise)
    data/model.pkl   - the trained decision tree

Run:  python3 ai/train_model.py
"""
import csv
import pickle
import random

from sklearn.metrics import accuracy_score, classification_report, confusion_matrix
from sklearn.model_selection import train_test_split
from sklearn.tree import DecisionTreeClassifier

from rules import ACTIONS, FEATURES, rule_action

random.seed(42)

# 1. Generate synthetic examples
rows = []
for _ in range(5000):
    r = {
        "snr_db": round(random.uniform(-5, 25), 1),
        "battery_pct": round(random.uniform(0, 100), 1),
        "time_left_s": random.randint(0, 600),
        "q_ttc": random.choice([0, 0, 0, 1, 2]),
        "q_sstv": random.randint(0, 3),
        "q_voice": random.randint(0, 4),
    }
    label = rule_action(r)
    if random.random() < 0.03:            # 3% wrong labels, like real noisy data
        label = random.choice(ACTIONS)
    r["action"] = label
    rows.append(r)

with open("data/train.csv", "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=FEATURES + ["action"])
    w.writeheader()
    w.writerows(rows)
print("Saved 5000 synthetic rows -> data/train.csv")

# 2. Train a small decision tree
X = [[r[k] for k in FEATURES] for r in rows]
y = [r["action"] for r in rows]
X_train, X_test, y_train, y_test = train_test_split(X, y, test_size=0.2, random_state=42)

model = DecisionTreeClassifier(max_depth=8, random_state=42)
model.fit(X_train, y_train)

# 3. Evaluate
pred = model.predict(X_test)
print("\nAccuracy:", round(accuracy_score(y_test, pred), 3))
print("\nClassification report:")
print(classification_report(y_test, pred, zero_division=0))
print("Confusion matrix (rows = true, columns = predicted):")
print("labels:", ACTIONS)
print(confusion_matrix(y_test, pred, labels=ACTIONS))
print("Tree nodes:", model.tree_.node_count, "| depth:", model.get_depth())

with open("data/model.pkl", "wb") as f:
    pickle.dump(model, f)
print("\nSaved model -> data/model.pkl")
