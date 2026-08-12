/**
 * =================================================================================
 * SMART IRRIGATION SYSTEM - PRODUCTION ESP8266 FIRMWARE (v2.0.0)
 * Architectural Blueprint: Embedded Systems Standard (Non-Blocking, Failsafe)
 * Project 29
 * =================================================================================
 * Features:
 *  - Non-volatile configuration persistence via EEPROM
 *  - Non-blocking cooperative multitasking scheduler
 *  - Moving-average digital filter for analog soil sensor readings
 *  - Hardware chatter protection with instant REST manual overrides
 *  - Differential 20x4 LCD buffer rendering engine (zero flicker)
 *  - Failsafe watchdog timer & emergency runtime cutoff
 *  - High-performance REST API with full CORS support
 *
 * REST API (dashboard is wired to this exact contract):
 *   GET  /api/status  -> full live state JSON
 *   GET  /api/config  -> { startThreshold, stopThreshold, maxRuntimeMs }
 *   POST /api/pump    body { "state": true|false }   (forces manual mode)
 *   POST /api/mode    body { "mode": "auto"|"manual" }
 *   POST /api/config  body { startThreshold, stopThreshold, maxRuntimeMs }
 *   OPTIONS *         -> CORS pre-flight
 *
 * === DASHBOARD IS SERVED FROM THE BOARD ITSELF ===
 * The data/ folder next to this .ino holds a lightweight build of the
 * dashboard (index.html, manifest.json, sw.js — no video background, it's
 * too large for on-board flash). Upload it to LittleFS SEPARATELY from the
 * sketch, using the Arduino IDE's LittleFS uploader tool:
 *   - IDE 2.x (2.2.1+): "arduino-littlefs-upload" plugin
 *       github.com/earlephilhower/arduino-littlefs-upload — download the
 *       .vsix from its Releases page, place it in
 *       <home>\.arduinoIDE\plugins\ (Windows) and restart the IDE, then
 *       Ctrl+Shift+P -> "Upload LittleFS to Pico/ESP8266/ESP32"
 *       (close the Serial Monitor first or the upload will fail)
 *   - IDE 1.x: "ESP8266 LittleFS Data Upload" plugin
 *       github.com/earlephilhower/arduino-esp8266littlefs-plugin
 * Board setting: Tools -> Flash Size -> "4MB (FS:2MB OTA:~1019KB)" (default
 * on most NodeMCU boards) — the dashboard is ~60 KB, comfortably inside 2 MB.
 * If LittleFS isn't uploaded (or fails to mount), the API still works fine —
 * only the on-board dashboard route is skipped; use webapp/index.html
 * (laptop-served, full video background) instead.
 *
 * Libraries (Library Manager): DHT sensor library (Adafruit), Adafruit
 * Unified Sensor, ArduinoJson (v6), LiquidCrystal I2C (Frank de Brabander).
 * ESP8266WiFi / ESP8266WebServer / ESP8266mDNS / LittleFS / EEPROM / Wire
 * ship with the ESP8266 core.
 * =================================================================================
 */

#include <ArduinoJson.h>
#include <DHT.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <LiquidCrystal_I2C.h>
#include <LittleFS.h>
#include <Wire.h>

#define FIRMWARE_VERSION "2.0.0"
#define EEPROM_MAGIC_KEY 0x49525249 // 'IRRI' magic byte for config validation

// =================================================================================
// 1. HARDWARE PIN DEFINITIONS & CONSTANTS
// =================================================================================
// NOTE: DHT moved off D2/GPIO4 because D2 is now the I2C bus (LCD) —
// Wire.begin() defaults to SDA=D2(GPIO4), SCL=D1(GPIO5) on the NodeMCU.
namespace HardwarePin {
constexpr uint8_t SOIL_SENSOR = A0;
constexpr uint8_t DHT_DATA = 13;   // ESP8266 D7
constexpr uint8_t RELAY = 12;      // ESP8266 D6
constexpr uint8_t BTN_MANUAL = 14; // ESP8266 D5
constexpr uint8_t BTN_AUTO = 0;    // ESP8266 D3 (also the boot-mode pin —
                                    // don't hold this button while powering
                                    // on / resetting the board)
} // namespace HardwarePin

