# Mission, goals, and platform strategy

## Mission

Groovebox is an open hardware and software instrument platform designed to be
serviced indefinitely. If a subsystem disappears, an equivalent subsystem must
be substitutable without discarding the instrument, its control surface, its
enclosure, or its software.

The source code for the brain application and the panel/MIDI controller is the
primary defence against digital decay. Published KiCad sources, firmware,
enclosure CAD, test procedures, and a documented substitution path are the
defence against hardware decay.

## Goals

1. **Keep the instrument, not the module.** The panel, power system, enclosure,
   and storage format should outlive any one single-board computer (SBC).
2. **Use separate brain carriers per SBC.** Each supported SBC gets its own
   carrier PCB and its own validated OS/device-tree configuration. There is no
   single "universal" brain PCB.
3. **Make the common interfaces stable.** Panel USB-MIDI, power input, battery
   interface, mounting frame, and enclosure openings are platform contracts.
4. **Make service normal.** Use connectors instead of soldered modules,
   replaceable batteries, standard fasteners, test points, and a service hatch.
5. **Publish the truth.** Document what is tested, what is experimental, and
   what is only mechanically provisioned. Never call a board "supported"
   without a passed bring-up checklist.
6. **Keep the software portable.** The emulator, HAL, panel protocol, and
   engine stay independent of any one SBC vendor.
7. **Target NSR-1 first.** Its 400 x 170 mm panel and 35 mm case give the
   brain carrier, power board, battery, and service access room to exist
   without compromising the control surface. NSR-2 is deferred.

## Architecture

```
                    SBC-specific brain carrier
                    ┌─────────────────────────┐
                    │ Pi Zero 2W (first)      │
                    │ Audio Injector Zero     │
                    │ display / DIN MIDI      │
                    └───────┬─────────┬───────┘
                            │ USB     │ 5V / power signals
                            ▼         ▼
                    Panel Pico     Power/battery board
                    control +      1S Li-ion, charger,
                    keyboard       gauge, 5V boost
```

The **brain carrier** is deliberately SBC-specific. It owns the SBC socket,
the SBC's power input, its USB host wiring, its audio path, its display
header, and its mounting holes. The **power board**, **panel**, and
**enclosure** are common platform elements.

This split is what makes substitution practical. A new SBC is a new carrier
plus a new validated software profile, not a redesign of the whole instrument.

## Support matrix

| Platform | Carrier | Status | What is claimed |
|---|---|---|---|
| Raspberry Pi Zero 2W | `hardware/brain` (rev A) | **Tier 1, schematic complete** | Reference platform; full feature validation target |
| Radxa Zero 3W | future carrier | Tier 3, not started | Mechanical study only; not supported |
| Orange Pi Zero 3W | future carrier | Tier 3, not started | Mechanical study only; not supported |

Support tiers:

- **Tier 1:** reference platform. Boot, display, audio, DIN MIDI, panel USB,
  power management, thermals, and battery runtime have all been tested.
- **Tier 2:** working platform with documented limitations. Core instrument
  features work, but one or more subsystems have known caveats.
- **Tier 3:** mechanical/electrical study only. No software or support claim.

The Pi Zero 2W is first because it has the most mature software path and the
existing Audio Injector Zero/ILI9488 recipes. Radxa Zero 3W is the preferred
second target because it is 65 x 30 mm, has a 40-pin header, and exposes
UART/SPI/I2S on useful pins. Orange Pi Zero 3W is 65 x 32 mm, uses 3.0 mm
mounting holes, and is a very new A733 platform; it remains a future option.

## Paths considered

### A. One universal brain PCB for every SBC

Rejected for now. The 40-pin header is physically similar but not a software
or electrical standard. USB host availability, audio codec support, display
overlays, power input, mounting holes, and thermal limits differ. A universal
board would need adapters, a USB hub, and per-SBC software anyway, while
creating a false impression of support.

