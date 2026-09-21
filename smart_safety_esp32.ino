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
const char* SERVER_URL = "http://10.201.11.89:5000/predict";
const char* DEVICE_ID = "ESP32-SAFETY-01";
const bool TEMP_SENSOR_ENABLED = true;
const float DEFAULT_TEMP_C = 28.0f;
const float LM35_ADC_REF_V = 3.3f;
const float LM35_ADC_MAX = 4095.0f;
const float LM35_TEMP_OFFSET_C = 0.0f;  // Use +ve/-ve to calibrate if needed.
const float LM35_DISCONNECT_VOLTAGE_V = 0.02f;
const int LM35_STUCK_HIGH_RAW = 4080;
const int LM35_STUCK_LOW_RAW = 5;
const int LM35_FAULT_CONFIRM_SAMPLES = 3;
const bool BUZZER_ENABLED = true;
// Most standalone buzzer modules used with ESP32 are active HIGH.
// If your module beeps when the pin goes LOW, change this back to true.
const bool BUZZER_ACTIVE_LOW = false;
const bool BUZZER_BOOT_TEST = true;

// Pin map
#define MQ2_PIN        34
#define LM35_PIN       35
#define TRIG_PIN       5
#define ECHO_PIN       18
#define BUZZER_PIN     26

const float MOTION_DISTANCE_CM = 50.0f;
const bool MOTION_BUZZER_ENABLED = true;
const float BUZZER_DISTANCE_THRESHOLD_CM = 15.0f;
const float GAS_WARNING_THRESHOLD_PPM = 120.0f;
const float GAS_DANGER_THRESHOLD_PPM = 250.0f;
const float TEMP_WARNING_THRESHOLD_C = 30.0f;
const float TEMP_DANGER_THRESHOLD_C = 45.0f;
const float TOXIC_GAS_BUZZER_THRESHOLD_PPM = 400.0f;
const float MAX_VALID_TEMP_C = 85.0f;
const float MIN_VALID_TEMP_C = 0.0f;
const int GAS_BASELINE_SAMPLES = 30;
const unsigned long SEND_INTERVAL = 2000;
const unsigned long SENSOR_POLL_INTERVAL = 200;
const unsigned long BUZZER_ON_MS = 250;
const unsigned long BUZZER_OFF_MS = 1000;
const unsigned long HTTP_TIMEOUT_MS = 2000;

unsigned long lastSendTime = 0;
unsigned long lastSensorPollTime = 0;
float gasBaselinePPM = 0.0f;
float latestRawGasPPM = 0.0f;
float latestAdjustedGasPPM = 0.0f;
float latestTemperature = DEFAULT_TEMP_C;
float latestTempVoltage = 0.0f;
int latestTempRaw = 0;
float latestDistanceCm = -1.0f;
int latestMotion = 0;
bool latestTempValid = true;
String latestTempFault = "None";
String latestStatus = "SAFE";
String lastBuzzerReason = "";
String currentBuzzerReason = "Off";
int tempHighFaultCount = 0;
int tempLowFaultCount = 0;

void setBuzzer(bool on) {
  if (!BUZZER_ENABLED) {
    digitalWrite(BUZZER_PIN, BUZZER_ACTIVE_LOW ? HIGH : LOW);
    return;
  }

  if (BUZZER_ACTIVE_LOW) {
    digitalWrite(BUZZER_PIN, on ? LOW : HIGH);
  } else {
    digitalWrite(BUZZER_PIN, on ? HIGH : LOW);
  }
}

void logBuzzerReason(const String& reason) {
  currentBuzzerReason = reason;
  if (reason != lastBuzzerReason) {
    Serial.println("[BUZZER] " + reason);
    lastBuzzerReason = reason;
  }
}

void runBuzzerSelfTest() {
  if (!BUZZER_ENABLED || !BUZZER_BOOT_TEST) {
    return;
  }

  Serial.println("[BUZZER] Self-test started");
  for (int i = 0; i < 2; i++) {
    setBuzzer(true);
    delay(180);
    setBuzzer(false);
    delay(220);
  }
  Serial.println("[BUZZER] Self-test done");
}

void printDivider() {
  Serial.println("--------------------------------------------------");
}

