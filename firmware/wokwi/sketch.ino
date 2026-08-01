/*  SIMULATION BUILD for Wokwi (wokwi.com)  ---------------------------------
    Same firmware as smart_irrigation.ino, with 3 sim-only changes:
      * DHT22 part (Wokwi has no DHT11)
      * soil + light are potentiometer/LDR sweeps (0..4095 calibration)
      * shorter pump max-run / cooldown so the demo cycles quickly
    In the sim: turn the SOIL pot down to 'dry' -> pump turns ON.
-------------------------------------------------------------------------- */

/*==============================================================================
  SMART IRRIGATION SYSTEM  —  ESP32 firmware
  --------------------------------------------------------------------------
  Hardware (from the project circuit diagram):
    ESP32 Dev Module, DHT11 (temp/hum), capacitive/resistive soil moisture,
    LDR light module, LCD 20x4 (I2C), 2-channel relay (water pump + spare),
    3 tactile buttons (MANUAL / MENU / SETTINGS), red + green status LEDs,
    3.7V Li-ion + SPDT power switch.

  Features:
    * AUTO watering with hysteresis (start below DRY%, stop above WET%)
    * MANUAL override (button toggles pump)
    * Optional "water only at night" using the LDR
    * Safety: max continuous pump run-time + cooldown between waterings
    * Sensor-fault handling (DHT NaN / soil sensor unplugged)
    * LCD dashboard + 3-button menu to change settings live
    * Settings saved to flash (survive power-off) via Preferences (NVS)

  Libraries (Library Manager):
    - "LiquidCrystal I2C" by Frank de Brabander
    - "DHT sensor library" by Adafruit  (+ "Adafruit Unified Sensor")
    Preferences + Wire ship with the ESP32 core.

  Board: "ESP32 Dev Module".   See README.md for wiring + calibration.
==============================================================================*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <Preferences.h>

/*------------------------------------------------------------------------------
  PIN MAP  —  VERIFY THESE AGAINST YOUR OWN WIRING (they are the only thing that
  must match your board). Change a number here if your diagram used another pin.
------------------------------------------------------------------------------*/
#define PIN_DHT          4    // DHT11  DATA
#define PIN_SOIL         34   // Soil moisture  A0  (ADC1, input-only)
#define PIN_LDR          35   // LDR module     OUT (ADC1, input-only)
#define PIN_SDA          18   // LCD 20x4  I2C SDA   (diagram: "D18 (SDA)")
#define PIN_SCL          19   // LCD 20x4  I2C SCL   (diagram: "D19 (SCL)")
#define PIN_RELAY_PUMP   22   // Relay IN1 -> water pump
#define PIN_RELAY_AUX    23   // Relay IN2 -> spare (2nd pump / solenoid valve)
#define PIN_BTN_MANUAL   25   // MANUAL ON/OFF button
#define PIN_BTN_MENU     26   // MENU button
#define PIN_BTN_SET      27   // SETTINGS button
#define PIN_LED_GREEN    17   // green LED  (system OK / soil moist)
#define PIN_LED_RED      16   // red LED    (pump running / dry / alert)

/*------------------------------------------------------------------------------
  HARDWARE OPTIONS
------------------------------------------------------------------------------*/
#define DHTTYPE          DHT22   // Wokwi provides DHT22; use DHT11 on the real board
#define LCD_ADDR         0x27      // if the screen stays blank, try 0x3F
#define RELAY_ACTIVE_LOW 1         // most blue 2-ch relay boards are active-LOW

#if RELAY_ACTIVE_LOW
  #define RLY_ON   LOW
  #define RLY_OFF  HIGH
#else
  #define RLY_ON   HIGH
  #define RLY_OFF  LOW
#endif

/*------------------------------------------------------------------------------
  CALIBRATION  (see README "Calibrating the soil sensor")
  ESP32 ADC is 12-bit: raw values run 0..4095.
------------------------------------------------------------------------------*/
int SOIL_RAW_DRY   = 0;      // SIM: potentiometer fully left  = dry
int SOIL_RAW_WET   = 4095;   // SIM: potentiometer fully right = wet
int LDR_RAW_DARK   = 0;      // SIM
int LDR_RAW_BRIGHT = 4095;   // SIM