namespace RelayLogic {
constexpr uint8_t ACTIVE = HIGH; // Active HIGH relay configuration
constexpr uint8_t INACTIVE = LOW;
} // namespace RelayLogic

namespace SoilCalibration {
constexpr int RAW_DRY = 800;          // Sensor value in dry air
constexpr int RAW_WET = 300;          // Sensor value fully submerged
constexpr uint8_t FILTER_SAMPLES = 8; // Moving average sample size
} // namespace SoilCalibration

// Safety clamps applied to values written via POST /api/config
namespace ConfigLimits {
constexpr int THRESHOLD_MIN = 0;
constexpr int THRESHOLD_MAX = 100;
constexpr uint32_t RUNTIME_MIN_MS = 3000UL;   // 3 s
constexpr uint32_t RUNTIME_MAX_MS = 600000UL; // 10 min
} // namespace ConfigLimits

// DHT Sensor Configuration — this build's sensor is a DHT22 (per the wired
// hardware/circuit diagram). Change to DHT11 only if you swap the physical
// sensor module.
#define DHTTYPE DHT22
DHT dht(HardwarePin::DHT_DATA, DHTTYPE);

// 20x4 LCD Setup
LiquidCrystal_I2C lcd(0x27, 20, 4);

// Web Server
ESP8266WebServer server(80);

// =================================================================================
// 2. DOMAIN ENUMS & CONFIGURATION STRUCTURES
// =================================================================================
enum class SystemMode : uint8_t { AUTO = 0, MANUAL = 1 };

enum class PumpState : uint8_t { OFF = 0, ON = 1 };

struct EEPROMConfig {
  uint32_t magic;
  int startThreshold;    // Moisture % to start auto-irrigation
  int stopThreshold;     // Moisture % to stop auto-irrigation
  uint32_t maxRuntimeMs; // Maximum continuous pump runtime limit
};

// Global System Configuration
EEPROMConfig config = {
    EEPROM_MAGIC_KEY,
    35,   // default start threshold: 35%
    45,   // default stop threshold: 45%
    30000 // default max runtime: 30000ms (30 seconds)
};

// =================================================================================
// 3. SYSTEM STATE MACHINE & TELEMETRY
// =================================================================================
struct SystemTelemetry {
  float temperature = 0.0f;
  float humidity = 0.0f;
  int soilMoisture = 0;
  int rawSoilAnalog = 0;

  SystemMode mode = SystemMode::AUTO;
  PumpState pump = PumpState::OFF;

  bool dhtSensorError = false;
  bool soilSensorError = false;

  uint32_t pumpStartTime = 0;
  uint32_t lastRelayToggleTime = 0;
  uint32_t lastSensorReadTime = 0;
  uint32_t lastLCDRefreshTime = 0;
  uint32_t lastWiFiCheckTime = 0;
} sysState;

// Button Debounce Objects
struct ButtonDebouncer {
  uint8_t pin;
  bool currentState;
  bool lastState;
  uint32_t lastDebounceTime;
};

ButtonDebouncer btnManual = {HardwarePin::BTN_MANUAL, HIGH, HIGH, 0};
ButtonDebouncer btnAuto = {HardwarePin::BTN_AUTO, HIGH, HIGH, 0};

constexpr uint32_t DEBOUNCE_DELAY_MS = 40;
constexpr uint32_t MIN_AUTO_CHATTER_DELAY_MS =
    1500; // Chatter protection for Auto Mode

// Whether the on-board dashboard filesystem mounted successfully.
// API routes work either way — this only gates the static file routes.
bool filesystemOk = false;

// =================================================================================
// Wi-Fi credentials — fill these in before flashing.
// NEVER commit real credentials to a public repo.
// =================================================================================
const char *WIFI_SSID = "YOUR_WIFI_NAME";
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// =================================================================================
// 4. FUNCTION DECLARATIONS
// =================================================================================
void loadEEPROMConfig();
void saveEEPROMConfig();

void initHardware();
void initFilesystem();
void initWiFi();
void initWebServer();

