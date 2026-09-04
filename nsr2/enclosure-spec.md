# NSR-2 enclosure specification — 3D printed

The faceplate **is** the IO PCB (deep-red soldermask + cream silk), so
the printed parts are only the body: a bottom tray with walls, standoffs
at two levels, and the rear-edge cutouts. Two printed pieces total.

## Construction

- **Tray** (one print): floor + four walls + standoffs. M3 heat-set
  inserts for the IO faceplate lip and the brain-board standoffs.
- **Lid** = IO faceplate PCB, dropped into the tray lip and secured with
  8× M3 (corners + offset mid pairs at x30/x150, matching the board's
  mounting holes).
- No separate top bezel: the PCB edge-to-wall gap is 1.5 mm all around;
  the tray lip overlaps the faceplate edge by 2 mm.

## Dimensions

| | mm |
|---|---|
| IO faceplate (lid) | 180 × 210 × 1.6 |
| Internal floor | 183 × 213 (1.5 mm clearance) |
| Wall thickness | 2.4 (3 perimeters @ 0.8) |
| External W × D | ≈ 188 × 218 |
| External height | ≈ 27–28 |

## Height stack-up (from the floor up)

| Level | Contents | Height |
|---|---|---|
| 0 | floor (1.6) | 1.6 |
| 1 | battery pouch, in side floor pockets (see placement) | ~8–10 |
| 2 | brain board: PCB 1.6 + 2×20 header 8.5 + Pi Zero ~3 + Audio Injector ~3 | ≈ 18 |
| 2 | Pico assembly under the IO top-right: header 8.5 + Pico ≈ 10 | ≈ 10 |
| 2 | EC11 bodies under the encoder row | ≈ 10.5 |
| 2 | Choc sockets + LEDs under the grid zone | ≈ 4.5 |
| 3 | IO faceplate lip | +2 |

Max under-faceplate stack ≈ 18 mm (brain) + clearance → internal ~20 mm
at the lid level → external ≈ 27–28 mm total. The instrument stays a
flat slab — no wedge in rev A (a rear wedge/stand is a print variant,
same tray, taller rear walls).

## Floor placement zones (top view, panel coordinates)

- **Brain board**: x 40–140, y 0–85 — under the screen/soft-key/encoder
  zone, on 18 mm standoffs, screen header facing up into the faceplate
  window.
- **Battery pouch (2000–3000 mAh, ~34 × 60 × 8)**: side floor pocket,
  x 0–32 or x 148–180 strip beside the encoder/transport rows — clear
  of EC11 bodies (which occupy x 34–146 under y ≈ 95–110), the brain
  stack, and the grid sockets (grid zone under-IO is only ~4.5 mm).
- **Pico**: bottom face of the IO board, top-right; its USB cable runs
  ~100 mm internally down to the brain board's USB-A receptacle.
- **Mic capsule**: glued behind the faceplate's ⌀3 mic hole (90, 5.5),
  foam ring, pigtail to brain J5.

## Rear-wall cutouts (y = 0 edge)

| Cutout | Size | For |
|---|---|---|
| DIN-5 MIDI IN + OUT | 2× ⌀16.5, spaced per brain J2/J3 | DIN sockets, flush |
| Power switch | slot 7 × 13 | brain SW1 actuator |
| Charge port | micro-USB slot 8 × 4 | PowerBoost input |
| SD card slot | slot 13 × 3, positioned over the Pi Zero's µSD edge | re-imaging without opening the box |
| BOOTSEL / RUN | ⌀3.5 hole top edge over Pico SW1 | firmware recovery (NSR-1 convention) |

No front or side cutouts — the USB link is internal; line out/in live on
the Audio Injector's rear 3.5 mm jacks, which face the rear wall: add a
double 3.5 mm cutout aligned with the codec jack row (verify Y position
against the actual Audio Injector Zero jack placement at layout time).

## Fastening & print notes

- M3 heat-set inserts (brass): 8× lid lip, 4× brain standoffs.
- Material: **black PETG** (contrast with the red faceplate) or
  `--red-dark` PETG for a monolith — aesthetic call.
- 0.2 mm layers, 3 perimeters, 15% gyroid; standoffs printed solid.
- Tolerances: holes +0.2 mm, slots +0.3 mm, lip fit test piece first.
- Feet: 4× adhesive silicone ⌀10 × 2 (or printed nubs).

## Open items

1. **Audio Injector Zero exact jack positions** — needed before the rear
   3.5 mm cutout is finalized (measure the real board).
2. **PowerBoost orientation** — its micro-USB charge port must align with
   the rear cutout; pick the J6 header rotation at layout time.
3. Wedge/stand variant — deferred (rev A is a flat slab).
4. Speaker? None — headphone/line only, as NSR-1 (battery budget).
