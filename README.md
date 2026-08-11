# Smart Irrigation System — Project 29

An ESP32-based automatic plant-watering controller with an on-device LCD interface
and a standalone web dashboard. Reads soil moisture, air temperature/humidity and
ambient light, and drives a water pump through a relay using closed-loop control.

---

## 1. What it is (PRD summary)

**Goal.** Keep soil moisture in a healthy band automatically, with a manual override and
a clear at-a-glance status — on the device (LCD + LEDs) and from a browser dashboard.

**Core user action.** Watch the soil/climate readings and let the system water on its own;
optionally take manual control and tune the thresholds.

**Data shape (live state).** `soil %`, `temp °C`, `humidity %`, `light`, `pump on/off`,
`mode auto|manual`, thresholds `dry/wet`, `maxrun`, `cooldown`, `night-only`, `fault`.

**Control logic.** AUTO: pump ON when `soil ≤ dry`, OFF when `soil ≥ wet` (hysteresis).
Safety: max continuous run-time + cooldown between waterings. MANUAL: pump follows the button.

**Out of scope.** Multi-field/multi-device fleets, cloud accounts, historical database.

---

## 2. Architecture

```
Sensors ──► ESP32 ──► Relay ──► Pump
  │           │
  │           ├─► LCD 20x4 + status LEDs        (on-device UI)
  │           └─► Wi-Fi JSON API  /api/state …  (← web dashboard)   [PENDING]
  │
Soil / DHT11 / LDR
```

- **Firmware (active — ESP8266):** `firmware/smart_irrigation_esp8266/smart_irrigation_esp8266.ino`
  — the current connection layer: sensors, hysteresis control, REST + JSON API, mDNS. The dashboard
  is wired to this. (An earlier ESP32 build with LCD/LEDs/NVS lives in `firmware/smart_irrigation/`.)
- **Simulation:** `firmware/wokwi/` — runs the whole thing in the browser at wokwi.com.
- **Web dashboard:** `webapp/` — an installable **PWA** (Vite, vanilla JS) that talks directly
  to the board's REST API, with an offline app-shell service worker, live 3 s polling, and a
  full-bleed video background. Needs no build step to run (plain ES modules).
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
| `firmware/smart_irrigation/` | earlier ESP32 build (DHT11, LCD, LEDs, NVS) |
| `firmware/wokwi/` | Wokwi sim: `diagram.json`, `sketch.ino`, `libraries.txt` |
| `firmware/wiring_diagram.png` | connection diagram |
| `webapp/` | PWA dashboard — `index.html`, `app.js`, `styles.css`, `sw.js`, `manifest.json` (+ `package.json`/`vite.config.js` for the dev server) |
| `webapp/bg.mp4`, `bg.jpg` | dashboard background video + poster |
| `*.stl` / `*.step` | 3D enclosure (body + lid), 150×120×60 mm |
| `Project29_*_Report.docx` | enclosure + firmware/software reports |

---

## 4. How to run each piece

- **Flash the board:** open `firmware/smart_irrigation_esp8266/smart_irrigation_esp8266.ino`
  in Arduino IDE, board = *NodeMCU 1.0 (ESP-12E)*, install **DHT sensor library** + **ArduinoJson**,
  fill in your Wi-Fi SSID/password, upload. The Serial monitor prints the IP.
- **Simulate:** `firmware/wokwi/` — logic demonstration at wokwi.com.
- **Dashboard (PWA):** from `webapp/`, run `npm install` then `npm run dev -- --host`
  (or just serve the folder: `python -m http.server 8000`). Open it on a phone/laptop on the
  **same Wi-Fi**, then Connect to `http://smart-irrigation.local` (or the board's IP). "Install"
  adds it to the home screen; the app shell then works offline.

---

## 5. Status & remaining work

1. **Firmware + PWA** ✅ — ESP8266 REST firmware and the PWA dashboard speak the same contract.
   Fill in your SSID/password, flash, connect.
2. **Enclosure** — the 3D enclosure still carries an LCD window / button holes from the earlier
   ESP32 build; the ESP8266 build has no on-device UI, so those provisions are now optional.
3. **Deploy** (optional) — the PWA runs from any static host, but an HTTPS host cannot call an
   `http://` board (mixed content), so keep it on the local network for live control.
