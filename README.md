# Smart Irrigation System — Project 29

An ESP8266-based automatic plant-watering controller with a REST/JSON API and a web
dashboard — servable either from the board itself or from a laptop. Reads soil moisture
and air temperature/humidity, and drives a water pump through a relay using closed-loop
control.

---

## 1. What it is (PRD summary)

**Goal.** Keep soil moisture in a healthy band automatically, with a manual override and
a clear at-a-glance status from a browser dashboard, over Wi-Fi.

**Core user action.** Watch the soil/climate readings and let the system water on its own;
optionally take manual control and tune the thresholds.

**Data shape (live state).** `soilMoisture %`, `temperature °C`, `humidity %`, `pump on/off`,
`mode auto|manual`, thresholds `startThreshold/stopThreshold`, `maxRuntimeMs`, `sensorError`.

**Control logic.** AUTO: pump ON when `soil < startThreshold`, OFF when `soil > stopThreshold`
(hysteresis). Safety: max continuous run-time (force-stop, bypasses the chatter guard) +
sensor-fault handling. MANUAL: pump follows direct API/dashboard commands.

**Out of scope.** Multi-field/multi-device fleets, cloud accounts, historical database.

---

## 2. Architecture

```
Soil sensor ──┐
              ├─► ESP8266 ──► Relay ──► Pump
DHT22 ────────┘      │
                      ├─► REST/JSON API  /api/status /api/config /api/pump /api/mode
                      │        │
                      │        ├─► dashboard served from the board itself (LittleFS,
                      │        │   gradient-mesh background) — same-origin, no laptop
                      │        │
                      │        └─► webapp/index.html (video background), served from a
                      │            laptop or Vercel, calling the API over the LAN
                      └─► mDNS  smart-irrigation.local
```

- **Firmware (active — ESP8266):** `firmware/smart_irrigation_esp8266/smart_irrigation_esp8266.ino`
  — the current connection layer: sensors, hysteresis control, REST + JSON API, mDNS. The dashboard
  is wired to this. (An earlier ESP32 build with LCD/LEDs/NVS lives in `firmware/smart_irrigation/`.)
- **Simulation:** `firmware/wokwi/` — runs the whole thing in the browser at wokwi.com.
- **Web dashboard — two builds, same UI/API, different background:**
  - `webapp/index.html` — full version with the **video background**; serve it from a laptop
    (or Vercel for a demo). Canvas soil gauge, sparklines, soil-trend chart, Home/Data/Device/
    Settings views, live + demo mode, installable (manifest + offline service worker + Install
    button).
  - `firmware/smart_irrigation_esp8266/data/index.html` — the **same dashboard**, lightweight
    (animated gradient-mesh background instead of video — the video is ~4.7 MB and won't fit on
    the board's flash). This one is **served by the ESP8266 itself** at its own IP /
    `smart-irrigation.local`, so no laptop or Vercel is needed at all — open the board's address
    on any phone/browser on the Wi-Fi and the dashboard is right there, same-origin with the API.
- **Device API:** a Wi-Fi web server on the ESP8266 exposing the JSON the dashboard reads/writes.

### Web API contract (dashboard is coded to this)
| Method/Path | Purpose |
|---|---|
| `GET /api/status` | full live state as JSON (`soilMoisture, temperature, humidity, pump, mode, sensorError, …`) |
| `GET /api/config` | thresholds: `startThreshold, stopThreshold, maxRuntimeMs` |
| `POST /api/pump` | body `{ "state": true\|false }` — manual pump (forces manual mode) |
| `POST /api/mode` | body `{ "mode": "auto"\|"manual" }` |
| `POST /api/config` | body `{ startThreshold, stopThreshold, maxRuntimeMs }` — update + clamp |

All responses send `Access-Control-Allow-Origin: *` so the standalone site can call the board.
mDNS is enabled — the board is reachable at `http://smart-irrigation.local`.

---

## 3. Repository map

| Path | What |
|---|---|
| `firmware/smart_irrigation_esp8266/` | **active firmware** — ESP8266, DHT22, REST/JSON API + mDNS |
| `firmware/smart_irrigation_esp8266/data/` | dashboard **served by the board** via LittleFS (lightweight, no video) — upload separately from the sketch, see §4 |
| `firmware/smart_irrigation/` | earlier ESP32 build (DHT11, LCD, LEDs, NVS) |
| `firmware/wokwi/` | Wokwi sim: `diagram.json`, `sketch.ino`, `libraries.txt` |
| `firmware/wiring_diagram.png` | connection diagram |
| `webapp/` | Dashboard — single-file `index.html` (**full, video background**) + `sw.js`, `manifest.json` (installable/offline). `package.json`/`vite.config.js` are just for the dev server |
| `webapp/bg.mp4`, `bg.jpg` | dashboard background video + poster |
| `*.stl` / `*.step` | 3D enclosure (body + lid), 150×120×60 mm |
| `Project29_*_Report.docx` | enclosure + firmware/software reports |

---

## 4. How to run each piece

- **Flash the board:** open `firmware/smart_irrigation_esp8266/smart_irrigation_esp8266.ino`
  in Arduino IDE, board = *NodeMCU 1.0 (ESP-12E)*, Flash Size = *4MB (FS:2MB OTA:~1019KB)*
  (the default on most boards), install **DHT sensor library** + **Adafruit Unified Sensor** +
  **ArduinoJson**, fill in your Wi-Fi SSID/password, upload. The Serial monitor prints the IP
  and the dashboard URL.
- **Upload the on-board dashboard (LittleFS) — do this once, separately from the sketch upload:**
  install the LittleFS uploader tool — IDE 2.x: the *arduino-littlefs-upload* plugin
  ([releases](https://github.com/earlephilhower/arduino-littlefs-upload/releases), drop the
  `.vsix` in `%USERPROFILE%\.arduinoIDE\plugins\`, restart, then `Ctrl+Shift+P` →
  "Upload LittleFS to Pico/ESP8266/ESP32"; close the Serial Monitor first). IDE 1.x: the
  *ESP8266 LittleFS Data Upload* plugin. This packs `firmware/smart_irrigation_esp8266/data/`
  onto the board's flash — after that, opening the board's IP in any browser shows the dashboard
  directly, no laptop needed.
- **Simulate:** `firmware/wokwi/` — logic demonstration at wokwi.com.
- **Dashboard (video version, laptop/Vercel):** from `webapp/`, run `npm install` then
  `npm run dev -- --host` (or just serve the folder: `python -m http.server 8000`). Open it on a
  phone/laptop on the **same Wi-Fi** as the board, then Connect to `http://smart-irrigation.local`
  (or the board's IP). "Install" adds it to the home screen; the app shell then works offline.

---

## 5. Status & remaining work

1. **Firmware + dashboard** ✅ — ESP8266 REST firmware and both dashboard builds speak the same
   contract. Fill in your SSID/password, flash, (optionally) upload the LittleFS dashboard, connect.
2. **Enclosure** — the 3D enclosure still carries an LCD window / button holes from the earlier
   ESP32 build; the ESP8266 build has no on-device UI, so those provisions are now optional.
3. **Deploy** (optional) — the video-background dashboard runs from any static host (e.g. Vercel),
   but an HTTPS host cannot call an `http://` board (mixed content) — that build is demo/showcase
   only unless the dashboard is served locally or from the board itself (see §4).
