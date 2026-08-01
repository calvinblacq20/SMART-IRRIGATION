/*==============================================================================
  SMART IRRIGATION SYSTEM  —  Wi-Fi + JSON API extension
  --------------------------------------------------------------------------
  Drop this file into the same sketch folder as smart_irrigation.ino.
  Arduino compiles all .ino files in the folder together, so no changes
  to the main file are needed.

  What this adds:
    • Connects to your Wi-Fi on boot (shows IP on LCD row 3)
    • Serves a tiny HTTP API the dashboard polls every 2 seconds
    • All API handlers run in the main loop (non-blocking, no RTOS)

  Libraries needed (Library Manager):
    - "ESPAsyncWebServer" by lacamera  (search: "ESPAsyncWebServer-esphome")
      OR the original by me-no-dev — either works.
    - "AsyncTCP" by dvarrel  (required by ESPAsyncWebServer on ESP32)

  These ship with the ESP32 core and need nothing extra:
    WiFi.h, Preferences.h (already in the main sketch)

  === SETUP (2 steps before flashing) ===
  1. Fill in your Wi-Fi SSID and password below.
  2. Flash the combined sketch (same board settings as before).
  The LCD will show "Wi-Fi OK" + the IP address.
  Open the dashboard, click the status pill, and enter that IP.
==============================================================================*/

#include <WiFi.h>
#include <ESPAsyncWebServer.h>

/*------------------------------------------------------------------------------
  YOUR WI-FI CREDENTIALS — fill these in before flashing
  Never commit this file to a public repo with real credentials.
------------------------------------------------------------------------------*/
#ifndef WIFI_SSID
  #define WIFI_SSID "YourNetworkName"
#endif
#ifndef WIFI_PASS
  #define WIFI_PASS "YourPassword"
#endif

/*------------------------------------------------------------------------------
  Server lives on port 80 (standard HTTP — no https needed on local network)
------------------------------------------------------------------------------*/
AsyncWebServer server(80);

// Set on every response so the dashboard (served from a file:// or other origin)
// can call us freely.
void addCors(AsyncWebServerResponse *r) {
  r->addHeader("Access-Control-Allow-Origin", "*");
  r->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
  r->addHeader("Access-Control-Allow-Headers", "Content-Type");
}

/*------------------------------------------------------------------------------
  Build the full state JSON from shared variables defined in smart_irrigation.ino
  All variables/functions used here are extern (in the same compilation unit).
------------------------------------------------------------------------------*/
String buildStateJson() {
  // fault = either sensor has gone bad
  bool fault = !soilOk || !dhtOk;

  char buf[512];
  snprintf(buf, sizeof(buf),
    "{"
      "\"soil\":%d,"
      "\"temp\":%.1f,"
      "\"hum\":%.0f,"
      "\"light\":%d,"
      "\"pump\":%d,"
      "\"mode\":\"%s\","
      "\"dry\":%d,"
      "\"wet\":%d,"
      "\"maxrun\":%d,"
      "\"cooldown\":%d,"
      "\"night\":%d,"
      "\"fault\":%d"
    "}",
    soilPct,
    isnan(tempC) ? 0.0f : tempC,
    isnan(humid) ? 0.0f : humid,
    lightLux,
    (int)pumpOn,
    cfg.mode == MODE_AUTO ? "auto" : "manual",
    (int)cfg.soilDry,
    (int)cfg.soilWet,
    (int)cfg.pumpMaxRun,
    (int)cfg.pumpCooldown,
    (int)cfg.nightOnly,
    (int)fault
  );
  return String(buf);
}