void printDetailedSnapshot() {
  printDivider();
  Serial.println("SMART SAFETY HELMET - LIVE STATUS");
  printDivider();

  Serial.println("Sensors");
  Serial.printf("  Gas Raw           : %.1f ppm\n", latestRawGasPPM);
  Serial.printf("  Gas Adjusted      : %.1f ppm\n", latestAdjustedGasPPM);
  Serial.printf("  Gas Baseline      : %.1f ppm\n", gasBaselinePPM);
  Serial.printf("  Temperature       : %.1f C\n", latestTemperature);
  Serial.printf("  Temp Raw ADC      : %d\n", latestTempRaw);
  Serial.printf("  Temp Voltage      : %.3f V\n", latestTempVoltage);
  if (latestDistanceCm > 0) {
    Serial.printf("  Distance          : %.1f cm\n", latestDistanceCm);
  } else {
    Serial.println("  Distance          : No echo");
  }
  Serial.printf("  Motion            : %s\n", latestMotion == 1 ? "Detected" : "Clear");

  printDivider();
  Serial.println("Safety Analysis");
  Serial.printf("  Status            : %s\n", latestStatus.c_str());
  Serial.printf("  Temp Sensor       : %s\n", TEMP_SENSOR_ENABLED ? "Enabled" : "Disabled");
  Serial.printf("  Temp Valid        : %s\n", latestTempValid ? "Yes" : "No");
  Serial.printf("  Temp Fault        : %s\n", latestTempFault.c_str());
  Serial.printf("  Buzzer State      : %s\n", currentBuzzerReason.c_str());

  printDivider();
  Serial.println("Thresholds");
  Serial.printf("  Obstacle Alert    : <= %.1f cm\n", BUZZER_DISTANCE_THRESHOLD_CM);
  Serial.printf("  Motion Range      : <= %.1f cm\n", MOTION_DISTANCE_CM);
  Serial.printf("  Gas Warning       : >= %.1f ppm\n", GAS_WARNING_THRESHOLD_PPM);
  Serial.printf("  Gas Danger        : >= %.1f ppm\n", GAS_DANGER_THRESHOLD_PPM);
  Serial.printf("  Toxic Gas Buzzer  : >= %.1f ppm with motion\n", TOXIC_GAS_BUZZER_THRESHOLD_PPM);
  Serial.printf("  Temp Warning      : >= %.1f C\n", TEMP_WARNING_THRESHOLD_C);
  Serial.printf("  Temp Danger       : >= %.1f C\n", TEMP_DANGER_THRESHOLD_C);
  Serial.printf("  LM35 Expected VOUT: %.2f V to %.2f V\n",
                MIN_VALID_TEMP_C / 100.0f,
                MAX_VALID_TEMP_C / 100.0f);

  printDivider();
  Serial.println("Network");
  Serial.printf("  WiFi SSID         : %s\n", WIFI_SSID);
  Serial.printf("  WiFi Status       : %s\n", WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("  ESP32 IP          : %s\n", WiFi.localIP().toString().c_str());
  }
  Serial.printf("  Server URL        : %s\n", SERVER_URL);
  printDivider();
}

int readAveragedAnalog(int pin, int samples = 8) {
  long total = 0;
  for (int i = 0; i < samples; i++) {
    total += analogRead(pin);
    delay(5);
  }
  return total / samples;
}

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
    Serial.print("Gateway IP: ");
    Serial.println(WiFi.gatewayIP());
    Serial.print("Target server: ");
    Serial.println(SERVER_URL);
  } else {
    Serial.println("\nWiFi connection failed");
    Serial.printf("WiFi status code: %d\n", WiFi.status());
  }
}

bool ensureWiFiConnected() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  Serial.println("WiFi disconnected. Reconnecting...");
  connectWiFi();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("[WIFI] Reconnect failed. Status code: %d\n", WiFi.status());
  }
  return WiFi.status() == WL_CONNECTED;
}

float readTemperatureC(int& rawOut, float& voltageOut) {
  rawOut = readAveragedAnalog(LM35_PIN);
  voltageOut = (rawOut / LM35_ADC_MAX) * LM35_ADC_REF_V;
  float temperatureC = (voltageOut * 100.0f) + LM35_TEMP_OFFSET_C;
  return temperatureC;
}

float readGasPPM() {
  int raw = readAveragedAnalog(MQ2_PIN);
  float ppm = map(raw, 0, 4095, 0, 1000);
  if (ppm < 0) ppm = 0;
  if (ppm > 1000) ppm = 1000;
  return ppm;
}

float calibrateGasBaseline() {
  float total = 0.0f;
  for (int i = 0; i < GAS_BASELINE_SAMPLES; i++) {
    total += readGasPPM();
    delay(150);
  }
  return total / GAS_BASELINE_SAMPLES;
}