void taskServiceNetwork();
void taskReadSensors();
void taskHandleButtons();
void taskIrrigationControl();
void taskUpdateDisplay();

bool executePumpState(PumpState newState, const char *reason,
                      bool bypassLockout = false);
void setSystemMode(SystemMode newMode, const char *reason);
int readFilteredSoilMoisture();
void sendCORSHeaders();

// =================================================================================
// 5. SETUP & MAIN LOOP (NON-BLOCKING SCHEDULER)
// =================================================================================
void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println(F("\n=============================================="));
  Serial.println(F(" SMART IRRIGATION SYSTEM - ESP8266 FIRMWARE  "));
  Serial.println(F(" Architecture: Non-Blocking Event Loop v2.0  "));
  Serial.println(F("=============================================="));

  EEPROM.begin(sizeof(EEPROMConfig));
  loadEEPROMConfig();

  initHardware();
  initFilesystem();
  initWiFi();
  initWebServer();

  if (MDNS.begin("smart-irrigation")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println(
        F("[mDNS] Domain registered: http://smart-irrigation.local"));
  }

  Serial.println(
      F("[System] Initialization complete. Entering main task scheduler.\n"));
}

void loop() {
  // Feed ESP8266 Software Watchdog Timer
  ESP.wdtFeed();

  // Task 1: Network & HTTP Requests Service
  taskServiceNetwork();

  // Task 2: Digital & Analog Sensor Acquisition (Periodic)
  taskReadSensors();

  // Task 3: Physical Pushbutton Debouncing & ISR Handling
  taskHandleButtons();

  // Task 4: Irrigation Logic Engine & Emergency Safety Timers
  taskIrrigationControl();

  // Task 5: LCD 20x4 Screen Differential Renderer
  taskUpdateDisplay();
}

// =================================================================================
// 6. EEPROM STORAGE ENGINE
// =================================================================================
void loadEEPROMConfig() {
  EEPROMConfig stored;
  EEPROM.get(0, stored);

  if (stored.magic == EEPROM_MAGIC_KEY) {
    config = stored;
    Serial.println(F("[EEPROM] Valid configuration loaded successfully."));
  } else {
    Serial.println(F("[EEPROM] Magic key mismatch. Writing default settings."));
    saveEEPROMConfig();
  }
}

void saveEEPROMConfig() {
  config.magic = EEPROM_MAGIC_KEY;
  EEPROM.put(0, config);
  EEPROM.commit();
  Serial.println(F("[EEPROM] Configuration saved to flash storage."));
}

// =================================================================================
// 7. HARDWARE & PERIPHERAL INITIALIZATION
// =================================================================================
void initHardware() {
  // Relay Pin Configuration (Set inactive before output mode to prevent relay
  // power click)
  digitalWrite(HardwarePin::RELAY, RelayLogic::INACTIVE);
  pinMode(HardwarePin::RELAY, OUTPUT);

  // Button Inputs with Pull-Up
  pinMode(HardwarePin::BTN_MANUAL, INPUT_PULLUP);
  pinMode(HardwarePin::BTN_AUTO, INPUT_PULLUP);

  // Sensor Subsystem
  dht.begin();

  // 20x4 I2C LCD Display Initialization
  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F(" Smart Irrigation  "));
  lcd.setCursor(0, 1);
  lcd.print(F(" Firmware v2.0.0    "));
  lcd.setCursor(0, 2);
  lcd.print(F(" Booting System...  "));
}

// Mounts the on-flash filesystem holding the dashboard (data/index.html,
// manifest.json, sw.js). API routes still work even if this fails — the
// dashboard just won't be served from the board itself.
void initFilesystem() {
  filesystemOk = LittleFS.begin();
  if (filesystemOk) {
    Serial.println(F("[LittleFS] Mounted — dashboard will be served from the board."));
  } else {
    Serial.println(F("[LittleFS] Mount FAILED — upload the data/ folder with the LittleFS tool."));
  }
}

