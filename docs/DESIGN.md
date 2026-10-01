# LFP-8 design notes

LFP-8 is an 8-channel charger, discharger and internal-resistance tester for
LiFePO4 32140/33140 cells (3.2 V, ~15 Ah). Five identical boards test **40 cells
at once**. Each board runs on one 5 V supply and reports over Wi-Fi through its
ESP32-C3.

```
            +5V bus (5.20 V, ≤ 8.5 A)                       ESP32-C3 ── Wi-Fi / web UI
                │                                              │ SPI-like      │ I²C
   ┌────────────┼──────── channel k (×8) ─────────────┐   4×74HC595 (32 bit)  ADS1115
   │  Q1 P-FET ─ D1 SS34 ─ R6 1.2 Ω ─┬─ BP ─ PTC ─ B+F │   CHG/DIS/LED ×8     ▲ ▲
   │  (linear CC/CV pass)            │                 │   mux select, ISET   │ │
   │       ▲ CC amp ◄── R6 drop      ├─ 4×11 Ω ─ Q4 ─ GND  (discharge 1.07 A) │ │
   │       ▲ CV amp ◄── Kelvin B+S/B−S                 │                      │ │
   │  OV latch, UV backstop, reverse sentinel          │   5× CD4051 ─────────┘ │
   │  B− ─ Q5 ┐ back-to-back ┌ Q8 ─ 0.1 Ω shunt ─ GND  │   (B+S, B−S, SH+, SH−, NTC)
   └──────────┴──────────────┴─────────────────────────┘
        SAFE_EN = watchdog ∧ rail UV ∧ rail OV ∧ board OT ∧ E-STOP  → isolates every cell
```

## Why this architecture

* **Cheapest possible BOM that still measures properly.** Every channel is
  built from JLCPCB *basic* parts (LM324, SOT-23 MOSFETs, BJTs, 0603 passives).
  Only six part types are *extended*: ESP32-C3 module, ADS1115, the 0.1 Ω shunt,
  the 1.2 Ω and 11 Ω 2512 resistors and the PTC fuse. There is no charger IC per
  channel. A discrete linear CC/CV loop costs about $0.15 per channel and has
  no inductor, so there is no switching noise in the Kelvin measurements.
* **One 5 V supply.** An LFP cell charges to 3.6 V, so a linear charger from
  5.2 V wastes at most (5.2 − 3.2 V) × 1 A ≈ 2 W per channel. It is spread over
  the pass FET, the Schottky diode and the 1.2 Ω ballast. The ballast also
  senses the CC current, so the FET only dissipates ≈ 0.3 W. Five boards need a
  common 5 V / 50 A supply; a 5 V / 10 A brick per board also works.
* **Resistive discharge.** 4 × 11 Ω 2512 resistors (2.75 Ω) switched by an
  AO3400A draw 1.07 A at the 3.29 V LFP plateau (≈ 0.07 C for a 15 Ah cell).
  That is slow but adequate for a capacity test and needs no regulation. The
  ADS1115 measures the real current through the Kelvin shunt, so the
  coulomb counter does not care that the current falls slightly with voltage.
* **True 4-wire (Kelvin) measurement.** Each cell has force and sense wires at
  both terminals (6-pin JST-XH: B+F, B+S, B−S, B−F, NTC, GND). The ADS1115
  measures the cell differentially between the sense wires through the
  multiplexers, and the current differentially across the Kelvin pads of the
  shunt. The CV loop and every protection comparator also use the sense wires,
  so contact resistance (often 30–100 mΩ with pogo pins) affects neither the
  charge voltage nor the IR result. If a sense wire breaks, a 100 Ω fall-back
  resistor ties it to its force wire, so the channel degrades gracefully.

## Channel circuit (`hardware/gen/lfp8_circuit.py`, `build_channel`)

| Block | Parts | Function |
|---|---|---|
| Pass device | Q1 AO3401A, Q2 MMBT3904 (1 k emitter degeneration), R3 10 k, C9 1 nF | Q2 sinks a gate current set by the error amps, so Q1 is a voltage-controlled current source |
| Error amps (LM324 A/B) | CC amp: differential integrator across the 1.2 Ω ballast vs VS (balanced, 47 nF / 1 k). CV amp: differential Kelvin B+S − B−S vs 0.38 × VREF (10 nF / 10 k) | The lower of the two outputs wins (diode min-select D2/D3 into the Q2 base node NB) |
| Charge enable | R7/R8 from `CHG_EN_k`, D7/D8 anti-windup into CHG_EN | Soft start; the integrators start from zero on every enable |
| Interlock | Q3 2N7002 on NB driven by the discharge driver output | The charger cannot run while the load is on |
| Discharge | LM324 C: drives Q4 when `DIS_EN_k` is high **and** Kelvin V_cell > UV threshold (positive feedback gives 2.22 V trip / 2.59 V re-arm) | Hardware backstop: a stuck firmware can never over-discharge a cell |
| OV latch | LM324 D: differential comparator with heavy positive feedback | Trips at 3.82 V; pulls GEN (gate enable) low, which isolates the cell and kills the charger drive. Releases when the cell is removed or SAFE_EN drops |
| Isolation | Q5 + Q8 AO3400A back-to-back in the B− path, gates driven from +5V via Q9 (PNP) and referenced to their common source with R53 22 k | Blocks current in both directions when off, including when the board is unpowered and with a reversed cell |
| Reverse sentinel | Q6 NPN, Q7 PNP + Q11 NPN, 150 k / 91 k divider | Pulls GEN low when B− is more than ~1.6 V above B+ in either power state |
| Measurement | 0.1 Ω Kelvin shunt R50, 1 k series resistors + 10 nF into the CD4051 muxes, NTC pull-up | ADS1115 AIN0−AIN1 (cell) and AIN2−AIN3 (current) |
| Fuse | 2 A hold PTC in B+ force | Board-side short or a stuck pass FET into a short cell |