/*------------------------------------------------------------------------------
  clamp helpers (avoids redefinition — use unique names)
------------------------------------------------------------------------------*/
static int clampWifi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/*------------------------------------------------------------------------------
  API routes
------------------------------------------------------------------------------*/
void setupWifi() {
  // --- preflight for browsers ---
  server.on("/*", HTTP_OPTIONS, [](AsyncWebServerRequest *req) {
    AsyncWebServerResponse *r = req->beginResponse(204);
    addCors(r);
    req->send(r);
  });

  // GET /api/state  →  full JSON state
  server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest *req) {
    String json = buildStateJson();
    AsyncWebServerResponse *r = req->beginResponse(200, "application/json", json);
    addCors(r);
    req->send(r);
  });

  // GET /api/pump?on=1|0  →  manual pump control
  server.on("/api/pump", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (req->hasParam("on")) {
      bool on = req->getParam("on")->value() == "1";
      manualPump = on;
      if (cfg.mode == MODE_MANUAL) setPump(on);
    }
    AsyncWebServerResponse *r = req->beginResponse(200, "application/json", buildStateJson());
    addCors(r);
    req->send(r);
  });

  // GET /api/mode?m=auto|manual
  server.on("/api/mode", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (req->hasParam("m")) {
      String m = req->getParam("m")->value();
      cfg.mode = (m == "manual") ? MODE_MANUAL : MODE_AUTO;
      if (cfg.mode == MODE_AUTO) { manualPump = false; }
    }
    AsyncWebServerResponse *r = req->beginResponse(200, "application/json", buildStateJson());
    addCors(r);
    req->send(r);
  });

  // GET /api/set?dry=&wet=&maxrun=&cooldown=&night=  →  update + save settings
  server.on("/api/set", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (req->hasParam("dry"))      cfg.soilDry      = clampWifi(req->getParam("dry")->value().toInt(),      5,  90);
    if (req->hasParam("wet"))      cfg.soilWet      = clampWifi(req->getParam("wet")->value().toInt(),     10,  95);
    if (req->hasParam("maxrun"))   cfg.pumpMaxRun   = clampWifi(req->getParam("maxrun")->value().toInt(),   5, 600);
    if (req->hasParam("cooldown")) cfg.pumpCooldown = clampWifi(req->getParam("cooldown")->value().toInt(), 0, 3600);
    if (req->hasParam("night"))    cfg.nightOnly     = req->getParam("night")->value() == "1" ? 1 : 0;
    // enforce the dry < wet invariant (dry must always be at least 5 below wet)
    if (cfg.soilDry >= cfg.soilWet - 4) cfg.soilDry = cfg.soilWet - 5;
    saveSettings();  // persist to NVS (function defined in smart_irrigation.ino)
    AsyncWebServerResponse *r = req->beginResponse(200, "application/json", buildStateJson());
    addCors(r);
    req->send(r);
  });

  // 404 for anything else
  server.onNotFound([](AsyncWebServerRequest *req) {
    AsyncWebServerResponse *r = req->beginResponse(404, "text/plain", "Not found");
    addCors(r);
    req->send(r);
  });
}

/*------------------------------------------------------------------------------
  wifiSetup() — called from setup() in smart_irrigation.ino (see patch below)
  Shows connection status on the LCD while connecting, then prints the IP.
------------------------------------------------------------------------------*/
void wifiSetup() {
  Serial.printf("[WiFi] Connecting to %s ...\n", WIFI_SSID);

  lcd.clear();
  printPadded(0, "Wi-Fi connecting...");
  printPadded(1, WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 15000) {
    delay(400);
    Serial.print('.');
  }

  if (WiFi.status() == WL_CONNECTED) {
    String ip = WiFi.localIP().toString();
    Serial.printf("\n[WiFi] Connected — IP: %s\n", ip.c_str());
    printPadded(2, "Wi-Fi OK");
    printPadded(3, ip);
    delay(2500);
    setupWifi();
    server.begin();
    Serial.println("[API] HTTP server started");
  } else {
    Serial.println("\n[WiFi] FAILED — running without network");
    printPadded(2, "Wi-Fi FAILED");
    printPadded(3, "offline mode");
    delay(2000);
  }

  lcd.clear();
}