void initWiFi() {
  lcd.setCursor(0, 3);
  lcd.print(F(" WiFi: Connecting.. "));

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startAttempt < 8000)) {
    delay(250);
    Serial.print(F("."));
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("\n[WiFi] Connected! IP Address: "));
    Serial.println(WiFi.localIP());
    Serial.print(F("[WiFi] Dashboard:  http://"));
    Serial.println(WiFi.localIP());
    Serial.println(F("[WiFi]        or: http://smart-irrigation.local"));
    lcd.setCursor(0, 3);
    lcd.print(F(" WiFi: Connected!   "));
  } else {
    Serial.println(F(
        "\n[WiFi] Connection timeout. Operating in Standalone/Offline mode."));
    lcd.setCursor(0, 3);
    lcd.print(F(" WiFi: Offline Mode "));
  }
  delay(1200);
  lcd.clear();
}

// =================================================================================
// 8. CORE SYSTEM TASKS
// =================================================================================
void taskServiceNetwork() {
  server.handleClient();
  MDNS.update();

  uint32_t now = millis();
  if (now - sysState.lastWiFiCheckTime >= 10000) {
    sysState.lastWiFiCheckTime = now;
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.reconnect();
    }
  }
}

void taskReadSensors() {
  uint32_t now = millis();

  // Read Soil Moisture every 500ms using sample smoothing
  if (now - sysState.lastSensorReadTime >= 500) {
    sysState.lastSensorReadTime = now;

    sysState.rawSoilAnalog = analogRead(HardwarePin::SOIL_SENSOR);
    sysState.soilMoisture = readFilteredSoilMoisture();

    // Check sensor disconnection (Analog reading floating > 1010)
    sysState.soilSensorError =
        (sysState.rawSoilAnalog > 1010 && SoilCalibration::RAW_DRY < 950);

    // Read DHT sensor (the DHT library internally rate-limits actual
    // hardware polls to its minimum sample interval and returns the last
    // cached reading otherwise, so calling this every 500ms is safe)
    float t = dht.readTemperature();
    float h = dht.readHumidity();

    // Physical sanity validation: Catch NaN or out-of-range sensor readings
    // (e.g. DHT11/DHT22 type mismatch)
    if (isnan(t) || isnan(h) || t < -40.0f || t > 100.0f || h < 0.0f ||
        h > 100.0f) {
      sysState.dhtSensorError = true;
    } else {
      sysState.temperature = t;
      sysState.humidity = h;
      sysState.dhtSensorError = false;
    }
  }
}

int readFilteredSoilMoisture() {
  long sum = 0;
  for (uint8_t i = 0; i < SoilCalibration::FILTER_SAMPLES; i++) {
    sum += analogRead(HardwarePin::SOIL_SENSOR);
    delayMicroseconds(100);
  }
  int avgRaw = sum / SoilCalibration::FILTER_SAMPLES;

  int moisture =
      map(avgRaw, SoilCalibration::RAW_DRY, SoilCalibration::RAW_WET, 0, 100);
  return constrain(moisture, 0, 100);
}

void debounceButton(ButtonDebouncer &btn, void (*onPress)()) {
  bool reading = digitalRead(btn.pin);
  uint32_t now = millis();

  if (reading != btn.lastState) {
    btn.lastDebounceTime = now;
  }

  if ((now - btn.lastDebounceTime) > DEBOUNCE_DELAY_MS) {
    if (reading != btn.currentState) {
      btn.currentState = reading;
      if (btn.currentState == LOW) { // Active LOW button press
        onPress();
      }
    }
  }
  btn.lastState = reading;
}

void taskHandleButtons() {
  debounceButton(btnManual, []() {
    Serial.println(F("[Button] Manual Override Button Pressed"));
    setSystemMode(SystemMode::MANUAL, "Physical Button Trigger");
    PumpState targetState =
        (sysState.pump == PumpState::OFF) ? PumpState::ON : PumpState::OFF;
    executePumpState(targetState, "Physical Button Toggle", true);
  });

  debounceButton(btnAuto, []() {
    Serial.println(F("[Button] Auto Mode Button Pressed"));
    setSystemMode(SystemMode::AUTO, "Physical Button Trigger");
  });
}