### B. Separate carrier per SBC on common platform interfaces

**Chosen.** Each carrier is small, understandable, and testable. The panel,
power board, battery, enclosure, and software platform remain common. A failed
or unavailable SBC only invalidates its carrier.

### C. Keep the Pi Zero 2W forever

Rejected. It fails the mission as soon as the Pi becomes unavailable. The Pi
remains the reference, not the permanent dependency.

### D. Move audio and display to class-compliant USB peripherals

Deferred. This could make the brain almost SBC-agnostic, but it consumes USB
ports, adds a hub, and puts audio latency and display bandwidth on the USB
link. Measure before committing.

## NSR-1 first

NSR-1 is the active hardware target:

- The 400 x 170 mm panel and 35 mm case have room for a separate brain
  carrier, power board, and 5000 mAh-class battery.
- The control and keyboard boards are already routed and verified.
- The larger surface makes service access and connector routing practical.
- NSR-2 is retained as a reference design but is not the current build.

The NSR-1 brain carrier is a Raspberry Pi Zero 2W carrier first. Future Radxa
and Orange Pi carriers will reuse the same power/panel/enclosure contracts but
will be separate boards and separate validation efforts.

## Power and battery

The power system is a separate board:

- 1S Li-ion/LiPo, 5000 mAh target, replaceable and protected.
- USB-C charging input with power path.
- 5V/3A-class boost output for the brain carrier.
- Fuel gauge, low-battery signal, charge status, NTC temperature input, and a
  physical power switch (soft latch is deferred).
- Keyed battery connector and a service disconnect.

The battery is not soldered and is not buried under the SBC. The enclosure
provides a battery bay with swelling clearance and a service hatch.

## Enclosure and mounting

The enclosure is a common frame with replaceable interface plates:

- A **brain adapter plate** carries the SBC-specific mounting pattern. The
  enclosure itself provides one sturdy M3 insert pattern, not a field of
  one-off holes.
- A **rear I/O plate** is swappable for the SBC's USB/HDMI/SD layout.
- A **power-board bay** and **battery bay** are separate from the brain.
- A bottom service hatch gives access to the battery, SBC, and carrier without
  removing the faceplate.
- Thermal clearance is reserved for the Radxa/Orange class of SBC and for the
  Audio Injector stack on the Pi.

## Serviceability rules

- Module-level connectors, not soldered modules.
- Standard fasteners and heat-set inserts.
- Protected, replaceable battery with a documented connector.
- Test points for power rails, battery, charge, and key buses.
- Published BOM with acceptable substitutes for non-critical parts.
- Reproducible software builds, pinned dependencies, and an emulator path.
- A written bring-up checklist and a compatibility matrix.
- Hardware licence must be reconciled before publishing a "ready to build"
  release: repository metadata currently mixes MIT and GPL-3.0 references.

## Open decisions

1. Audio path for non-Pi carriers: common I2S codec module vs class-compliant
   USB audio.
2. Exact 1S battery form factor: replaceable pouch vs standard 21700 cell.
3. USB-C PD negotiation and maximum charge current.
4. Whether alternate carriers use 5V on the 40-pin header or their own USB-C
   power input.
5. Final NSR-1 rear-cavity layout and service-hatch geometry.
6. Hardware licence and trademark/branding policy.

## Change policy

- A new SBC requires a new carrier, a new OS/device-tree profile, and a passed
  bring-up checklist before it appears as Tier 1 or Tier 2.
- A component substitution must be recorded in the BOM and, if it changes an
  interface, in the relevant board spec.
- The mission and support matrix are updated whenever a platform is added,
  deferred, or retired.

See [`docs/design.md`](design.md) for the NSR-1 electrical design,
[`hardware/README.md`](../hardware/README.md) for board status, and
[`hardware/enclosure-spec.md`](../hardware/enclosure-spec.md) for the
mechanical envelope.