/*------------------------------------------------------------------------------
  TIMING
------------------------------------------------------------------------------*/
const uint32_t DHT_PERIOD  = 2000;   // DHT11 max ~0.5 Hz
const uint32_t SOIL_PERIOD = 1000;
const uint32_t UI_PERIOD   = 300;
const uint32_t DEBOUNCE_MS = 40;

/*------------------------------------------------------------------------------
  OBJECTS / STATE
------------------------------------------------------------------------------*/
LiquidCrystal_I2C lcd(LCD_ADDR, 20, 4);
DHT dht(PIN_DHT, DHTTYPE);
Preferences prefs;

enum Mode  { MODE_AUTO = 0, MODE_MANUAL = 1 };
enum UI    { UI_HOME = 0, UI_MENU = 1, UI_EDIT = 2 };

struct Settings {
  uint8_t  mode         = MODE_AUTO;
  uint8_t  soilDry      = 35;   // %  start watering below this
  uint8_t  soilWet      = 55;   // %  stop watering above this
  uint16_t pumpMaxRun   = 15;   // s  (shortened for a lively simulation)
  uint16_t pumpCooldown = 5;    // s  (shortened for a lively simulation)
  uint8_t  nightOnly    = 0;    // 1 = only water when it is dark
};
Settings cfg;

// live readings
float   tempC = NAN, humid = NAN;
int     soilRaw = 0, ldrRaw = 0;
int     soilPct = 0, lightPct = 0, lightLux = 0;
bool    soilOk = false, dhtOk = false;

// pump
bool     pumpOn = false, manualPump = false;
uint32_t pumpStartMs = 0, pumpStopMs = 0;

// ui
uint8_t  uiState = UI_HOME;
int8_t   menuIdx = 0;
const uint8_t MENU_COUNT = 7;   // Mode, Dry, Wet, MaxRun, Cooldown, Night, Save

// scheduler
uint32_t tDht = 0, tSoil = 0, tUi = 0;

/*==============================  BUTTONS  ====================================*/
struct Btn { uint8_t pin; bool last; uint32_t t; };
Btn bMan { PIN_BTN_MANUAL, HIGH, 0 };
Btn bMen { PIN_BTN_MENU,   HIGH, 0 };
Btn bSet { PIN_BTN_SET,    HIGH, 0 };

// returns true once on a fresh press (active-LOW, debounced)
bool pressed(Btn &b) {
  bool r = digitalRead(b.pin);
  if (r != b.last && (millis() - b.t) > DEBOUNCE_MS) {
    b.t = millis();
    b.last = r;
    if (r == LOW) return true;
  }
  return false;
}

/*==============================  SENSORS  ====================================*/
int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void readSoil() {
  // average a few samples to steady the ADC
  long acc = 0;
  for (int i = 0; i < 8; i++) { acc += analogRead(PIN_SOIL); delay(2); }
  soilRaw = acc / 8;
  soilOk  = (soilRaw > 50 && soilRaw < 4090);        // unplugged -> stuck at rail
  int pct = map(soilRaw, SOIL_RAW_DRY, SOIL_RAW_WET, 0, 100);
  soilPct = clampi(pct, 0, 100);
}

void readLight() {
  long acc = 0;
  for (int i = 0; i < 8; i++) { acc += analogRead(PIN_LDR); delay(2); }
  ldrRaw   = acc / 8;
  int pct  = map(ldrRaw, LDR_RAW_DARK, LDR_RAW_BRIGHT, 0, 100);
  lightPct = clampi(pct, 0, 100);
  lightLux = lightPct * 10;   // rough relative estimate (0..1000), not calibrated lux
}

void readDht() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  dhtOk = !(isnan(t) || isnan(h));
  if (dhtOk) { tempC = t; humid = h; }     // keep last good on a bad read
}

/*==============================  PUMP / CONTROL  =============================*/
void setPump(bool on) {
  if (on == pumpOn) return;
  pumpOn = on;
  digitalWrite(PIN_RELAY_PUMP, on ? RLY_ON : RLY_OFF);
  if (on) pumpStartMs = millis(); else pumpStopMs = millis();
  Serial.printf("[PUMP] %s\n", on ? "ON" : "OFF");
}