float getAdjustedGasPPM(float rawGasPPM) {
  float adjusted = rawGasPPM - gasBaselinePPM;
  if (adjusted < 0) {
    adjusted = 0;
  }
  return adjusted;
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

String classifyLocally(float adjustedGasPPM, float temperature) {
  if (adjustedGasPPM >= GAS_DANGER_THRESHOLD_PPM || temperature >= TEMP_DANGER_THRESHOLD_C) {
    return "DANGER";
  }
  if (adjustedGasPPM >= GAS_WARNING_THRESHOLD_PPM || temperature >= TEMP_WARNING_THRESHOLD_C) {
    return "WARNING";
  }
  return "SAFE";
}

void applyStatus(const String& status, int motion, float adjustedGasPPM, float distanceCm) {
  if (!BUZZER_ENABLED) {
    logBuzzerReason("Disabled");
    setBuzzer(false);
    return;
  }

  if (distanceCm > 0 && distanceCm <= BUZZER_DISTANCE_THRESHOLD_CM) {
    logBuzzerReason("Obstacle alert");
    unsigned long cycleMs = BUZZER_ON_MS + BUZZER_OFF_MS;
    unsigned long phase = millis() % cycleMs;
    setBuzzer(phase < BUZZER_ON_MS);
    return;
  }

  if (motion == 1 && adjustedGasPPM >= TOXIC_GAS_BUZZER_THRESHOLD_PPM) {
    logBuzzerReason("Toxic gas + motion alert");
    unsigned long cycleMs = BUZZER_ON_MS + BUZZER_OFF_MS;
    unsigned long phase = millis() % cycleMs;
    setBuzzer(phase < BUZZER_ON_MS);
    return;
  }

  if (MOTION_BUZZER_ENABLED && motion == 1) {
    logBuzzerReason("Motion alert");
    unsigned long cycleMs = (BUZZER_ON_MS + BUZZER_OFF_MS) * 2;
    unsigned long phase = millis() % cycleMs;
    setBuzzer(phase < BUZZER_ON_MS);
    return;
  }

  if (status == "DANGER") {
    logBuzzerReason("Danger alert");
    unsigned long cycleMs = BUZZER_ON_MS + BUZZER_OFF_MS;
    unsigned long phase = millis() % cycleMs;
    setBuzzer(phase < BUZZER_ON_MS);
    return;
  }

  if (status == "WARNING") {
    logBuzzerReason("Warning alert");
    unsigned long cycleMs = (BUZZER_ON_MS + BUZZER_OFF_MS) * 2;
    unsigned long phase = millis() % cycleMs;
    setBuzzer(phase < BUZZER_ON_MS);
    return;
  }

  logBuzzerReason("Off");
  setBuzzer(false);
}

void refreshSensorState() {
  latestRawGasPPM = readGasPPM();
  latestAdjustedGasPPM = getAdjustedGasPPM(latestRawGasPPM);
  latestTemperature = TEMP_SENSOR_ENABLED ? readTemperatureC(latestTempRaw, latestTempVoltage) : DEFAULT_TEMP_C;
  latestDistanceCm = readDistanceCM();
  latestMotion = readMotionLikeState(latestDistanceCm);
  latestTempValid = true;
  latestTempFault = "None";

  if (latestTempRaw >= LM35_STUCK_HIGH_RAW) {
    tempHighFaultCount++;
    tempLowFaultCount = 0;
    if (tempHighFaultCount >= LM35_FAULT_CONFIRM_SAMPLES) {
      latestTempValid = false;
      latestTempFault = "ADC stuck HIGH (LM35 VOUT may be tied to 3V3/open on GPIO35)";
    }
  } else if (latestTempRaw <= LM35_STUCK_LOW_RAW || latestTempVoltage <= LM35_DISCONNECT_VOLTAGE_V) {
    tempLowFaultCount++;
    tempHighFaultCount = 0;
    if (tempLowFaultCount >= LM35_FAULT_CONFIRM_SAMPLES) {
      latestTempValid = false;
      latestTempFault = "Sensor disconnected/ADC stuck LOW";
    }
  } else if (latestTemperature < MIN_VALID_TEMP_C || latestTemperature > MAX_VALID_TEMP_C) {
    tempHighFaultCount = 0;
    tempLowFaultCount = 0;
    latestTempValid = false;
    latestTempFault = "Out of range";
  } else {
    tempHighFaultCount = 0;
    tempLowFaultCount = 0;
  }

  if (!latestTempValid) {
    // Keep helmet logic running by using fallback temperature when LM35 is invalid,
    // while still allowing gas, obstacle, and motion alerts to drive the buzzer.
    latestTemperature = DEFAULT_TEMP_C;
  }

  latestStatus = classifyLocally(latestAdjustedGasPPM, latestTemperature);
  applyStatus(latestStatus, latestMotion, latestAdjustedGasPPM, latestDistanceCm);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== Smart Safety Monitoring System ===");

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  analogReadResolution(12);

  analogSetPinAttenuation(MQ2_PIN, ADC_11db);
  analogSetPinAttenuation(LM35_PIN, ADC_11db);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  setBuzzer(false);
  runBuzzerSelfTest();

  Serial.println("[TEMP] LM35 wiring: VCC -> 3V3, GND -> GND, VOUT -> GPIO35");
  Serial.println("[TEMP] Normal LM35 output is about 0.25V to 0.85V for 25C to 85C.");
  Serial.println("[TEMP] If ADC stays near 4095, check for VOUT on 3V3, loose wiring, or wrong pin.");

  connectWiFi();

  Serial.println("Warming up MQ-2 sensor for 20 seconds...");
  delay(20000);
  Serial.println("Calibrating gas baseline in clean air...");
  gasBaselinePPM = calibrateGasBaseline();
  Serial.printf("Gas baseline locked: %.1f ppm\n", gasBaselinePPM);
  Serial.println("Ready!");
}

void loop() {
  unsigned long now = millis();
  if (now - lastSensorPollTime >= SENSOR_POLL_INTERVAL) {
    lastSensorPollTime = now;
    refreshSensorState();
  }

  if (now - lastSendTime < SEND_INTERVAL) {
    delay(10);
    return;
  }
  lastSendTime = now;

  printDetailedSnapshot();

  if (!TEMP_SENSOR_ENABLED) {
    Serial.println("[TEMP] Sensor disabled. Using default temperature.");
  } else if (!latestTempValid) {
    Serial.println("[TEMP] Invalid LM35 reading. Using fallback default temperature.");
    Serial.println("[TEMP] " + latestTempFault);
  }

  StaticJsonDocument<200> doc;
  doc["gas_ppm"] = round(latestAdjustedGasPPM * 10) / 10.0;
  doc["temperature"] = round(latestTemperature * 10) / 10.0;
  doc["motion"] = latestMotion;
  doc["source"] = "esp32";
  doc["device_id"] = DEVICE_ID;

  String payload;
  serializeJson(doc, payload);

  if (ensureWiFiConnected()) {
    int httpCode = -1;
    String response;

    for (int attempt = 1; attempt <= 2; attempt++) {
      HTTPClient http;
      http.begin(SERVER_URL);
      http.addHeader("Content-Type", "application/json");
      http.setTimeout(HTTP_TIMEOUT_MS);

      httpCode = http.POST(payload);
      if (httpCode == 200) {
        response = http.getString();
        http.end();
        break;
      }

      String httpError = HTTPClient::errorToString(httpCode);
      Serial.printf("HTTP error: %d (%s) on attempt %d\n", httpCode, httpError.c_str(), attempt);
      Serial.print("[HTTP] POST target: ");
      Serial.println(SERVER_URL);
      Serial.printf("[HTTP] WiFi status: %d\n", WiFi.status());
      http.end();

      if (attempt == 1) {
        WiFi.disconnect();
        delay(500);
        if (!ensureWiFiConnected()) {
          break;
        }
        delay(250);
      }
    }

    if (httpCode == 200) {
      Serial.println("Server response: " + response);

      StaticJsonDocument<512> resp;
      DeserializationError err = deserializeJson(resp, response);

      if (!err) {
        String prediction = resp["prediction"].as<String>();
        float confidence = resp["confidence"].as<float>();

        Serial.printf("[ML] Prediction: %s (%.1f%%)\n", prediction.c_str(), confidence);
        latestStatus = prediction;
        applyStatus(prediction, latestMotion, latestAdjustedGasPPM, latestDistanceCm);
      } else {
        Serial.println("[ML] JSON parse failed");
        String fallback = classifyLocally(latestAdjustedGasPPM, latestTemperature);
        Serial.println("[ML] Falling back to local safety logic after parse failure.");
        Serial.println("[ML] Local fallback: " + fallback);
        latestStatus = fallback;
        applyStatus(fallback, latestMotion, latestAdjustedGasPPM, latestDistanceCm);
      }
    } else {
      Serial.println("[ML] Server unreachable. Falling back to local safety logic.");
      String fallback = classifyLocally(latestAdjustedGasPPM, latestTemperature);
      Serial.println("[ML] Local fallback: " + fallback);
      latestStatus = fallback;
      applyStatus(fallback, latestMotion, latestAdjustedGasPPM, latestDistanceCm);
    }
  } else {
    String fallback = classifyLocally(latestAdjustedGasPPM, latestTemperature);
    Serial.println("[ML] Offline fallback: " + fallback);
    latestStatus = fallback;
    applyStatus(fallback, latestMotion, latestAdjustedGasPPM, latestDistanceCm);
  }
}
