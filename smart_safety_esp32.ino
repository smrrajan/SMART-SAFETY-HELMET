/*
 * Smart Safety Monitoring System - ESP32 Client
 * Sensors: MQ-2 (gas), LM35 (temperature), HC-SR04 (presence), buzzer
 *
 * Install via Arduino Library Manager:
 *   - ArduinoJson (v6)
 *
 * Before upload:
 *   1. Set WIFI_SSID / WIFI_PASSWORD
 *   2. Set SERVER_URL to your laptop LAN IP
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

const char* WIFI_SSID = "RAJAN";
const char* WIFI_PASSWORD = "12345678";
const char* SERVER_URL = "http://172.30.219.234 :5000/predict";
const char* DEVICE_ID = "ESP32-SAFETY-01";

// Pin map
#define MQ2_PIN        34   // ADC1 input
#define LM35_PIN       35   // ADC1 input
#define TRIG_PIN       5
#define ECHO_PIN       18
#define BUZZER_PIN     26

// Treat nearby object as motion/presence
const float MOTION_DISTANCE_CM = 50.0f;
const unsigned long SEND_INTERVAL = 5000;

unsigned long lastSendTime = 0;

void connectWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected");
    Serial.print("ESP32 IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi connection failed");
  }
}

float readTemperatureC() {
  int raw = analogRead(LM35_PIN);
  float voltage = (raw / 4095.0f) * 3.3f;
  float temperatureC = voltage * 100.0f;
  return temperatureC;
}

float readGasPPM() {
  int raw = analogRead(MQ2_PIN);

  // Simple approximation for this project's expected 0-1000 ppm model input.
  float ppm = map(raw, 0, 4095, 0, 1000);
  if (ppm < 0) ppm = 0;
  if (ppm > 1000) ppm = 1000;
  return ppm;
}

float readDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration <= 0) {
    return -1.0f;
  }

  float distance = duration * 0.0343f / 2.0f;
  return distance;
}

int readMotionLikeState(float distanceCm) {
  if (distanceCm > 0 && distanceCm <= MOTION_DISTANCE_CM) {
    return 1;
  }
  return 0;
}

void applyStatus(const String& status) {
  if (status == "DANGER") {
    digitalWrite(BUZZER_PIN, HIGH);
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== Smart Safety Monitoring System ===");

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(BUZZER_PIN, LOW);

  connectWiFi();

  Serial.println("Warming up MQ-2 sensor for 20 seconds...");
  delay(20000);
  Serial.println("Ready!");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  unsigned long now = millis();
  if (now - lastSendTime < SEND_INTERVAL) {
    return;
  }
  lastSendTime = now;

  float gasPPM = readGasPPM();
  float temperature = readTemperatureC();
  float distanceCm = readDistanceCM();
  int motion = readMotionLikeState(distanceCm);

  Serial.println("\n----- SENSOR DATA -----");
  Serial.printf("Gas: %.1f ppm\n", gasPPM);
  Serial.printf("Temperature: %.1f C\n", temperature);
  if (distanceCm > 0) {
    Serial.printf("Distance: %.1f cm\n", distanceCm);
  } else {
    Serial.println("Distance: no echo");
  }
  Serial.printf("Motion flag: %d\n", motion);

  StaticJsonDocument<200> doc;
  doc["gas_ppm"] = round(gasPPM * 10) / 10.0;
  doc["temperature"] = round(temperature * 10) / 10.0;
  doc["motion"] = motion;
  doc["source"] = "esp32";
  doc["device_id"] = DEVICE_ID;

  String payload;
  serializeJson(doc, payload);

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(SERVER_URL);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000);

    int httpCode = http.POST(payload);

    if (httpCode == 200) {
      String response = http.getString();
      Serial.println("Server response: " + response);

      StaticJsonDocument<512> resp;
      DeserializationError err = deserializeJson(resp, response);

      if (!err) {
        String prediction = resp["prediction"].as<String>();
        float confidence = resp["confidence"].as<float>();

        Serial.printf("Prediction: %s (%.1f%%)\n", prediction.c_str(), confidence);
        applyStatus(prediction);
      } else {
        Serial.println("JSON parse failed");
      }
    } else {
      Serial.printf("HTTP error: %d\n", httpCode);
      digitalWrite(BUZZER_PIN, LOW);
    }

    http.end();
  } else {
    String fallback;
    if (gasPPM > 600 || temperature > 45) fallback = "DANGER";
    else if (gasPPM > 300 || temperature > 30) fallback = "WARNING";
    else fallback = "SAFE";

    Serial.println("Offline fallback: " + fallback);
    applyStatus(fallback);
  }
}
