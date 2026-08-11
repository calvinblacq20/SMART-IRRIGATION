"""
Generate a clean B&W wiring / connection diagram for the Smart Irrigation firmware.
ESP8266 (NodeMCU) in the centre, components around it, signal wires labelled with the pin.
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

fig, ax = plt.subplots(figsize=(13.5, 8.0))
ax.set_xlim(0, 15); ax.set_ylim(0, 10); ax.axis("off")


def box(x, y, w, h, label, fc=BOX, fs=10, bold=True):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.02,rounding_size=0.08",
                                linewidth=1.4, edgecolor=INK, facecolor=fc))
    ax.text(x + w / 2, y + h / 2, label, ha="center", va="center",
            fontsize=fs, fontweight="bold" if bold else "normal", color=INK)


def wire(p1, p2, label="", lx=None, ly=None, fs=9):
    (x1, y1), (x2, y2) = p1, p2
    ax.plot([x1, x2], [y1, y2], color=INK, linewidth=1.2, zorder=1)
    ax.plot([x1, x2], [y1, y2], "o", color=INK, markersize=2.6, zorder=2)
    if label:
        ax.text(lx if lx is not None else (x1 + x2) / 2,
                ly if ly is not None else (y1 + y2) / 2 + 0.12, label,
                ha="center", va="bottom", fontsize=fs, color=INK,
                bbox=dict(boxstyle="round,pad=0.12", fc="white", ec="none"))


# ---------------- ESP8266 / NodeMCU ----------------
EX, EW = 6.4, 2.6
ax.add_patch(Rectangle((EX, 2.4), EW, 5.6, linewidth=1.8, edgecolor=INK, facecolor=MCU))
ax.text(EX + EW / 2, 7.6, "ESP8266", ha="center", va="center", fontsize=13, fontweight="bold", color=INK)
ax.text(EX + EW / 2, 7.2, "NodeMCU", ha="center", va="center", fontsize=8.5, color=INK)

Lx, Rx = EX, EX + EW
Lpin = {"A0": 6.3, "D2 (GPIO4)": 5.0}     # inputs on the left
Rpin = {"D6 (GPIO12)": 5.5}                # output on the right
for name, y in Lpin.items():
    ax.plot([Lx - 0.18, Lx], [y, y], color=INK, lw=1.1)
    ax.text(Lx + 0.12, y, name, ha="left", va="center", fontsize=8, color=INK)
for name, y in Rpin.items():
    ax.plot([Rx, Rx + 0.18], [y, y], color=INK, lw=1.1)
    ax.text(Rx - 0.12, y, name, ha="right", va="center", fontsize=8, color=INK)

# ---------------- LEFT components (inputs) ----------------
box(0.6, 6.0, 2.6, 0.7, "Soil Moisture")
wire((3.2, 6.35), (Lx - 0.18, Lpin["A0"]), "AOUT -> A0", lx=4.7, ly=6.45)

box(0.6, 4.65, 2.6, 0.7, "DHT22")
wire((3.2, 5.0), (Lx - 0.18, Lpin["D2 (GPIO4)"]), "DATA -> D2", lx=4.7, ly=5.1)

# ---------------- RIGHT components (output) ----------------
box(11.4, 5.15, 2.2, 0.9, "Relay Module")
wire((Rx + 0.18, Rpin["D6 (GPIO12)"]), (11.4, 5.6), "IN -> D6", lx=10.4, ly=5.7)
box(13.8, 5.2, 1.0, 0.8, "Pump", fc="white")
ax.annotate("", xy=(13.8, 5.6), xytext=(13.6, 5.6),
            arrowprops=dict(arrowstyle="->", color=INK, lw=1.2))
ax.text(13.62, 6.12, "COM / NO", fontsize=7.5, color=INK, ha="center")

# ---------------- power / notes ----------------
ax.add_patch(FancyBboxPatch((0.5, 0.5), 14.0, 1.35, boxstyle="round,pad=0.05,rounding_size=0.08",
                            linewidth=1.2, edgecolor=INK, facecolor="#f6f6f6"))
ax.text(0.8, 1.55, "POWER & COMMON CONNECTIONS", fontsize=9.5, fontweight="bold", color=INK, va="center")
ax.text(0.8, 1.18, "Sensor VCC -> 3V3    -    all GND -> common GND    -    Relay VCC -> 5V (VIN)    "
        "-    Relay is active-HIGH (RELAY_ON = HIGH)", fontsize=8.6, color=INK, va="center")
ax.text(0.8, 0.82, "Pump powered from its own supply through the relay COM/NO contacts (add a "
        "flyback diode across an inductive pump).", fontsize=8.6, color=INK, va="center")

ax.text(7.5, 9.4, "SMART IRRIGATION SYSTEM - ESP8266 WIRING / CONNECTION DIAGRAM",
        ha="center", va="center", fontsize=13, fontweight="bold", color=INK)

fig.tight_layout()
out = os.path.join(HERE, "wiring_diagram.png")
fig.savefig(out, dpi=170, bbox_inches="tight", facecolor="white")
print("saved", out)
