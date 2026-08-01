# Run the Smart Irrigation System live in Wokwi (free, in your browser)

Wokwi is a free online simulator — no install, no login needed to run.
You "open" this project the same way you imported the STL into Onshape:
create a blank ESP32 project, then paste in these two files.

## Files in this folder
| File | What it is |
|---|---|
| `diagram.json` | the wiring (ESP32 + all components) |
| `sketch.ino` | the firmware (simulation build) |
| `libraries.txt` | libraries Wokwi installs automatically |

## Steps (2 minutes)
1. Go to **https://wokwi.com/projects/new/esp32**
2. Click the **`diagram.json`** tab → select all → paste the contents of `diagram.json`.
3. Click the **`sketch.ino`** tab → select all → paste the contents of `sketch.ino`.
4. Add the libraries: click the **Library Manager** (📚 icon on the left) → search and add
   **DHT sensor library**, **Adafruit Unified Sensor**, **LiquidCrystal I2C**.
   *(Or: click the `+` next to the file tabs → New File → name it `libraries.txt` → paste
   the contents of `libraries.txt`. Wokwi then installs them for you.)*
5. Press the green **▶ Play** button. The LCD lights up and shows the dashboard.

## What you'll see / how to drive the demo
- **Soil moisture** = the **potentiometer** on the left. Turn it **down (dry)** → in AUTO
  mode the **relay clicks ON** (pump) and the **red LED** lights. Turn it **up (wet)** past
  the Wet threshold → pump turns **OFF**, **green LED** on.
- **DHT22** = drag its temperature/humidity sliders; values appear on the LCD.
- **Light** = the photoresistor's light slider (used only if you enable *Night-only*).
- **Buttons**: **MANUAL** toggles the pump by hand; **MENU** opens settings, **SET** edits /
  saves (see the button map in the main README).
- The **relay module** clicks and its LED lights when the pump is "running".
- Open the **Serial Monitor** (bottom panel) to watch live readings @ 115200.

## Notes
- This is the **same firmware** as `../smart_irrigation/smart_irrigation.ino`, with only
  3 simulation tweaks (marked at the top of `sketch.ino`): DHT22 instead of DHT11, the
  soil/light calibration set for the sim sliders, and shorter pump timers so it cycles fast.
- On the **real board** use the original `smart_irrigation.ino` (DHT11, your calibrated values).
- Pins in the sim match the firmware: I2C on GPIO18/19, relay on GPIO22, etc.

## Optional: even faster to open
If you use VS Code, install the **Wokwi for VS Code** extension, drop `diagram.json`,
`sketch.ino` and a compiled binary in a folder with a `wokwi.toml`, and press F1 →
"Wokwi: Start Simulator". For a quick look, the website steps above are easiest.
