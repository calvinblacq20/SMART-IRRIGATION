/*==============================================================================
  SMART IRRIGATION SYSTEM  —  ESP8266 firmware (NodeMCU / Wemos D1)
  Project 29 — updated connection layer.

  REST API (dashboard is wired to this exact contract):
    GET  /api/status  -> full live state JSON
    GET  /api/config  -> { startThreshold, stopThreshold, maxRuntimeMs }
    POST /api/pump    body { "state": true|false }   (forces manual mode)
    POST /api/mode    body { "mode": "auto"|"manual" }
    POST /api/config  body { startThreshold, stopThreshold, maxRuntimeMs }
    OPTIONS *         -> CORS pre-flight

  Board: ESP8266.  Sensors: soil (A0), DHT22 (D2/GPIO4).  Relay: D6/GPIO12.
  mDNS: http://smart-irrigation.local

  Libraries (Library Manager): DHT sensor library (Adafruit), ArduinoJson (v6).
  ESP8266WiFi / ESP8266WebServer / ESP8266mDNS ship with the ESP8266 core.
==============================================================================*/
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <DHT.h>
#include <ArduinoJson.h>

#define FIRMWARE_VERSION "1.0.0"

// ==========================================
// CONFIGURATION (Adjust these before flashing)
// ==========================================
const char* WIFI_SSID     = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Hardware Pins
const int PIN_SOIL_SENSOR = A0;
const int PIN_DHT         = 4;  // D2
const int PIN_RELAY       = 12; // D6

// Relay logic (Change to LOW if using an Active-Low relay module)
const int RELAY_ON  = HIGH;
const int RELAY_OFF = LOW;

// Soil Moisture Calibration (Raw ADC values 0-1023)
const int SOIL_DRY_RAW = 800; // Value when probe is completely dry in air
const int SOIL_WET_RAW = 300; // Value when probe is submerged in water

// DHT Setup
#define DHTTYPE DHT22
DHT dht(PIN_DHT, DHTTYPE);

// Web Server
ESP8266WebServer server(80);

// ==========================================
// SYSTEM STATE
// ==========================================
float currentTemp = 0.0;
float currentHum = 0.0;
int currentMoisture = 0;       // Percentage 0-100
bool isPumpOn = false;
bool isAutoMode = true;
bool dhtError = false;

// Configurable Settings
int configStartThreshold = 35; // Start watering below 35%
int configStopThreshold  = 45; // Stop watering above 45%
unsigned long configMaxPumpRuntime = 30000; // 30 seconds

// Safety clamps for /api/config
const unsigned long RUNTIME_MIN = 3000UL;     // 3 s  (never below the chatter window)
const unsigned long RUNTIME_MAX = 600000UL;   // 10 min

// Timers & Safety
unsigned long lastDHTReadTime = 0;
unsigned long lastSoilReadTime = 0;
unsigned long lastWiFiCheckTime = 0;
unsigned long pumpStartTime = 0;
unsigned long lastRelaySwitchTime = 0;

const unsigned long DHT_INTERVAL = 2500;   // 2.5 seconds
const unsigned long SOIL_INTERVAL = 1000;  // 1 second
const unsigned long WIFI_INTERVAL = 10000; // 10 seconds
const unsigned long MIN_RELAY_DELAY = 5000;// Minimum 5 seconds between relay toggles (Chatter protection)

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println(F("\n===================================="));
  Serial.println(F("SMART IRRIGATION SYSTEM"));
  Serial.print(F("Firmware: "));
  Serial.println(FIRMWARE_VERSION);
  Serial.print(F("Reset Reason: "));
  Serial.println(ESP.getResetReason());
  Serial.println(F("===================================="));

  initializePins();
  initializeSensors();
  initializeWiFi();
  initializeWebServer();
  initializeMDNS();

  Serial.println(F("System ready."));
}

// ==========================================
// LOOP
// ==========================================
void loop() {
  server.handleClient();
  MDNS.update();

  handleWiFi();
  readSensors();
  handlePumpSafety();
  updateIrrigation();
}