void taskIrrigationControl() {
  uint32_t now = millis();

  // Safety Cutoff 1: Max continuous runtime limit
  if (sysState.pump == PumpState::ON) {
    if (now - sysState.pumpStartTime >= config.maxRuntimeMs) {
      executePumpState(PumpState::OFF, "Safety: Max Runtime Limit Reached",
                       true);
      return;
    }
  }

  // Safety Cutoff 2: Soil sensor failure
  if (sysState.soilSensorError && sysState.pump == PumpState::ON) {
    executePumpState(PumpState::OFF, "Safety: Soil Sensor Fault", true);
    return;
  }

  // Automatic Hysteresis Engine
  if (sysState.mode == SystemMode::AUTO) {
    if (sysState.soilMoisture < config.startThreshold &&
        sysState.pump == PumpState::OFF) {
      executePumpState(PumpState::ON, "Auto: Soil Moisture Below Threshold");
    } else if (sysState.soilMoisture >= config.stopThreshold &&
               sysState.pump == PumpState::ON) {
      executePumpState(PumpState::OFF, "Auto: Target Moisture Reached");
    }
  }
}

// =================================================================================
// 9. ACTUATOR CONTROL & STATE MUTATIONS
// =================================================================================
bool executePumpState(PumpState newState, const char *reason,
                      bool bypassLockout) {
  uint32_t now = millis();

  // Chatter protection for Automatic Mode triggers
  if (!bypassLockout &&
      (now - sysState.lastRelayToggleTime < MIN_AUTO_CHATTER_DELAY_MS)) {
    Serial.print(
        F("[Actuator] Chatter lockout active. Suppressing auto request: "));
    Serial.println(reason);
    return false;
  }

  if (newState == sysState.pump) {
    return true; // State unchanged
  }

  sysState.pump = newState;
  sysState.lastRelayToggleTime = now;
  sysState.lastLCDRefreshTime = 0; // Force immediate display update

  if (newState == PumpState::ON) {
    digitalWrite(HardwarePin::RELAY, RelayLogic::ACTIVE);
    sysState.pumpStartTime = now;
    Serial.print(F("[ACTUATOR] RELAY ON | Reason: "));
    Serial.println(reason);
  } else {
    digitalWrite(HardwarePin::RELAY, RelayLogic::INACTIVE);
    Serial.print(F("[ACTUATOR] RELAY OFF | Reason: "));
    Serial.println(reason);
  }
  return true;
}

void setSystemMode(SystemMode newMode, const char *reason) {
  if (sysState.mode != newMode) {
    sysState.mode = newMode;
    sysState.lastLCDRefreshTime = 0; // Force immediate display refresh
    Serial.print(F("[System] Mode changed to: "));
    Serial.print(newMode == SystemMode::AUTO ? "AUTO" : "MANUAL");
    Serial.print(F(" | Reason: "));
    Serial.println(reason);
  }
}

// =================================================================================
// 10. 20x4 LCD DIFFERENTIAL RENDERING ENGINE
// =================================================================================
void taskUpdateDisplay() {
  uint32_t now = millis();
  if (now - sysState.lastLCDRefreshTime < 1500) {
    return;
  }
  sysState.lastLCDRefreshTime = now;

  char buf[21];

  // ROW 0: Soil Moisture & Mode
  lcd.setCursor(0, 0);
  snprintf(buf, sizeof(buf), "Soil:%3d%% Mode:%-6s", sysState.soilMoisture,
           sysState.mode == SystemMode::AUTO ? "AUTO" : "MANUAL");
  lcd.print(buf);

  // ROW 1: Temperature & Pump Status
  lcd.setCursor(0, 1);
  if (sysState.dhtSensorError) {
    snprintf(buf, sizeof(buf), "Temp: ERR   Pump:%-3s",
             sysState.pump == PumpState::ON ? "ON" : "OFF");
  } else {
    snprintf(buf, sizeof(buf), "Temp: %2dC   Pump:%-3s",
             (int)sysState.temperature,
             sysState.pump == PumpState::ON ? "ON" : "OFF");
  }
  lcd.print(buf);

  // ROW 2: Humidity & Wi-Fi Connection
  lcd.setCursor(0, 2);
  if (sysState.dhtSensorError) {
    snprintf(buf, sizeof(buf), "Hum : ERR   WiFi:%-3s",
             WiFi.status() == WL_CONNECTED ? "ON" : "OFF");
  } else {
    snprintf(buf, sizeof(buf), "Hum : %2d%%   WiFi:%-3s",
             (int)sysState.humidity,
             WiFi.status() == WL_CONNECTED ? "ON" : "OFF");
  }
  lcd.print(buf);

  // ROW 3: Status Summary
  lcd.setCursor(0, 3);
  if (sysState.soilSensorError) {
    lcd.print(F("Status: SENSOR FAULT"));
  } else if (sysState.pump == PumpState::ON &&
             sysState.mode == SystemMode::AUTO) {
    lcd.print(F("Status: Auto Water  "));
  } else if (sysState.pump == PumpState::ON &&
             sysState.mode == SystemMode::MANUAL) {
    lcd.print(F("Status: Manual Water"));
  } else if (sysState.mode == SystemMode::MANUAL) {
    lcd.print(F("Status: Standby(MAN)"));
  } else {
    lcd.print(F("Status: Monitoring  "));
  }
}

