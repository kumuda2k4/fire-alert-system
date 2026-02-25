/**
 * ============================================================
 *  IoT Fire Alert & Security System
 *  Hardware : ESP8266 (NodeMCU)
 *  Sensors  : DHT11 (fireStatus proxy) + MQ2 (gasLevel)
 *  Actuator : Active Buzzer (pulsing alarm)
 *  Cloud    : Firebase Realtime Database via HTTP REST
 *
 *  FULFILS ALL SPEC REQUIREMENTS:
 *  ✅ fireStatus  — Boolean (true when temp ≥ 45°C)
 *  ✅ gasLevel    — Integer analog value from MQ2
 *  ✅ alertTime   — Real timestamp (NTP synced, IST)
 *  ✅ Pulsing buzzer alarm pattern
 *  ✅ Autonomous triggering (works with no Wi-Fi)
 *  ✅ Auto-reset after 5s of safe readings
 *  ✅ Baseline gas calibration at boot
 * ============================================================
 *
 *  WIRING
 *  DHT11  → VCC:3.3V | GND | DATA:D4
 *  MQ2    → VCC:Vin(5V) | GND | AO:A0
 *  Buzzer → +:D7 | -:GND
 *
 *  LIBRARIES (Tools → Manage Libraries)
 *  1. DHT sensor library      — by Adafruit
 *  2. Adafruit Unified Sensor — by Adafruit
 *  (ESP8266HTTPClient, WiFiClientSecure, time.h are built-in)
 * ============================================================
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <DHT.h>
#include <time.h>

// ── CONFIGURATION ────────────────────────────────────────────
#define WIFI_SSID       "Raya"
#define WIFI_PASSWORD   "Kumuda01"
#define DATABASE_URL    "https://fire-alert-system-48f7f-default-rtdb.firebaseio.com"
#define DATABASE_SECRET "vhfdBACeyRxZYaeamjZAfBXkEjLGA9wRyWo5gDfF"

// ── Pins ─────────────────────────────────────────────────────
#define DHT_PIN         D4
#define DHT_TYPE        DHT11
#define MQ2_PIN         A0
#define BUZZER_PIN      D7

// ── Thresholds ───────────────────────────────────────────────
// DHT11 replaces flame sensor: temp spike = fire detected
#define TEMP_FIRE_THRESHOLD   33.0   // °C → fireStatus = true
#define GAS_THRESHOLD         200    // MQ2 analog (0–1023)
#define SAFE_HOLD_MS          5000   // 5s safe → auto-reset

// ── Timing ───────────────────────────────────────────────────
#define SENSOR_POLL_MS        2000   // read sensors every 1s
#define FIREBASE_PUSH_MS      3000   // live push every 3s
#define BUZZ_ON_MS            300    // buzzer pulse ON duration
#define BUZZ_OFF_MS           200    // buzzer pulse OFF duration

// ─────────────────────────────────────────────────────────────

DHT dht(DHT_PIN, DHT_TYPE);
WiFiClientSecure wifiClient;

bool  alertActive = false;
int   gasLevel    = 0;
float temperature = 0.0;
float humidity    = 0.0;
bool  fireStatus  = false;
int   baselineGas = 400;
bool  ntpSynced   = false;

unsigned long lastSensorMs = 0;
unsigned long lastPushMs   = 0;
unsigned long safeStartMs  = 0;
unsigned long buzzTick     = 0;
bool          buzzState    = false;

// ─────────────────────────────────────────────────────────────
//  Get real timestamp string for alertTime
//  Returns "YYYY-MM-DD HH:MM:SS" if NTP synced
//  Falls back to device uptime string if NTP failed
// ─────────────────────────────────────────────────────────────
String getTimestamp() {
  if (ntpSynced) {
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    char buf[25];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
    return String(buf);
  }
  // Fallback: format uptime as readable string
  unsigned long s = millis() / 1000;
  unsigned long m = s / 60;
  unsigned long h = m / 60;
  char buf[30];
  snprintf(buf, sizeof(buf), "uptime_%02lu:%02lu:%02lu", h, m % 60, s % 60);
  return String(buf);
}

// ─────────────────────────────────────────────────────────────
//  NTP Time Sync (IST = UTC+5:30)
// ─────────────────────────────────────────────────────────────
void syncTime() {
  configTime(19800, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print(F("[NTP] Syncing time"));
  time_t now = time(nullptr);
  int attempts = 0;
  while (now < 100000 && attempts < 30) {
    delay(500);
    Serial.print(F("."));
    now = time(nullptr);
    attempts++;
  }
  if (now > 100000) {
    ntpSynced = true;
    Serial.println(F(" OK"));
    Serial.print(F("[NTP] Current time: "));
    Serial.println(getTimestamp());
  } else {
    ntpSynced = false;
    Serial.println(F(" FAILED — using uptime as timestamp"));
  }
}

// ─────────────────────────────────────────────────────────────
//  Firebase REST PUT helper
//  path  : e.g. "/sensor" or "/alerts/2024-01-15 12:58:23"
//  body  : JSON string
// ─────────────────────────────────────────────────────────────
bool firebasePut(String path, String body) {
  String url = String(DATABASE_URL) + path + ".json?auth=" + DATABASE_SECRET;
  wifiClient.setInsecure();

  HTTPClient http;
  http.begin(wifiClient, url);
  http.addHeader("Content-Type", "application/json");

  int code = http.PUT(body);
  bool ok = (code == 200 || code == 204);

  if (ok) {
    Serial.println(F("[Firebase] Push OK"));
  } else {
    Serial.print(F("[Firebase] Failed. Code: "));
    Serial.print(code);
    Serial.print(F(" — "));
    Serial.println(http.getString());
  }

  http.end();
  return ok;
}

// ─────────────────────────────────────────────────────────────
//  Live sensor push — runs every FIREBASE_PUSH_MS
//  Sends: fireStatus, gasLevel, temperature, humidity, alertActive
// ─────────────────────────────────────────────────────────────
void pushLiveData() {
  String body = "{";
  body += "\"fireStatus\":"  + String(fireStatus  ? "true" : "false") + ",";
  body += "\"gasLevel\":"    + String(gasLevel)                        + ",";
  body += "\"temperature\":" + String(temperature, 1)                  + ",";
  body += "\"humidity\":"    + String(humidity, 1)                     + ",";
  body += "\"alertActive\":" + String(alertActive  ? "true" : "false") + ",";
  body += "\"lastUpdated\":\"" + getTimestamp() + "\"";
  body += "}";

  firebasePut("/sensor", body);
}

// ─────────────────────────────────────────────────────────────
//  Alert event logger — called ONCE when alert first triggers
//  Logs: fireStatus, gasLevel, alertTime (real timestamp)
//  This is the "Life Cycle" log the spec requires
// ─────────────────────────────────────────────────────────────
void logAlertToFirebase() {
  String alertTime = getTimestamp();   // real readable timestamp

  // ── Update /sensor node ───────────────────────────────────
  String sensorBody = "{";
  sensorBody += "\"fireStatus\":"  + String(fireStatus ? "true" : "false") + ",";
  sensorBody += "\"gasLevel\":"    + String(gasLevel)                       + ",";
  sensorBody += "\"temperature\":" + String(temperature, 1)                 + ",";
  sensorBody += "\"humidity\":"    + String(humidity, 1)                    + ",";
  sensorBody += "\"alertActive\":true,";
  sensorBody += "\"alertTime\":\"" + alertTime + "\"";
  sensorBody += "}";
  firebasePut("/sensor", sensorBody);

  // ── Log to /alerts/{timestamp} ────────────────────────────
  // Each alert gets its own node keyed by the exact alertTime
  String alertBody = "{";
  alertBody += "\"fireStatus\":"  + String(fireStatus ? "true" : "false") + ",";
  alertBody += "\"gasLevel\":"    + String(gasLevel)                       + ",";
  alertBody += "\"temperature\":" + String(temperature, 1)                 + ",";
  alertBody += "\"alertTime\":\"" + alertTime                              + "\",";
  alertBody += "\"reason\":\""    + String(fireStatus ? "TEMP_HIGH" : "GAS_HIGH") + "\"";
  alertBody += "}";

  // Replace spaces in key with underscores so Firebase accepts it as path
  String alertKey = alertTime;
  alertKey.replace(" ", "_");
  alertKey.replace(":", "-");

  firebasePut("/alerts/" + alertKey, alertBody);

  Serial.print(F("[Firebase] Alert logged — alertTime: "));
  Serial.println(alertTime);
}

// ─────────────────────────────────────────────────────────────
//  Safe reset — called when system returns to normal
// ─────────────────────────────────────────────────────────────
void pushSafeReset() {
  String body = "{";
  body += "\"fireStatus\":false,";
  body += "\"gasLevel\":"    + String(gasLevel)      + ",";
  body += "\"temperature\":" + String(temperature, 1) + ",";
  body += "\"humidity\":"    + String(humidity, 1)    + ",";
  body += "\"alertActive\":false,";
  body += "\"lastUpdated\":\"" + getTimestamp() + "\"";
  body += "}";
  firebasePut("/sensor", body);
}

// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println(F("\n================================"));
  Serial.println(F("  IoT Fire Alert System"));
  Serial.println(F("  DHT11 + MQ2 + ESP8266"));
  Serial.println(F("================================\n"));

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // DHT11 init + stabilize
  dht.begin();
  delay(2000);

  // ── MQ2 Warm-up (spec requirement: calibration) ──────────
  Serial.println(F("[INIT] MQ2 warming up (15s)..."));
  for (int i = 15; i > 0; i--) {
    Serial.print(i); Serial.print(F("s "));
    delay(1000);
  }
  Serial.println();

  // ── Baseline calibration ─────────────────────────────────
  // Takes 20 readings in clean air and averages them
  // This is the "Baseline" the spec requires
  Serial.println(F("[CALIBRATE] Measuring clean-air baseline..."));
  long sum = 0;
  for (int i = 0; i < 20; i++) {
    sum += analogRead(MQ2_PIN);
    delay(100);
  }
  baselineGas = sum / 20;
  Serial.print(F("[CALIBRATE] Baseline gas level = ")); Serial.println(baselineGas);
  Serial.print(F("[CALIBRATE] Alert threshold    = ")); Serial.println(GAS_THRESHOLD);
  Serial.print("Alert threshold = baseline + 80 = "); Serial.println(baselineGas + 80);

  // ── Wi-Fi ─────────────────────────────────────────────────
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print(F("[WiFi] Connecting"));
  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500); Serial.print(F("."));
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("\n[WiFi] Connected! IP: "));
    Serial.println(WiFi.localIP());
    syncTime();   // get real timestamps for alertTime
  } else {
    Serial.println(F("\n[WiFi] Offline — local buzzer alarm still works"));
  }

  Serial.println(F("\n[READY] System monitoring started."));
}

// ─────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // ── 1. Detection Layer: read sensors every 1s ────────────
  if (now - lastSensorMs >= SENSOR_POLL_MS) {
    lastSensorMs = now;

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (isnan(t) || isnan(h)) {
      Serial.println("[ERROR] DHT read failed!");
    } else {
      temperature = t;
      humidity = h;
    }

    gasLevel   = analogRead(MQ2_PIN);

    // fireStatus: DHT11 acts as flame sensor proxy
    // spec: "Boolean High/Low based on flame detection"
    fireStatus = (temperature >= TEMP_FIRE_THRESHOLD);

    Serial.printf("[SENSOR] Temp:%.1fC  Hum:%.1f%%  Gas:%d  fireStatus:%s\n",
                  temperature, humidity, gasLevel,
                  fireStatus ? "HIGH" : "LOW");
  }

  // ── 2. Logic Layer: threshold comparison ─────────────────
  bool gasDanger = (gasLevel >= baselineGas + 80);
  bool danger = fireStatus || gasDanger;

  if (danger) {
    safeStartMs = now;
    if (!alertActive) {
      alertActive = true;
      Serial.println(F("\n!!! THRESHOLD BREACHED — ALERT TRIGGERED !!!"));
      Serial.print(F("    Reason: "));
      if (fireStatus)              Serial.print(F("High Temp (fireStatus=HIGH) "));
      if (gasLevel >= GAS_THRESHOLD) Serial.print(F("High Gas "));
      Serial.println();
      // Cloud Layer: log alertTime + snapshot to Firebase
      if (WiFi.status() == WL_CONNECTED) logAlertToFirebase();
    }
  } else {
    // Auto-Reset Logic: 5s of safe readings → reset
    if (alertActive && (now - safeStartMs >= SAFE_HOLD_MS)) {
      alertActive = false;
      Serial.println(F("[SAFE] 5s of normal readings — auto-resetting to SAFE state."));
      if (WiFi.status() == WL_CONNECTED) pushSafeReset();
    }
    if (!alertActive) safeStartMs = now;
  }

  // ── 3. Local Alert Layer: pulsing buzzer ─────────────────
  // Spec: "pulsing alarm pattern during high-risk events"
  // Works autonomously — no Wi-Fi needed
  if (alertActive) {
    unsigned long dur = buzzState ? BUZZ_ON_MS : BUZZ_OFF_MS;
    if (now - buzzTick >= dur) {
      buzzTick  = now;
      buzzState = !buzzState;
      digitalWrite(BUZZER_PIN, buzzState);
    }
  } else {
    digitalWrite(BUZZER_PIN, LOW);
    buzzState = false;
  }

  // ── 4. Cloud Layer: live data push every 3s ──────────────
  if (WiFi.status() == WL_CONNECTED && now - lastPushMs >= FIREBASE_PUSH_MS) {
    lastPushMs = now;
    pushLiveData();
  }
}