// ==========================================
// INITIALIZATION FUNCTIONS
// ==========================================
void initializePins() {
  // IMPORTANT: Set default state BEFORE making it an output to prevent relay blips on boot
  digitalWrite(PIN_RELAY, RELAY_OFF);
  pinMode(PIN_RELAY, OUTPUT);
  Serial.println(F("Pins initialized. Pump is OFF."));
}

void initializeSensors() {
  dht.begin();
  Serial.println(F("Sensors initialized."));
}

void initializeWiFi() {
  Serial.print(F("Connecting to WiFi: "));
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // Non-blocking wait, give it 10 seconds maximum during boot
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(500);
    Serial.print(F("."));
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("\nWiFi connected."));
    Serial.print(F("IP address: "));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("\nWiFi connection timed out. Proceeding offline."));
  }
}

void initializeMDNS() {
  if (MDNS.begin("smart-irrigation")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println(F("mDNS responder started at smart-irrigation.local"));
  }
}

// ==========================================
// CORE LOGIC FUNCTIONS
// ==========================================
void handleWiFi() {
  if (millis() - lastWiFiCheckTime >= WIFI_INTERVAL) {
    lastWiFiCheckTime = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println(F("WiFi disconnected. Attempting reconnect (non-blocking)..."));
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  }
}

void readSensors() {
  unsigned long currentMillis = millis();

  // Read DHT22
  if (currentMillis - lastDHTReadTime >= DHT_INTERVAL) {
    lastDHTReadTime = currentMillis;
    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (isnan(t) || isnan(h)) {
      if (!dhtError) {
        Serial.println(F("Warning: DHT sensor read failure."));
        dhtError = true;
      }
    } else {
      currentTemp = t;
      currentHum = h;
      dhtError = false;
    }
  }

  // Read Soil Moisture
  if (currentMillis - lastSoilReadTime >= SOIL_INTERVAL) {
    lastSoilReadTime = currentMillis;
    int rawValue = analogRead(PIN_SOIL_SENSOR);

    // Map raw ADC to percentage, constrain to 0-100
    currentMoisture = map(rawValue, SOIL_DRY_RAW, SOIL_WET_RAW, 0, 100);
    currentMoisture = constrain(currentMoisture, 0, 100);
  }
}

// force=true bypasses chatter protection — used ONLY by safety shut-offs so a
// max-runtime or sensor-fault cutoff can never be swallowed by the 5 s guard.
void setPump(bool state, String reason, bool force = false) {
  if (!force && millis() - lastRelaySwitchTime < MIN_RELAY_DELAY) {
    return; // Ignore normal command if trying to switch too fast
  }

  if (state && !isPumpOn) {
    digitalWrite(PIN_RELAY, RELAY_ON);
    isPumpOn = true;
    pumpStartTime = millis();
    lastRelaySwitchTime = millis();
    Serial.print(F("Pump ON - Reason: "));
    Serial.println(reason);
  }
  else if (!state && isPumpOn) {
    digitalWrite(PIN_RELAY, RELAY_OFF);
    isPumpOn = false;
    lastRelaySwitchTime = millis();
    Serial.print(F("Pump OFF - Reason: "));
    Serial.println(reason);
  }
}

void updateIrrigation() {
  if (!isAutoMode) return;

  // Sanity check: If moisture sensor disconnected/broken, do not water
  if (currentMoisture <= 0 && analogRead(PIN_SOIL_SENSOR) > 1000 && SOIL_DRY_RAW < 1000) {
    if (isPumpOn) setPump(false, "Safety: Soil sensor appears disconnected", true);
    return;
  }

  // Hysteresis logic
  if (currentMoisture < configStartThreshold && !isPumpOn) {
    setPump(true, "Auto Start (Moisture Low)");
  }
  else if (currentMoisture > configStopThreshold && isPumpOn) {
    setPump(false, "Auto Stop (Moisture High)");
  }
}

void handlePumpSafety() {
  if (isPumpOn) {
    if (millis() - pumpStartTime >= configMaxPumpRuntime) {
      setPump(false, "Safety: Max Runtime Exceeded", true); // force: must never be blocked
    }
  }
}

