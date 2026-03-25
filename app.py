"""
Flask API Server – Smart Safety Monitoring System
Receives sensor data from ESP32 and returns ML predictions.

Endpoints:
  POST /predict          → {"gas_ppm": x, "temperature": y, "motion": z}
  GET  /status           → server health + model info
  GET  /history          → last 100 readings
  GET  /dashboard        → serves the web dashboard HTML
"""

from flask import Flask, request, jsonify
from flask_cors import CORS
import joblib
import numpy as np
import json, os
import sqlite3
import sys
from datetime import datetime, timezone
from collections import deque

app = Flask(__name__)
CORS(app)

# ─── Load model ───────────────────────────────────────────────────────────────
app_root = os.path.abspath(os.path.dirname(__file__))
MODEL_PATH   = os.path.join(app_root, "ml/model/safety_model.pkl")
ENCODER_PATH = os.path.join(app_root, "ml/model/label_encoder.pkl")
META_PATH    = os.path.join(app_root, "ml/model/model_metadata.json")
DATA_DIR     = os.path.join(app_root, "data")
DB_PATH      = os.path.join(DATA_DIR, "sensor_history.db")
ESP32_STALE_AFTER_SECONDS = 15

# Fallback for this repo layout: model/ instead of ml/model/
if not os.path.exists(MODEL_PATH):
    MODEL_PATH = os.path.join(app_root, "model/safety_model.pkl")
if not os.path.exists(ENCODER_PATH):
    ENCODER_PATH = os.path.join(app_root, "model/label_encoder.pkl")
if not os.path.exists(META_PATH):
    META_PATH = os.path.join(app_root, "model/model_metadata.json")

model   = joblib.load(MODEL_PATH)
encoder = joblib.load(ENCODER_PATH)
with open(META_PATH) as f:
    metadata = json.load(f)

# In-memory history (last 100 readings)
history = deque(maxlen=100)


def get_db_connection():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    return conn


def init_db():
    os.makedirs(DATA_DIR, exist_ok=True)
    with get_db_connection() as conn:
        conn.execute("""
            CREATE TABLE IF NOT EXISTS sensor_readings (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp TEXT NOT NULL,
                gas_ppm REAL NOT NULL,
                temperature REAL NOT NULL,
                motion INTEGER NOT NULL,
                prediction TEXT NOT NULL,
                confidence REAL NOT NULL,
                probabilities TEXT NOT NULL,
                source TEXT NOT NULL DEFAULT 'manual',
                device_id TEXT
            )
        """)
        existing_columns = {
            row["name"] for row in conn.execute("PRAGMA table_info(sensor_readings)").fetchall()
        }
        if "source" not in existing_columns:
            conn.execute(
                "ALTER TABLE sensor_readings ADD COLUMN source TEXT NOT NULL DEFAULT 'manual'"
            )
        if "device_id" not in existing_columns:
            conn.execute("ALTER TABLE sensor_readings ADD COLUMN device_id TEXT")
        conn.commit()


