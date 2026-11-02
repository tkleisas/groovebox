# NSR-1 power/battery board — rev A

Status: **rev A schematic and layout.** This board is common to the NSR-1
platform and to future SBC carriers; two BAT_NEG links remain as airwires. It is deliberately separate from the brain carrier.

Source of truth for this board. Platform policy is in
[`../../docs/mission.md`](../../docs/mission.md).

## Scope

The power board owns:

- USB-C charging input;
- 1S Li-ion/LiPo charge management with power path;
- battery protection coordination, fuse, reverse-polarity protection, and NTC;
- 5V/3A-class boost output for the brain carrier;
- fuel gauge and low-battery signalling;
- physical power switch and charge/power status LEDs;
- a replaceable battery connector and service disconnect.

The brain carrier only consumes 5V and reads the low-battery/gauge signals.
The power board does not carry the Pi, display, codec, or panel.

## Architecture

```
USB-C 5V ── ESD/fuse ── charger + power path ── 1S pack
                              │                    │
                              │                    ├── fuel gauge ── I²C
                              │                    ├── NTC
                              │                    └── protection
                              ▼
                        boost 5V/3A ── power switch ── J3 → brain carrier
                              │
                              ├── J4 LOW_BATT
                              └── J5 I²C gauge
```

The charger remains active when the instrument is switched off, so the pack
can charge without powering the boost. The physical switch controls the boost
and the 5V output.

## Board outline and mounting

- Board: **100 × 70 mm**, 4-layer, 1.6 mm, green soldermask, 2 oz copper on
  the outer-layer 5V/battery paths.
- M3 mounting holes (3.2 mm) at `(5,5)`, `(95,5)`, `(5,65)`, `(95,65)`.
- The board fits the NSR-1 power-board bay on four M3 inserts.
- USB-C, power switch, and status LEDs must align with the rear panel or a
  light pipe. Keep the battery and its wiring away from the boost/charger
  hot loop.
- Place the battery connector and NTC connector on the edge nearest the
  battery bay.

## Electrical target

| Parameter | Target |
|---|---|
| Battery | 1S Li-ion/LiPo, 5000 mAh nominal, protected, NTC |
| Battery range | 3.0–4.2 V |
| Charge input | USB-C 5 V, 1.5–3 A |
| Charge current | 1.5 A default; 2 A only with thermal validation |
| 5V output | 5.0 V, 2.5 A continuous, 3 A peak |
| Output ripple | < 100 mV p-p at 2 A |
| Standby | Boost off when the power switch is off; charger may remain active |
| Gauge | 1S fuel gauge, I²C, alert output |
| Protection | Fuse/eFuse, reverse polarity, TVS, NTC, pack protection |

Runtime estimate with a 5000 mAh pack (18.5 Wh, 85% conversion):

- 2.5 W average load: ~6.3 h
- 5 W average load: ~3.1 h
- 7.5 W average load: ~2.1 h

Measure the real Pi + HAT + display + panel load before publishing a runtime
claim. At 5V/2.5A the 1S input is about 4A; the pack must be rated for at
least 1C continuous.

## Candidate parts

These are starting points, not frozen BOM choices. Confirm availability and
thermal performance before layout.

| Ref | Candidate | Role |
|---|---|---|
| U1 | BQ24074-class | 1S charger with power path, standalone (no firmware needed) |
| U2 | TPS61089 | 5V/3A boost from 1S |
| U3 | BQ27441-G1 | 1S fuel gauge, I²C + GPOUT |
| Q1 | P-channel MOSFET | Reverse-polarity / load path |
| F1 | resettable fuse or eFuse | Input/output fault protection |
| D1 | red LED | charging |
| D2 | green LED | 5V output on |
| D3 | TVS | USB-C VBUS ESD |

A future revision may move to an I²C-controlled charger for 2–3 A charging
and richer status reporting. Rev A keeps the charger standalone so the power
board works without the brain.

## Connectors

| Ref | Type | Pinout |
|---|---|---|
| J1 | USB-C receptacle, 16-pin | Charge input only; CC1/CC2 = 5.1 kΩ to GND; VBUS/GND |
| J2 | XT30 | Battery power; keyed, replaceable |
| J3 | XH-2P | 5V output to brain carrier: 1=+5V, 2=GND |
| J4 | XH-2P | LOW_BATT: 1=open-drain alert, 2=GND |
| J5 | XH-4P | Gauge I²C: 1=3V3, 2=GND, 3=SDA, 4=SCL |
| J6 | XH-2P | Battery NTC: 1=NTC, 2=GND |
| SW1 | SPDT slide, THT | Boost/output enable |
| TP1–TP4 | test points | BAT, 5V, GND, SDA/SCL |

J3 and J4/J5 use keyed connectors so the brain carrier cannot be plugged in
backwards. The battery connector is separate from the NTC connector.

## Power path and switching

- USB-C VBUS → fuse/TVS → charger input.
- Charger SYS/OUT → boost input; charger continues to charge the pack while
  the system runs.
- Boost EN → SW1 → off when the instrument is off.
- Battery → protection → charger/gauge/boost.
- Low-battery alert is open-drain and pulled up on the brain carrier.
- Gauge I²C is 3.3V logic; the brain carrier provides the pull-ups or the
  power board fits them once, not both.

## Battery

- Protected 1S 5000 mAh-class pack with NTC and a keyed XT30 lead.
- Do not solder the pack directly to the PCB.
- Provide 2 mm swelling clearance and strain relief in the enclosure.
- Charge temperature window: 0–45 °C; disable charging outside it via NTC.
- Keep the pack away from the boost inductor, charger, and SBC heatsink.
- Publish the exact pack dimensions and connector pinout when selected.

## Bring-up checklist

1. With no battery, USB-C alone powers the charger output; measure 5V at J3
   with SW1 on.
2. With a battery, charge current is within the configured limit; NTC
   disables charging when heated/cooled outside the window.
3. Boost output holds 5V at 0.5/1.5/2.5A; ripple and temperature are
   acceptable.
4. SW1 off: 5V output is off, charger still charges.
5. Fuel gauge appears at its I²C address; SOC tracks discharge and charge.
6. LOW_BATT asserts before the pack protection cuts off.
7. Reverse battery connection is blocked; fuse/TVS survives a USB ESD test.
8. Thermal soak at 2.5A output with the enclosure closed; no component
   exceeds its rating and the battery stays within its temperature limit.

## Open items

- Freeze charger/boost/gauge part numbers and availability.
- Decide whether rev A needs USB-C PD negotiation or 5.1k sink is enough.
- Select the exact 5000 mAh pack and its dimensions/connector.
- Decide whether the power switch is PCB-mounted or panel-mounted.
- Add the power board and battery to the NSR-1 fit check and EMD assembly.