// ==========================================
// HTTP SERVER / API HANDLERS
// ==========================================
void sendCORSHeaders() {
  server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
  server.sendHeader(F("Access-Control-Allow-Methods"), F("GET, POST, OPTIONS"));
  server.sendHeader(F("Access-Control-Allow-Headers"), F("Content-Type"));
}

void handleOptions() {
  sendCORSHeaders();
  server.send(204); // No Content
}

void initializeWebServer() {
  // Pre-flight CORS request
  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) {
      handleOptions();
    } else {
      sendCORSHeaders();
      server.send(404, "application/json", "{\"success\":false,\"error\":\"Not Found\"}");
    }
  });

  server.on("/api/status", HTTP_GET, []() {
    StaticJsonDocument<384> doc;
    doc["device"] = "smart-irrigation";
    doc["version"] = FIRMWARE_VERSION;
    doc["ip"] = WiFi.localIP().toString();
    doc["uptime"] = millis() / 1000;
    doc["wifi"] = (WiFi.status() == WL_CONNECTED);
    doc["mode"] = isAutoMode ? "auto" : "manual";
    doc["pump"] = isPumpOn;
    doc["soilMoisture"] = currentMoisture;
    doc["temperature"] = currentTemp;
    doc["humidity"] = currentHum;
    doc["sensorError"] = dhtError;

    String response;
    serializeJson(doc, response);
    sendCORSHeaders();
    server.send(200, "application/json", response);
  });

  server.on("/api/config", HTTP_GET, []() {
    StaticJsonDocument<200> doc;
    doc["startThreshold"] = configStartThreshold;
    doc["stopThreshold"] = configStopThreshold;
    doc["maxRuntimeMs"] = configMaxPumpRuntime;

    String response;
    serializeJson(doc, response);
    sendCORSHeaders();
    server.send(200, "application/json", response);
  });

  server.on("/api/pump", HTTP_POST, []() {
    sendCORSHeaders();
    if (server.hasArg("plain") == false) {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Body required\"}");
      return;
    }

    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
      return;
    }

    if (!doc.containsKey("state")) {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing 'state' boolean\"}");
      return;
    }

    bool requestedState = doc["state"];

    // Switch to manual mode if controlling pump via API
    isAutoMode = false;

    setPump(requestedState, "API Manual Request");

    // Report the ACTUAL pump state (chatter protection may have deferred the switch)
    String resp = String("{\"success\":true,\"pump\":") + (isPumpOn ? "true" : "false") + "}";
    server.send(200, "application/json", resp);
  });

  server.on("/api/mode", HTTP_POST, []() {
    sendCORSHeaders();
    if (server.hasArg("plain") == false) {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Body required\"}");
      return;
    }

    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
      return;
    }

    String mode = doc["mode"];
    if (mode == "auto") {
      isAutoMode = true;
    } else if (mode == "manual") {
      isAutoMode = false;
    } else {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid mode\"}");
      return;
    }

    server.send(200, "application/json", "{\"success\":true}");
  });

  server.on("/api/config", HTTP_POST, []() {
    sendCORSHeaders();
    if (server.hasArg("plain") == false) {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Body required\"}");
      return;
    }

    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
      return;
    }

    if (doc.containsKey("startThreshold")) configStartThreshold = doc["startThreshold"];
    if (doc.containsKey("stopThreshold"))  configStopThreshold  = doc["stopThreshold"];
    if (doc.containsKey("maxRuntimeMs"))    configMaxPumpRuntime = doc["maxRuntimeMs"].as<unsigned long>();

    // Validate boundaries (clamp to sane ranges, then enforce hysteresis gap)
    configStartThreshold = constrain(configStartThreshold, 0, 100);
    configStopThreshold  = constrain(configStopThreshold, 0, 100);
    if (configMaxPumpRuntime < RUNTIME_MIN) configMaxPumpRuntime = RUNTIME_MIN;
    if (configMaxPumpRuntime > RUNTIME_MAX) configMaxPumpRuntime = RUNTIME_MAX;
    if (configStartThreshold >= configStopThreshold) {
      configStartThreshold = configStopThreshold - 5; // Enforce minimum hysteresis
      if (configStartThreshold < 0) configStartThreshold = 0;
    }

    server.send(200, "application/json", "{\"success\":true}");
  });

  server.begin();
}
