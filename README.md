# Smart Safety Monitoring System – ML Edition
ESP32 + MQ3 + DHT22 + PIR → Random Forest → Flask API → Web Dashboard

## Project Structure

```
smart_safety_system/
├── ml/
│   ├── generate_dataset.py      # Synthetic dataset generator
│   ├── train_model.py           # Train & save Random Forest model
│   └── model/                   # Auto-created after training
│       ├── safety_model.pkl
│       ├── label_encoder.pkl
│       ├── model_metadata.json
│       ├── confusion_matrix.png
│       ├── feature_importance.png
│       └── decision_tree_rules.txt
├── server/
│   ├── app.py                   # Flask API server
│   └── requirements.txt
├── esp32/
│   └── smart_safety_esp32.ino  # Arduino sketch for ESP32
└── dashboard/
    └── index.html               # Real-time web dashboard
```

## Wiring – ESP32 Pin Map

| Sensor     | ESP32 Pin | Notes                          |
|------------|-----------|--------------------------------|
| MQ3 AOUT   | GPIO 34   | ADC1 only (ADC2 conflicts WiFi)|
| DHT22 DATA | GPIO 27   | 10kΩ pull-up to 3.3V           |
| PIR OUT    | GPIO 26   |                                |
| LED Red    | GPIO 25   | 220Ω resistor                  |
| LED Green  | GPIO 33   | 220Ω resistor                  |
| LED Blue   | GPIO 32   | 220Ω resistor                  |
| Buzzer +   | GPIO 14   | Active buzzer                  |

## Setup Steps

### 1. Install Python dependencies
```bash
pip install flask flask-cors scikit-learn pandas numpy matplotlib joblib
```

### 2. Generate dataset
```bash
cd ml
python generate_dataset.py
# → creates ml/data/sensor_data.csv (3000 rows)
```

### 3. Train the model
```bash
python train_model.py
# → saves ml/model/safety_model.pkl and label_encoder.pkl
```

### 4. Start Flask server
```bash
cd ../server
python app.py
# → http://0.0.0.0:5000
```

### 5. Open dashboard
Visit http://localhost:5000 in your browser (dashboard is served by Flask).
Or open dashboard/index.html directly and update API_BASE in the script.

### 6. Flash ESP32
- Open esp32/smart_safety_esp32.ino in Arduino IDE
- Set WIFI_SSID, WIFI_PASSWORD, SERVER_URL (your PC's LAN IP)
- Install: DHT library (Adafruit), ArduinoJson (v6)
- Flash and open Serial Monitor @ 115200 baud

## API Reference

### POST /predict
```json
Request:  { "gas_ppm": 150.0, "temperature": 28.5, "motion": 1 }
Response: {
  "status": "ok",
  "prediction": "SAFE",
  "confidence": 94.2,
  "probabilities": { "SAFE": 94.2, "WARNING": 4.1, "DANGER": 1.7 },
  "color": "#2ecc71",
  "emoji": "✅",
  "timestamp": "2025-03-21T10:23:45Z"
}
```

### GET /status
Returns model info, accuracy, total readings served.

### GET /history?limit=20
Returns last N sensor readings.

## Classification Thresholds (dataset)

| Class   | Gas (ppm) | Temperature (°C) | Motion |
|---------|-----------|------------------|--------|
| SAFE    | 50–300    | 18–30            | Rare   |
| WARNING | 300–600   | 30–45            | Mix    |
| DANGER  | 600–1000  | 45–80            | Common |

Retrain with real sensor data for production accuracy.