All the numbers in the table below come from ngspice (behavioural LM324 /
LM393 / TL431, VDMOS FETs, an LFP cell model with OCV curve and RC polarisation),
see `sim/` and [TEST_REPORT.md](TEST_REPORT.md).

| Parameter | Value |
|---|---|
| CC charge current (ISET 4, 5.20 V rail) | 0.97–1.00 A (passive ceiling ≈ (V_IN − 0.38 − V_cell)/1.5 Ω) |
| ISET codes 0–7 | 0, 0.26, 0.52, 0.77, 1.0, 1.0, 1.0, 1.0 A at 3.3 V cell |
| CV voltage | 3.576 V nominal, 3.51–3.62 V over tolerances (Monte Carlo, 100 runs) |
| OV latch | 3.82 V nominal, 3.78–3.87 V |
| Discharge current | 1.07 A at 3.29 V |
| Discharge UV backstop | 2.22 V under load, no restart below 2.59 V |
| IR measurement error (Kelvin) | +0.6 % (8.13 vs 8.08 mΩ ohmic, 11.60 vs 11.54 mΩ DC) |
| Loop stability | CC phase margin ≥ 90° (fc 26 Hz – 1.3 kHz over 23 corners), CV ≥ 94° |
| Unpowered drain per cell | 81 µA |

## Board-level safety chain (`SAFE_EN`)

Two LM393s form an open-collector wired-AND. SAFE_EN drives the 74HC595 `/OE`
(through Q8 2N7002), so when it is low every control output goes high-Z and is
pulled to its off state. It also pulls every channel's GEN node low, which
isolates every cell.

| Input | Implementation | Trip |
|---|---|---|
| Watchdog | `HEARTBEAT` (ESP32 IO10, ≥ 100 Hz) → 100 nF / BAT54 charge pump → comparator vs VREF divider | 23–33 ms after the toggling stops; ≤ 50 Hz rejected |
| Rail UV | 8.2 k / 10 k divider vs VREF, 1 M hysteresis | 4.48 V |
| Rail OV | 12 k / 10 k divider vs VREF | 5.49 V (also catches a failed VREF) |
| Board over-temperature | diode-connected MMBT3904 (0.601 V @ 25 °C, −1.97 mV/K) vs VREF divider | 70–95 °C |
| E-STOP | SW1 shorts SAFE_EN to GND | 126 µs to isolation |

## Supply

* `+5V` input through a terminal block (J1). An SMBJ6.0A TVS and an SS54
  reverse-polarity crowbar sit next to it. The supply must be current-limited
  or fused (≤ 15 A); a reversed PSU otherwise burns the SS54.
* AMS1117-3.3 for the ESP32. CJ431 2.495 V shunt reference (VREF) for every
  threshold.
* ISET DAC: three 74HC595 outputs into a binary-weighted resistor network
  (10 k / 20 k / 40.2 k / 4.3 k) buffered by an LM358. It scales with the rail,
  which keeps the charger's headroom constant.

## PCB

* 216 × 146 mm, 2 layers, 1.6 mm FR-4, 1 oz copper, JLCPCB standard rules.
  Minimum 0.2 mm clearance, 0.25 mm tracks, 0.3 mm vias.
* Eight 25 mm channel columns. The JST-XH connectors are on the bottom edge,
  with the power stage (pass FET, Schottky, ballast, load bank, isolation FETs,
  shunt) directly above them and the analogue small-signal parts above that.
  The common section (ESP32-C3 with its antenna on the top edge, shift
  registers, multiplexers, ADC, safety chain) runs along the top.
* `+5V` bus: 6.5 mm wide on both layers along the bottom edge, fed from J1 by a
  5 mm wide feed on both layers. Each channel taps it with a 1 mm spur.
* GND: solid plane on B.Cu, plus a GND pour on F.Cu.
* Every 1 A path is pre-routed as a locked 0.8–1.6 mm track (`make_pcb.py`,
  `ch_preroutes`). The load-bank pads sit on solid copper zones for heat
  spreading. Freerouting routes everything else.
* The board needs a fan while discharging. 8 channels × 4.1 W = 33 W of heat
  goes mostly into the 11 Ω resistors. The board stand in `mechanical/` holds
  a fan under the board, and the firmware runs it from the `FAN` header (J2).

## Single source of truth

```
hardware/gen/lfp8_circuit.py ──► sim/spicelib.py    → ngspice benches (sim/tests)
                             ├─► make_sch.py        → hardware/kicad/*.kicad_sch
                             ├─► make_pcb.py        → placement + power pre-routes → DSN
                             │   route_pcb.py       → Freerouting → routed board
                             ├─► verify_netlist.py  → schematic == PCB == DSL (pin by pin)
                             └─► make_fab.py        → Gerbers, drill, BOM, CPL
```

The same Python netlist feeds the simulator, the schematic and the PCB, so the
simulated circuit is, pin for pin, the circuit that gets manufactured.
`verify_netlist.py --pcb` proves it after every change.
