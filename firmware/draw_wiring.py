"""
Generate a clean B&W wiring / connection diagram for the Smart Irrigation firmware.
ESP32 in the centre, components around it, signal wires labelled with the GPIO pin.
Output: wiring_diagram.png
"""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Rectangle

HERE = os.path.dirname(os.path.abspath(__file__))
INK = "#111111"
BOX = "#ececec"
MCU = "#d5d5d5"

fig, ax = plt.subplots(figsize=(13.5, 8.6))
ax.set_xlim(0, 15); ax.set_ylim(0, 10); ax.axis("off")


def box(x, y, w, h, label, fc=BOX, fs=10, bold=True):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.02,rounding_size=0.08",
                                linewidth=1.4, edgecolor=INK, facecolor=fc))
    ax.text(x + w / 2, y + h / 2, label, ha="center", va="center",
            fontsize=fs, fontweight="bold" if bold else "normal", color=INK)


def wire(p1, p2, label="", lx=None, ly=None, fs=8.5):
    (x1, y1), (x2, y2) = p1, p2
    ax.plot([x1, x2], [y1, y2], color=INK, linewidth=1.1, zorder=1)
    ax.plot([x1, x2], [y1, y2], "o", color=INK, markersize=2.5, zorder=2)
    if label:
        ax.text(lx if lx is not None else (x1 + x2) / 2,
                ly if ly is not None else (y1 + y2) / 2 + 0.12, label,
                ha="center", va="bottom", fontsize=fs, color=INK,
                bbox=dict(boxstyle="round,pad=0.1", fc="white", ec="none"))


# ---------------- ESP32 ----------------
EX, EW = 6.2, 2.6
ax.add_patch(Rectangle((EX, 1.4), EW, 7.2, linewidth=1.8, edgecolor=INK, facecolor=MCU))
ax.text(EX + EW / 2, 8.25, "ESP32", ha="center", va="center", fontsize=13, fontweight="bold", color=INK)
ax.text(EX + EW / 2, 7.85, "DEV MODULE", ha="center", va="center", fontsize=8.5, color=INK)

# left pins (x at EX) and right pins (x at EX+EW)
Lx, Rx = EX, EX + EW
Lpin = {  # gpio : y
    "GPIO4": 7.15, "GPIO34": 6.55, "GPIO35": 5.95,
    "GPIO25": 4.75, "GPIO26": 4.15, "GPIO27": 3.55,
}
Rpin = {
    "GPIO18": 7.15, "GPIO19": 6.6, "GPIO22": 5.7, "GPIO23": 5.15,
    "GPIO17": 4.1, "GPIO16": 3.5,
}
for name, y in Lpin.items():
    ax.plot([Lx - 0.18, Lx], [y, y], color=INK, lw=1.1)
    ax.text(Lx + 0.12, y, name, ha="left", va="center", fontsize=8, color=INK)
for name, y in Rpin.items():
    ax.plot([Rx, Rx + 0.18], [y, y], color=INK, lw=1.1)
    ax.text(Rx - 0.12, y, name, ha="right", va="center", fontsize=8, color=INK)

# ---------------- LEFT components (inputs) ----------------
box(0.5, 6.85, 2.2, 0.7, "DHT11")
wire((2.7, 7.2), (Lx - 0.18, Lpin["GPIO4"]), "DATA", lx=4.4, ly=7.3)

box(0.5, 6.2, 2.2, 0.55, "Soil Moisture")
wire((2.7, 6.5), (Lx - 0.18, Lpin["GPIO34"]), "A0", lx=4.4, ly=6.62)

box(0.5, 5.6, 2.2, 0.5, "LDR Module")
wire((2.7, 5.85), (Lx - 0.18, Lpin["GPIO35"]), "OUT", lx=4.4, ly=5.98)

box(0.5, 4.5, 2.2, 0.55, "Btn MANUAL")
wire((2.7, 4.77), (Lx - 0.18, Lpin["GPIO25"]), "", )
box(0.5, 3.9, 2.2, 0.5, "Btn MENU")
wire((2.7, 4.15), (Lx - 0.18, Lpin["GPIO26"]), "")
box(0.5, 3.3, 2.2, 0.5, "Btn SETTINGS")
wire((2.7, 3.55), (Lx - 0.18, Lpin["GPIO27"]), "")
ax.text(3.4, 4.05, "to GND", fontsize=7.5, style="italic", color=INK, rotation=0)

# ---------------- RIGHT components (outputs) ----------------
box(11.6, 6.55, 2.6, 0.9, "LCD 20x4 (I2C)")
wire((Rx + 0.18, Rpin["GPIO18"]), (11.6, 7.15), "SDA", lx=10.4, ly=7.25)
wire((Rx + 0.18, Rpin["GPIO19"]), (11.6, 6.8), "SCL", lx=10.4, ly=6.55)

box(11.6, 5.0, 2.0, 0.9, "Relay 2-ch")
wire((Rx + 0.18, Rpin["GPIO22"]), (11.6, 5.65), "IN1", lx=10.4, ly=5.72)
wire((Rx + 0.18, Rpin["GPIO23"]), (11.6, 5.25), "IN2", lx=10.4, ly=5.0)
box(13.9, 5.05, 1.0, 0.8, "Pump", fc="white")
ax.annotate("", xy=(13.9, 5.45), xytext=(13.6, 5.45),
            arrowprops=dict(arrowstyle="->", color=INK, lw=1.2))
ax.text(13.75, 5.95, "COM/NO", fontsize=7, color=INK, ha="center")

box(11.6, 3.75, 2.0, 0.5, "LED Green", fc="white")
wire((Rx + 0.18, Rpin["GPIO17"]), (11.6, 4.0), "220Ω", lx=10.4, ly=4.1)
box(11.6, 3.15, 2.0, 0.5, "LED Red", fc="white")
wire((Rx + 0.18, Rpin["GPIO16"]), (11.6, 3.4), "220Ω", lx=10.4, ly=3.25)

# ---------------- power / notes ----------------
ax.add_patch(FancyBboxPatch((0.5, 0.35), 14.0, 1.15, boxstyle="round,pad=0.05,rounding_size=0.08",
                            linewidth=1.2, edgecolor=INK, facecolor="#f6f6f6"))
ax.text(0.75, 1.18, "POWER & COMMON CONNECTIONS", fontsize=9, fontweight="bold", color=INK, va="center")
notes = ("Sensor VCC → 3V3   •   all GND → common GND   •   Relay VCC → 5V (VIN)   "
         "•   button other leg → GND (internal pull-ups)")
notes2 = ("LEDs: anode → GPIO via 220Ω, cathode → GND   •   "
          "Pump powered from battery through relay COM/NO (add flyback diode)")
ax.text(0.75, 0.85, notes, fontsize=8.4, color=INK, va="center")
ax.text(0.75, 0.55, notes2, fontsize=8.4, color=INK, va="center")

ax.text(7.5, 9.5, "SMART IRRIGATION SYSTEM — ESP32 WIRING / CONNECTION DIAGRAM",
        ha="center", va="center", fontsize=13, fontweight="bold", color=INK)

fig.tight_layout()
out = os.path.join(HERE, "wiring_diagram.png")
fig.savefig(out, dpi=170, bbox_inches="tight", facecolor="white")
print("saved", out)
