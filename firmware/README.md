# Smart Irrigation System — Firmware Package

ESP32 firmware for the Smart Irrigation System (the enclosure you already 3D-modelled).
This is the **software stage** of the project. Everything you need to flash, calibrate,
and demo is here.

---

## 1. What the system does

- Reads **soil moisture**, **temperature + humidity (DHT11)**, and **light (LDR)**.
- **AUTO mode:** turns the water pump ON when soil drops below the *Dry* threshold and
  OFF when it rises above the *Wet* threshold (hysteresis, so it doesn't chatter).
- **MANUAL mode:** the pump is toggled by the MANUAL button.
- **Safety:** the pump can never run longer than *Max run* seconds, and must wait
  *Cooldown* seconds between waterings.
- **LCD 20×4** shows a live dashboard; the 3 buttons drive a settings menu.
- **LEDs:** green = healthy/idle, red = pump running or soil dry, red **blinking** = sensor fault.
- **Settings persist** in flash (survive power-off).

---

## 2. Software you need (Arduino IDE)

1. **Boards Manager** → install **“esp32” by Espressif** (adds ESP32 Dev Module).
2. **Library Manager** → install:
   - **LiquidCrystal I2C** (Frank de Brabander)
   - **DHT sensor library** (Adafruit)
   - **Adafruit Unified Sensor** (dependency of the DHT library)
   - *Preferences* and *Wire* already ship with the ESP32 core.
3. **Tools → Board:** `ESP32 Dev Module`. **Upload speed:** 921600. Select your COM port.
4. Open `smart_irrigation/smart_irrigation.ino`, click **Upload**.

Open **Serial Monitor @ 115200** to watch live readings.

---

## 3. Wiring (matches the circuit diagram)

> These are the pins the firmware uses. If any wire on your board goes to a different
> GPIO, just change that one `#define` at the top of the `.ino` — nothing else.

| Component | Signal | ESP32 pin |
|---|---|---|
| DHT11 | DATA | GPIO4 |
| Soil moisture | A0 (analog) | GPIO34 (ADC, input-only) |
| LDR module | OUT (analog) | GPIO35 (ADC, input-only) |
| LCD 20×4 (I2C) | SDA | GPIO18 |
| LCD 20×4 (I2C) | SCL | GPIO19 |
| Relay ch1 | IN1 → pump | GPIO22 |
| Relay ch2 | IN2 → spare/valve | GPIO23 |
| Button MANUAL | signal | GPIO25 |
| Button MENU | signal | GPIO26 |
| Button SETTINGS | signal | GPIO27 |
| Green LED (+220 Ω) | anode | GPIO17 |
| Red LED (+220 Ω) | anode | GPIO16 |

Power/common:
- Sensor **VCC → 3V3**, all **GND → GND** (common ground with the relay and LEDs).
- Buttons: one leg to the GPIO, the other leg to **GND** (firmware uses internal pull-ups,
  so no external resistors needed — pressed = LOW).
- **Relay board VCC → 5V (VIN)** if you have it; the coils are happier at 5V.
- **Pump is powered from the battery through the relay contacts, NOT from the ESP32.**
  Wire: Battery+ → relay **COM1**, relay **NO1** → pump+, pump− → Battery−. Add a
  flyback diode across a DC pump if the module doesn't already isolate it.

### Pins you should double-check against your own diagram
Everything works regardless, but these are the 2 worth a 5-second look:
- **I2C (LCD)** — I read your diagram’s `D18 (SDA) / D19 (SCL)` labels. If you actually
  wired the LCD to the ESP32’s default `GPIO21/GPIO22`, change `PIN_SDA`/`PIN_SCL` to `21`/`22`.
- **Relay active level** — most blue 2-channel boards are **active-LOW** (default here).
  If your pump turns on when it should be off, set `#define RELAY_ACTIVE_LOW 0`.

---

## 4. Calibrating the soil sensor (do this once)

ESP32 analog readings are 0–4095. You must tell the firmware what "dry" and "wet" look like:

1. Upload the firmware, open Serial Monitor. Each line prints `soil=..%(rawNNNN)`.
2. Hold the probe **in dry air** → note the `raw` number → put it in `SOIL_RAW_DRY`.
3. Dip the probe **in a glass of water** (up to the line) → note `raw` → put it in `SOIL_RAW_WET`.
4. Re-upload. Now `Soil:` shows ~0 % dry and ~100 % wet.

(Optional) Do the same for the LDR with `LDR_RAW_DARK` / `LDR_RAW_BRIGHT`.

---

## 5. Using the buttons

**Home screen**
- **MANUAL** — toggles the pump on/off (switches the system into MANUAL mode).
- **MENU** or **SETTINGS** — opens the settings list.

**Settings list**
- **MENU** — move to the next item.
- **SETTINGS** — edit the highlighted item (or run *Save & Exit*).
- **MANUAL** — back to Home without saving.

**Edit screen**
- **SETTINGS (+)** / **MENU (−)** — change the value.
- **MANUAL** — confirm, back to the list.

Adjustable settings: **Mode** (AUTO/MANUAL), **Dry threshold %**, **Wet threshold %**,
**Max run (s)**, **Cooldown (s)**, **Night-only** (water only when dark). Choose
*Save & Exit* to store them permanently.

---

## 6. Test / verification checklist (for your demo & report)

| # | Test | Expected result |
|---|---|---|
| 1 | Power on | LCD splash → dashboard; green LED on |
| 2 | Squeeze probe / put in water | Soil % rises; in AUTO the pump stays OFF above Wet% |
| 3 | Probe in dry air (AUTO) | Below Dry%, pump turns ON, red LED on |
| 4 | Keep pump running | After *Max run* s it auto-stops (safety) |
| 5 | Press MANUAL | Pump toggles on/off immediately |
| 6 | Unplug soil sensor | Red LED blinks (fault), pump forced OFF |
| 7 | Cover the LDR with Night-only = YES | Watering allowed only while dark |
| 8 | Change a setting → Save & Exit → reboot | New value is remembered |
| 9 | Breadboard vs enclosure | Same behaviour after transplant |

---

## 7. Troubleshooting

| Symptom | Fix |
|---|---|
| LCD blank / boxes only | Try `LCD_ADDR 0x3F`; check SDA/SCL not swapped; tweak the LCD contrast pot |
| Pump logic inverted | Set `RELAY_ACTIVE_LOW 0` |
| Soil always 0 % or 100 % | Re-run calibration (step 4); confirm A0 → GPIO34 |
| Temp/Hum show `--` | DHT11 needs VCC on 3V3, ~1 s warm-up; check DATA → GPIO4 |
| Pump won’t stop / ESP resets when pump runs | Power the pump from the battery via the relay, never from the ESP32; add flyback diode |
| WiFi later? | Move analog sensors to ADC1 pins only (GPIO32–39) — already done here |

---

## 8. Where this fits in the project

1. ✅ Circuit (line) diagram
2. ✅ 3D enclosure model  (`../body.stl`, `../lid.stl` — now correctly 150×120×60 mm)
3. ✅ Breadboard layout
4. ✅ Hand sketch
5. **➡ Firmware (this package)**
6. Assembly & wiring inside the enclosure
7. Calibration + testing (use the checklist above)
8. Final report (circuit + code + test results + photos)

Firmware file: `smart_irrigation/smart_irrigation.ino`