def save_reading(reading):
    with get_db_connection() as conn:
        conn.execute(
            """
            INSERT INTO sensor_readings
            (timestamp, gas_ppm, temperature, motion, prediction, confidence, probabilities, source, device_id)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                reading["timestamp"],
                reading["gas_ppm"],
                reading["temperature"],
                reading["motion"],
                reading["prediction"],
                reading["confidence"],
                json.dumps(reading["probabilities"]),
                reading["source"],
                reading["device_id"],
            ),
        )
        conn.commit()


def get_total_readings():
    with get_db_connection() as conn:
        row = conn.execute("SELECT COUNT(*) AS total FROM sensor_readings").fetchone()
        return row["total"]


def fetch_history(limit):
    with get_db_connection() as conn:
        rows = conn.execute(
            """
            SELECT timestamp, gas_ppm, temperature, motion, prediction, confidence, probabilities, source, device_id
            FROM sensor_readings
            ORDER BY id DESC
            LIMIT ?
            """,
            (limit,),
        ).fetchall()

    readings = []
    for row in reversed(rows):
        readings.append({
            "timestamp": row["timestamp"],
            "gas_ppm": row["gas_ppm"],
            "temperature": row["temperature"],
            "motion": row["motion"],
            "prediction": row["prediction"],
            "confidence": row["confidence"],
            "probabilities": json.loads(row["probabilities"]),
            "source": row["source"],
            "device_id": row["device_id"],
        })
    return readings


def get_esp32_status():
    with get_db_connection() as conn:
        row = conn.execute(
            """
            SELECT timestamp, device_id
            FROM sensor_readings
            WHERE source = 'esp32'
            ORDER BY id DESC
            LIMIT 1
            """
        ).fetchone()

    if not row:
        return {
            "connected": False,
            "last_seen": None,
            "device_id": None,
            "stale_after_seconds": ESP32_STALE_AFTER_SECONDS,
        }

    last_seen = datetime.fromisoformat(row["timestamp"].replace("Z", "+00:00"))
    now = datetime.now(timezone.utc)
    age_seconds = (now - last_seen).total_seconds()

    return {
        "connected": age_seconds <= ESP32_STALE_AFTER_SECONDS,
        "last_seen": row["timestamp"],
        "device_id": row["device_id"],
        "stale_after_seconds": ESP32_STALE_AFTER_SECONDS,
    }


init_db()

# ─── Helpers ──────────────────────────────────────────────────────────────────
STATUS_EMOJI = {"SAFE": "✅", "WARNING": "⚠️", "DANGER": "🚨"}
STATUS_COLOR = {"SAFE": "#2ecc71", "WARNING": "#f39c12", "DANGER": "#e74c3c"}

def validate_input(data):
    """Returns (gas_ppm, temperature, motion) or raises ValueError."""
    required = ["gas_ppm", "temperature", "motion"]
    for field in required:
        if field not in data:
            raise ValueError(f"Missing field: {field}")
    gas  = float(data["gas_ppm"])
    temp = float(data["temperature"])
    mot  = int(data["motion"])
    if mot not in (0, 1):
        raise ValueError("motion must be 0 or 1")
    if not (0 <= gas <= 10000):
        raise ValueError("gas_ppm out of range [0, 10000]")
    if not (-40 <= temp <= 125):
        raise ValueError("temperature out of range [-40, 125]")
    return gas, temp, mot

# ─── Routes ───────────────────────────────────────────────────────────────────
@app.route("/predict", methods=["POST"])
def predict():
    try:
        data = request.get_json(force=True)
        gas, temp, motion = validate_input(data)

        features = np.array([[gas, temp, motion]])
        pred_idx = model.predict(features)[0]
        proba    = model.predict_proba(features)[0]

        label = encoder.inverse_transform([pred_idx])[0]
        confidence = round(float(proba[pred_idx]) * 100, 1)

        # Build probability dict per class
        class_proba = {
            cls: round(float(p) * 100, 1)
            for cls, p in zip(encoder.classes_, proba)
        }

        reading = {
            "timestamp":   datetime.utcnow().isoformat() + "Z",
            "gas_ppm":     gas,
            "temperature": temp,
            "motion":      motion,
            "prediction":  label,
            "confidence":  confidence,
            "probabilities": class_proba,
            "source": str(data.get("source", "manual")).lower(),
            "device_id": data.get("device_id"),
        }
        history.append(reading)
        save_reading(reading)

        return jsonify({
            "status":      "ok",
            "prediction":  label,
            "confidence":  confidence,
            "probabilities": class_proba,
            "color":       STATUS_COLOR[label],
            "emoji":       STATUS_EMOJI[label],
            "timestamp":   reading["timestamp"],
        }), 200

    except ValueError as e:
        return jsonify({"status": "error", "message": str(e)}), 400
    except Exception as e:
        return jsonify({"status": "error", "message": "Internal server error", "detail": str(e)}), 500


@app.route("/status", methods=["GET"])
def status():
    esp32_status = get_esp32_status()
    return jsonify({
        "status":       "online",
        "model":        metadata["model_type"],
        "features":     metadata["features"],
        "classes":      metadata["classes"],
        "accuracy":     metadata["test_accuracy"],
        "total_readings": get_total_readings(),
        "database":     DB_PATH,
        "esp32":        esp32_status,
    }), 200


@app.route("/history", methods=["GET"])
def get_history():
    limit = min(int(request.args.get("limit", 50)), 100)
    return jsonify(fetch_history(limit)), 200


@app.route("/", methods=["GET"])
def dashboard():
    """Serve the standalone dashboard HTML."""
    dashboard_path = os.path.join(os.path.dirname(__file__), "dashboard", "index.html")
    if not os.path.exists(dashboard_path):
        dashboard_path = os.path.join(os.path.dirname(__file__), "index.html")

    if os.path.exists(dashboard_path):
        with open(dashboard_path, encoding="utf-8", errors="replace") as f:
            return f.read(), 200, {"Content-Type": "text/html"}

    return "<h2>Dashboard not found. Place dashboard/index.html or index.html next to this server.</h2>", 404


if __name__ == "__main__":
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    print("🚀 Smart Safety ML Server starting on http://0.0.0.0:5000")
    print(f"   Model: {metadata['model_type']}  |  Accuracy: {metadata['test_accuracy']}")
    app.run(host="0.0.0.0", port=5000, debug=False)
