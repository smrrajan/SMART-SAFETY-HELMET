"""
Dataset Generator for Smart Safety Monitoring System
Generates synthetic sensor data for: Gas (MQ3), Temperature (DHT22), Motion (PIR)
Labels: SAFE=0, WARNING=1, DANGER=2
"""

import pandas as pd
import numpy as np
import os

np.random.seed(42)

def generate_dataset(n_samples=3000):
    records = []

    # ── SAFE class (50%) ──────────────────────────────────────────────────────
    n_safe = n_samples // 2
    safe = {
        "gas_ppm":     np.random.uniform(50,  300,  n_safe),   # low gas
        "temperature": np.random.uniform(18,   30,  n_safe),   # comfortable temp
        "motion":      np.random.choice([0, 1], n_safe, p=[0.7, 0.3]),  # occasional motion
        "label":       ["SAFE"] * n_safe,
    }
    records.append(pd.DataFrame(safe))

    # ── WARNING class (30%) ───────────────────────────────────────────────────
    n_warn = int(n_samples * 0.30)
    warning = {
        "gas_ppm":     np.random.uniform(300,  600, n_warn),   # elevated gas
        "temperature": np.random.uniform(30,   45,  n_warn),   # warm
        "motion":      np.random.choice([0, 1], n_warn, p=[0.4, 0.6]),
        "label":       ["WARNING"] * n_warn,
    }
    records.append(pd.DataFrame(warning))

    # ── DANGER class (20%) ────────────────────────────────────────────────────
    n_danger = n_samples - n_safe - n_warn
    danger = {
        "gas_ppm":     np.random.uniform(600, 1000, n_danger), # high gas / fire
        "temperature": np.random.uniform(45,   80,  n_danger), # very hot
        "motion":      np.random.choice([0, 1], n_danger, p=[0.3, 0.7]),
        "label":       ["DANGER"] * n_danger,
    }
    records.append(pd.DataFrame(danger))

    df = pd.concat(records, ignore_index=True).sample(frac=1, random_state=42)
    df = df.round({"gas_ppm": 2, "temperature": 2})

    os.makedirs("data", exist_ok=True)
    df.to_csv("data/sensor_data.csv", index=False)
    print(f"Dataset saved → data/sensor_data.csv  ({len(df)} rows)")
    print(df["label"].value_counts())
    return df

if __name__ == "__main__":
    generate_dataset()