// =================================================================================
// 11. REST API ENDPOINTS & CORS HEADERS
// =================================================================================
void sendCORSHeaders() {
  server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
  server.sendHeader(F("Access-Control-Allow-Methods"), F("GET, POST, OPTIONS"));
  server.sendHeader(F("Access-Control-Allow-Headers"), F("Content-Type"));
}

void initWebServer() {
  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) {
      sendCORSHeaders();
      server.send(204);
    } else {
      sendCORSHeaders();
      server.send(404, "application/json",
                  "{\"success\":false,\"error\":\"Endpoint Not Found\"}");
    }
  });

  // GET /api/status - Live System Telemetry
  server.on("/api/status", HTTP_GET, []() {
    StaticJsonDocument<384> doc;
    doc["device"] = "smart-irrigation";
    doc["version"] = FIRMWARE_VERSION;
    doc["ip"] = WiFi.localIP().toString();
    doc["uptime"] = millis() / 1000;
    doc["wifi"] = (WiFi.status() == WL_CONNECTED);
    doc["mode"] = (sysState.mode == SystemMode::AUTO) ? "auto" : "manual";
    doc["pump"] = (sysState.pump == PumpState::ON);
    doc["soilMoisture"] = sysState.soilMoisture;
    doc["temperature"] = sysState.temperature;
    doc["humidity"] = sysState.humidity;
    doc["sensorError"] = (sysState.dhtSensorError || sysState.soilSensorError);

    String response;
    serializeJson(doc, response);
    sendCORSHeaders();
    server.send(200, "application/json", response);
  });

  // GET /api/config - Active Configuration Parameters
  server.on("/api/config", HTTP_GET, []() {
    StaticJsonDocument<192> doc;
    doc["startThreshold"] = config.startThreshold;
    doc["stopThreshold"] = config.stopThreshold;
    doc["maxRuntimeMs"] = config.maxRuntimeMs;

    String response;
    serializeJson(doc, response);
    sendCORSHeaders();
    server.send(200, "application/json", response);
  });

  // POST /api/pump - Instant Manual Pump Command Override
  server.on("/api/pump", HTTP_POST, []() {
    sendCORSHeaders();
    if (!server.hasArg("plain")) {
      server.send(400, "application/json",
                  "{\"success\":false,\"error\":\"JSON payload required\"}");
      return;
    }

    StaticJsonDocument<192> doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err || !doc.containsKey("state")) {
      server.send(400, "application/json",
                  "{\"success\":false,\"error\":\"Invalid JSON or missing "
                  "'state' field\"}");
      return;
    }

    bool targetState = doc["state"];
    setSystemMode(SystemMode::MANUAL, "API Manual Request");

    // Pass bypassLockout = true for 100% instant manual execution on first
    // click
    bool executed = executePumpState(
        targetState ? PumpState::ON : PumpState::OFF, "API Request", true);

    StaticJsonDocument<128> resDoc;
    resDoc["success"] = executed;
    resDoc["pump"] = (sysState.pump == PumpState::ON);
    resDoc["mode"] = "manual";

    String resStr;
    serializeJson(resDoc, resStr);
    server.send(200, "application/json", resStr);
  });

  // POST /api/mode - Mode Switch Command
  server.on("/api/mode", HTTP_POST, []() {
    sendCORSHeaders();
    if (!server.hasArg("plain")) {
      server.send(400, "application/json",
                  "{\"success\":false,\"error\":\"JSON payload required\"}");
      return;
    }

    StaticJsonDocument<192> doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err || !doc.containsKey("mode")) {
      server.send(400, "application/json",
                  "{\"success\":false,\"error\":\"Invalid JSON or missing "
                  "'mode' field\"}");
      return;
    }

    String reqMode = doc["mode"];
    if (reqMode == "auto") {
      setSystemMode(SystemMode::AUTO, "API Request");
    } else if (reqMode == "manual") {
      setSystemMode(SystemMode::MANUAL, "API Request");
    } else {
      server.send(
          400, "application/json",
          "{\"success\":false,\"error\":\"Mode must be 'auto' or 'manual'\"}");
      return;
    }

    StaticJsonDocument<128> resDoc;
    resDoc["success"] = true;
    resDoc["mode"] = (sysState.mode == SystemMode::AUTO) ? "auto" : "manual";

    String resStr;
    serializeJson(resDoc, resStr);
    server.send(200, "application/json", resStr);
  });

  // POST /api/config - Save Configuration Settings to EEPROM Flash
  server.on("/api/config", HTTP_POST, []() {
    sendCORSHeaders();
    if (!server.hasArg("plain")) {
      server.send(400, "application/json",
                  "{\"success\":false,\"error\":\"JSON payload required\"}");
      return;
    }

    StaticJsonDocument<192> doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      server.send(400, "application/json",
                  "{\"success\":false,\"error\":\"Invalid JSON\"}");
      return;
    }

    if (doc.containsKey("startThreshold"))
      config.startThreshold = doc["startThreshold"];
    if (doc.containsKey("stopThreshold"))
      config.stopThreshold = doc["stopThreshold"];
    if (doc.containsKey("maxRuntimeMs"))
      config.maxRuntimeMs = doc["maxRuntimeMs"].as<uint32_t>();

    // Clamp to safe ranges before persisting (a bad request must never leave
    // the controller with an out-of-range or unsafe configuration)
    config.startThreshold = constrain(config.startThreshold,
                                      ConfigLimits::THRESHOLD_MIN,
                                      ConfigLimits::THRESHOLD_MAX);
    config.stopThreshold = constrain(config.stopThreshold,
                                     ConfigLimits::THRESHOLD_MIN,
                                     ConfigLimits::THRESHOLD_MAX);
    if (config.maxRuntimeMs < ConfigLimits::RUNTIME_MIN_MS)
      config.maxRuntimeMs = ConfigLimits::RUNTIME_MIN_MS;
    if (config.maxRuntimeMs > ConfigLimits::RUNTIME_MAX_MS)
      config.maxRuntimeMs = ConfigLimits::RUNTIME_MAX_MS;
    if (config.startThreshold >= config.stopThreshold) {
      config.startThreshold = config.stopThreshold - 5;
      if (config.startThreshold < ConfigLimits::THRESHOLD_MIN)
        config.startThreshold = ConfigLimits::THRESHOLD_MIN;
    }

    // Persist configuration to flash EEPROM
    saveEEPROMConfig();

    server.send(200, "application/json", "{\"success\":true}");
  });

  // ---- Dashboard, served straight from the board (LittleFS) ----
  // A lightweight build of webapp/index.html (gradient-mesh background,
  // no video — the full video version is too large for on-board flash).
  if (filesystemOk) {
    server.serveStatic("/", LittleFS, "/index.html");
    server.serveStatic("/index.html", LittleFS, "/index.html");
    server.serveStatic("/manifest.json", LittleFS, "/manifest.json");
    server.serveStatic("/sw.js", LittleFS, "/sw.js");
    Serial.println(F("[HTTP] Dashboard routes registered: / /index.html /manifest.json /sw.js"));
  }

  server.begin();
  Serial.println(F("[HTTP] Web Server started on port 80"));
}