void updateControl() {
  uint32_t now = millis();

  if (cfg.mode == MODE_MANUAL) {
    setPump(manualPump);
  } else {                                   // ---- AUTO ----
    bool cooled  = (now - pumpStopMs) >= (uint32_t)cfg.pumpCooldown * 1000UL;
    bool nightOk = cfg.nightOnly ? (lightPct < 30) : true;
    if (!pumpOn) {
      if (soilOk && soilPct <= cfg.soilDry && cooled && nightOk) setPump(true);
    } else {
      if (!soilOk || soilPct >= cfg.soilWet) setPump(false);
    }
  }

  // safety: never let the pump run longer than pumpMaxRun
  if (pumpOn && (now - pumpStartMs) >= (uint32_t)cfg.pumpMaxRun * 1000UL) {
    setPump(false);
    if (cfg.mode == MODE_MANUAL) manualPump = false;
    Serial.println("[SAFETY] max run reached -> pump OFF");
  }
}

void updateLeds() {
  bool fault = !soilOk || !dhtOk;
  bool red   = pumpOn || (soilOk && soilPct <= cfg.soilDry) || fault;
  bool green = !red && soilOk && soilPct > cfg.soilDry;
  // blink red on fault so it is distinguishable from "watering"
  if (fault && !pumpOn) red = (millis() / 400) % 2;
  digitalWrite(PIN_LED_RED,   red   ? HIGH : LOW);
  digitalWrite(PIN_LED_GREEN, green ? HIGH : LOW);
}

/*==============================  PERSISTENCE  ================================*/
void saveSettings() {
  prefs.begin("irrig", false);
  prefs.putBytes("cfg", &cfg, sizeof(cfg));
  prefs.end();
  Serial.println("[NVS] settings saved");
}
void loadSettings() {
  prefs.begin("irrig", true);
  if (prefs.getBytesLength("cfg") == sizeof(cfg)) prefs.getBytes("cfg", &cfg, sizeof(cfg));
  prefs.end();
}

/*==============================  LCD UI  =====================================*/
void printPadded(uint8_t row, const String &s) {
  String line = s;
  while (line.length() < 20) line += ' ';
  if (line.length() > 20) line = line.substring(0, 20);
  lcd.setCursor(0, row);
  lcd.print(line);
}

const char* menuName(int i) {
  switch (i) {
    case 0: return "Mode";
    case 1: return "Dry thr %";
    case 2: return "Wet thr %";
    case 3: return "Max run s";
    case 4: return "Cooldown s";
    case 5: return "Night only";
    default:return "Save & Exit";
  }
}
String menuValue(int i) {
  switch (i) {
    case 0: return cfg.mode == MODE_AUTO ? "AUTO" : "MANUAL";
    case 1: return String(cfg.soilDry);
    case 2: return String(cfg.soilWet);
    case 3: return String(cfg.pumpMaxRun);
    case 4: return String(cfg.pumpCooldown);
    case 5: return cfg.nightOnly ? "YES" : "NO";
    default:return "";
  }
}
void editValue(int i, int dir) {          // dir = +1 / -1
  switch (i) {
    case 0: cfg.mode = (cfg.mode == MODE_AUTO) ? MODE_MANUAL : MODE_AUTO; break;
    case 1: cfg.soilDry      = clampi(cfg.soilDry + dir, 5, cfg.soilWet - 5); break;
    case 2: cfg.soilWet      = clampi(cfg.soilWet + dir, cfg.soilDry + 5, 95); break;
    case 3: cfg.pumpMaxRun   = clampi(cfg.pumpMaxRun + dir * 5, 5, 600); break;
    case 4: cfg.pumpCooldown = clampi(cfg.pumpCooldown + dir * 5, 0, 3600); break;
    case 5: cfg.nightOnly    = !cfg.nightOnly; break;
  }
}

void renderHome() {
  char l0[21], l1[21], l2[21], l3[21];
  snprintf(l0, sizeof(l0), "Soil:%3d%%  Pump:%-3s", soilOk ? soilPct : 0, pumpOn ? "ON" : "OFF");
  if (dhtOk) snprintf(l1, sizeof(l1), "T:%4.1fC  H:%3.0f%%", tempC, humid);
  else       snprintf(l1, sizeof(l1), "T: --.-C  H: --%%");
  snprintf(l2, sizeof(l2), "Light:%4d lux", lightLux);
  snprintf(l3, sizeof(l3), "%-6s Dry:%2d Wet:%2d",
           cfg.mode == MODE_AUTO ? "AUTO" : "MANUAL", cfg.soilDry, cfg.soilWet);
  printPadded(0, l0); printPadded(1, l1); printPadded(2, l2); printPadded(3, l3);
}

