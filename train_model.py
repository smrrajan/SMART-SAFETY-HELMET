"""
Model Training Script – Smart Safety Monitoring System
Trains a Random Forest classifier on gas / temperature / motion data.
Saves: model/safety_model.pkl  and  model/label_encoder.pkl
"""

import os, json
import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

from sklearn.ensemble import RandomForestClassifier
from sklearn.tree import DecisionTreeClassifier, export_text
from sklearn.model_selection import train_test_split, cross_val_score
from sklearn.preprocessing import LabelEncoder
from sklearn.metrics import (classification_report, confusion_matrix,
                             ConfusionMatrixDisplay)
import joblib

# ─── Config ───────────────────────────────────────────────────────────────────
DATA_PATH  = "data/sensor_data.csv"
MODEL_DIR  = "model"
FEATURES   = ["gas_ppm", "temperature", "motion"]
TARGET     = "label"

os.makedirs(MODEL_DIR, exist_ok=True)

# ─── Load data ────────────────────────────────────────────────────────────────
df = pd.read_csv(DATA_PATH)
print(f"Loaded {len(df)} samples\n{df[TARGET].value_counts()}\n")

X = df[FEATURES].values
le = LabelEncoder()
y = le.fit_transform(df[TARGET])          # DANGER=0, SAFE=1, WARNING=2

X_train, X_test, y_train, y_test = train_test_split(
    X, y, test_size=0.2, random_state=42, stratify=y
)

# ─── Train Random Forest (primary) ───────────────────────────────────────────
rf = RandomForestClassifier(
    n_estimators=100,
    max_depth=10,
    random_state=42,
    class_weight="balanced",
)
rf.fit(X_train, y_train)

cv_scores = cross_val_score(rf, X, y, cv=5, scoring="accuracy")
print(f"Cross-val accuracy: {cv_scores.mean():.4f} ± {cv_scores.std():.4f}")

y_pred = rf.predict(X_test)
print("\nClassification Report:")
print(classification_report(y_test, y_pred, target_names=le.classes_))

# ─── Confusion matrix ─────────────────────────────────────────────────────────
cm = confusion_matrix(y_test, y_pred)
disp = ConfusionMatrixDisplay(confusion_matrix=cm, display_labels=le.classes_)
fig, ax = plt.subplots(figsize=(6, 5))
disp.plot(ax=ax, colorbar=False, cmap="Blues")
ax.set_title("Random Forest – Confusion Matrix")
plt.tight_layout()
plt.savefig(f"{MODEL_DIR}/confusion_matrix.png", dpi=120)
print(f"Saved → {MODEL_DIR}/confusion_matrix.png")

# ─── Feature importance ───────────────────────────────────────────────────────
importances = rf.feature_importances_
fig2, ax2 = plt.subplots(figsize=(6, 3))
bars = ax2.barh(FEATURES, importances, color=["#e63946","#2a9d8f","#e9c46a"])
ax2.set_xlabel("Importance")
ax2.set_title("Feature Importances")
plt.tight_layout()
plt.savefig(f"{MODEL_DIR}/feature_importance.png", dpi=120)
print(f"Saved → {MODEL_DIR}/feature_importance.png")

# ─── Also train a single Decision Tree for interpretability ──────────────────
dt = DecisionTreeClassifier(max_depth=5, random_state=42, class_weight="balanced")
dt.fit(X_train, y_train)
tree_rules = export_text(dt, feature_names=FEATURES)
with open(f"{MODEL_DIR}/decision_tree_rules.txt", "w") as f:
    f.write(tree_rules)
print(f"Saved → {MODEL_DIR}/decision_tree_rules.txt")

# ─── Save models & encoder ────────────────────────────────────────────────────
joblib.dump(rf, f"{MODEL_DIR}/safety_model.pkl")
joblib.dump(le, f"{MODEL_DIR}/label_encoder.pkl")
print(f"\n✅ Model saved → {MODEL_DIR}/safety_model.pkl")
print(f"✅ Encoder saved → {MODEL_DIR}/label_encoder.pkl")

# ─── Save model metadata ──────────────────────────────────────────────────────
metadata = {
    "model_type": "RandomForestClassifier",
    "features": FEATURES,
    "classes": list(le.classes_),
    "n_estimators": rf.n_estimators,
    "cv_accuracy": round(float(cv_scores.mean()), 4),
    "test_accuracy": round(float((y_pred == y_test).mean()), 4),
}
with open(f"{MODEL_DIR}/model_metadata.json", "w") as f:
    json.dump(metadata, f, indent=2)
print(f"✅ Metadata saved → {MODEL_DIR}/model_metadata.json")
