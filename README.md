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

- **Firmware (device):** `firmware/smart_irrigation/smart_irrigation.ino` — sensors, control,
  LCD menu, LEDs, settings saved to flash (NVS).
- **Simulation:** `firmware/wokwi/` — runs the whole thing in the browser at wokwi.com.
- **Web dashboard:** `webapp/index.html` — standalone site, live + demo modes, responsive
  (desktop + mobile), talks to the device API.
- **Device API:** a Wi-Fi web server on the ESP32 exposing the JSON the dashboard reads/writes.
  Lives in `firmware/smart_irrigation/smart_irrigation_wifi.ino` — compiled with the main sketch automatically.

### Web API contract (dashboard already coded to this)
| Method/Path | Purpose |
|---|---|
| `GET /api/state` | full live state as JSON |
| `GET /api/pump?on=1\|0` | manual pump on/off |
| `GET /api/mode?m=auto\|manual` | switch mode |
| `GET /api/set?dry=&wet=&maxrun=&cooldown=&night=` | update settings |

All responses send `Access-Control-Allow-Origin: *` so the standalone site can call the board.

---

## 3. Repository map

| Path | What |
|---|---|
| `firmware/smart_irrigation/` | production firmware (DHT11) |
| `firmware/wokwi/` | Wokwi sim: `diagram.json`, `sketch.ino`, `libraries.txt` |
| `firmware/wiring_diagram.png` | connection diagram |
| `webapp/index.html` | web dashboard (self-contained) |
| `webapp/bg.mp4`, `bg.jpg` | dashboard background media |
| `*.stl` / `*.step` | 3D enclosure (body + lid), 150×120×60 mm |
| `Project29_*_Report.docx` | enclosure + firmware/software reports |

---

## 4. How to run each piece

- **Flash the board:** open `firmware/smart_irrigation/smart_irrigation.ino` in Arduino IDE,
  board = *ESP32 Dev Module*, install the DHT + LiquidCrystal_I2C libraries, upload.
- **Simulate:** paste `firmware/wokwi/diagram.json` and `sketch.ino` into a new ESP32 project
  at wokwi.com, add the libraries, press play.
- **Dashboard:** open `webapp/index.html` in a browser (Demo mode runs with no hardware).
  Enter the board's IP in the connection dialog to go live.

---

## 5. Status & remaining work (priority order)

1. **Device-API firmware** ✅ — `smart_irrigation_wifi.ino` adds Wi-Fi + HTTP API.
   Fill in your SSID/password, flash, and the dashboard drives the real board.
2. **Version control** — the enclosing git repo is the whole home folder with no commits;
   give this project its own repo and an initial commit. Add a `.gitignore` (below).
3. **A control-logic test** — the hysteresis/safety logic is pure and should have one unit test.
4. **Performance** — `bg.mp4` is ~2.7 MB; fine locally, compress/lazy-load if hosted.
5. **Deploy** (optional) — host the dashboard on a static host if remote access is wanted
   (note: an HTTPS host cannot call an `http://` board — keep the site on the local network).