void renderMenu() {
  printPadded(0, "== SETTINGS ==");
  // show selected item + neighbours
  for (int row = 1; row <= 3; row++) {
    int i = menuIdx - 1 + (row - 1);
    if (i < 0 || i >= MENU_COUNT) { printPadded(row, ""); continue; }
    String s = String(i == menuIdx ? ">" : " ") + menuName(i);
    String v = menuValue(i);
    if (v.length()) { while ((s.length() + v.length()) < 20) s += ' '; s += v; }
    printPadded(row, s);
  }
}

void renderEdit() {
  printPadded(0, "== EDIT ==");
  printPadded(1, String(menuName(menuIdx)));
  printPadded(2, String("Value: ") + menuValue(menuIdx));
  printPadded(3, "MENU- SET+  MAN=ok");
}

void renderUI() {
  if (uiState == UI_HOME) renderHome();
  else if (uiState == UI_MENU) renderMenu();
  else renderEdit();
}

/*==============================  BUTTON HANDLING  ============================*/
void handleButtons() {
  bool man = pressed(bMan);
  bool men = pressed(bMen);
  bool set = pressed(bSet);
  if (!man && !men && !set) return;

  if (uiState == UI_HOME) {
    if (man) {                       // MANUAL: toggle pump + force manual mode
      cfg.mode = MODE_MANUAL;
      manualPump = !manualPump;
    }
    if (men) { uiState = UI_MENU; menuIdx = 0; }      // enter settings list
    if (set) { uiState = UI_MENU; menuIdx = 0; }
  }
  else if (uiState == UI_MENU) {
    if (men) menuIdx = (menuIdx + 1) % MENU_COUNT;    // next item
    if (set) {                                        // select
      if (menuIdx == MENU_COUNT - 1) {                // "Save & Exit"
        saveSettings(); uiState = UI_HOME;
      } else uiState = UI_EDIT;
    }
    if (man) { uiState = UI_HOME; }                   // back (without saving)
  }
  else { /* UI_EDIT */
    if (men) editValue(menuIdx, -1);
    if (set) editValue(menuIdx, +1);
    if (man) uiState = UI_MENU;                       // confirm -> back to list
  }
  lcd.clear();
}

/*==============================  SETUP / LOOP  ===============================*/
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[BOOT] Smart Irrigation System");

  pinMode(PIN_RELAY_PUMP, OUTPUT); digitalWrite(PIN_RELAY_PUMP, RLY_OFF);
  pinMode(PIN_RELAY_AUX,  OUTPUT); digitalWrite(PIN_RELAY_AUX,  RLY_OFF);
  pinMode(PIN_LED_GREEN,  OUTPUT);
  pinMode(PIN_LED_RED,    OUTPUT);
  pinMode(PIN_BTN_MANUAL, INPUT_PULLUP);
  pinMode(PIN_BTN_MENU,   INPUT_PULLUP);
  pinMode(PIN_BTN_SET,    INPUT_PULLUP);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);     // full 0..3.3V input range

  loadSettings();
  dht.begin();

  Wire.begin(PIN_SDA, PIN_SCL);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  printPadded(0, "SMART IRRIGATION");
  printPadded(1, "  SYSTEM  v1.0");
  printPadded(2, "starting...");
  delay(1500);
  lcd.clear();

  // prime sensors
  readSoil(); readLight(); readDht();
}

void loop() {
  uint32_t now = millis();

  if (now - tSoil >= SOIL_PERIOD) { tSoil = now; readSoil(); readLight(); }
  if (now - tDht  >= DHT_PERIOD)  { tDht  = now; readDht(); }

  handleButtons();
  updateControl();
  updateLeds();

  if (now - tUi >= UI_PERIOD) {
    tUi = now;
    renderUI();
    Serial.printf("soil=%d%%(raw%d) T=%.1f H=%.0f light=%d%% pump=%d mode=%s\n",
                  soilPct, soilRaw, tempC, humid, lightPct, pumpOn,
                  cfg.mode == MODE_AUTO ? "AUTO" : "MAN");
  }
}
