# Ordering, assembly and bring-up

## 1. Order the boards (JLCPCB)

Files are in `hardware/fab/` (regenerate with `make -C hardware fab`).

1. Upload **`lfp8_gerbers.zip`** at jlcpcb.com → *Order now*.
   * Layers 2, thickness 1.6 mm, 1 oz copper, HASL lead-free (cheapest) or ENIG,
     any solder-mask colour (green is cheapest and fastest).
   * Quantity **5**: one board per 8 cells, so 5 boards give 40 cells. This is
     also JLCPCB's minimum.
   * "Remove order number": *Specify a location* is not needed; choose
     *No* or accept the default.
2. Enable **PCB Assembly**, *Economic*, *Top side*, quantity 5 (or 2 to test
   first, then order the rest).
3. Upload **`lfp8_bom_jlcpcb.csv`** (BOM) and **`lfp8_cpl_jlcpcb.csv`**
   (pick-and-place).
4. In the parts list, check that every line matched the LCSC number given.
   * **U6 ESP32-C3-WROOM-02-N4 (C2934560)** could not be verified offline when
     this design was made. If JLCPCB shows it out of stock, pick another
     ESP32-C3-WROOM-02 4 MB variant (same footprint), or leave U6 unplaced and
     solder a module by hand.
   * Extended parts cost a one-time $3 loading fee each. There are 6 extended
     types: U6, U7 (ADS1115), R_x50 (0.1 Ω shunt), R_x06 (1.2 Ω), R_x24–27
     (11 Ω) and F_x01 (PTC). Everything else is basic or preferred, which has no
     loading fee.
5. **Check the placement preview carefully** (JLCPCB's DFM / 3D view). Rotation
   offsets for SOT-23, SOT-223, SOIC and TSSOP have already been applied in
   the CPL. Still confirm the pin-1 / cathode marks of:
   * SS34 / SS54 / SS14 diodes, BAT54WS, LEDs and SMBJ6.0A (cathode bar)
   * SOT-23 FETs and transistors (pin 3 alone on one side)
   * LM324 / LM358 / LM393 / 74HC595 / CD4051 / ADS1115 (pin-1 dot)
   * ESP32 module (antenna towards the board edge)
   If any part is rotated wrongly, fix it in the JLCPCB web editor (it lets you
   rotate parts), or correct `ROT_DB` in `hardware/gen/make_fab.py` and
   regenerate.

### Cost estimate (October 2026, before shipping and tax)

Researched and cross-checked against JLCPCB's fee pages, 2026 checkout records
and the JLC part database. No live quote was possible from the build machine,
so upload the files for an exact figure.

| Order | PCB fab | Fixed PCBA fees¹ | Joints² | X-ray³ | Parts | **Total** | **Per board** |
|---|---|---|---|---|---|---|---|
| 5 PCBs, 2 assembled (JLC minimum) | ≈ $26 | $29 | $6 | $3 | ≈ $44 | ≈ $108 | **≈ $54** |
| 5 PCBs, 5 assembled (one 40-cell set) | ≈ $26 | $29 | $16 | $8 | ≈ $100 | ≈ $180 | **≈ $36** |
| 10 PCBs, 10 assembled | ≈ $46 | $29 | $32 | $16 | ≈ $185 | ≈ $308 | **≈ $31** |

1. Economic PCBA, once per order:
   * setup $8.18
   * stencil $1.53
   * nitrogen reflow ≈ $0.90
   * $3.07 loading fee for each of the 6 extended part types: ESP32-C3 module,
     ADS1115, 0.1 Ω shunt, 1.2 Ω and 11 Ω 2512 resistors, PTC. The other 60
     BOM lines are basic or preferred parts and pay no loading fee.
2. 2016 SMT joints per board × $0.0016.
3. $1.64 per board if JLCPCB X-rays the ESP32 module's ground pad.

* Uncertainty is about ±15 %. The bare-PCB price ($20–34 for 5) and the
  basic-part prices are the soft spots. A $9 monthly SMT coupon may apply.
* Hand-soldered connectors (§2) add about $0.5–1 per board at LCSC minimum
  quantities.
* The board must stay on **Economic** PCBA:
  * 216 × 146 mm fits its 480 × 320 mm limit.
  * It is under the 650 cm² large-size fee threshold.
  * Standard PCBA would add ≈ $107 per order: feeder fees on all 66 BOM lines,
    higher setup and stencil fees.
* JLCPCB adds 5 mm edge rails automatically, because the ESP32 antenna sits at
  the edge (about +$1–3).
* Economic PCBA quantities are probably 2, 5, 10…, so check whether 3 or 4
  assembled boards can be selected.
* Shipping, tariffs and taxes depend on the destination. Example: to the US
  with DHL DDP (≈ $50–75), JLCPCB's pre-collected tariff (≈ 35 %) and state
  sales tax, the landed cost is ≈ $64–68 per board for the 5-board order.

## 2. Hand-soldered parts (per board)

`lfp8_hand_solder.csv` lists them:

| Ref | Part | Qty | Notes |
|---|---|---|---|
| J101…J801 | JST-XH B6B-XH-A, 6-pin vertical, 2.5 mm | 8 | Cell harness: 1 B+F, 2 B+S, 3 B−S, 4 B−F, 5 NTC, 6 GND |
| J1 | 2-pin 5.08 mm screw terminal (≥ 10 A, e.g. KF301-5.0-2P) | 1 | **+5 V input**. Use ≥ 1.5 mm² (16 AWG) wire, keep it short |
| J2 | 2-pin 2.54 mm header | 1 | 5 V fan (+ on pin 1) |
| J3 | 6-pin 2.54 mm header | 1 | USB: VBUS, D−, D+, GND, EN, BOOT (programming) |
| J4 | 6-pin 2.54 mm header | 1 | +5V, GND, AUX_OUT, SDA, SCL, +3V3 (expansion) |

## 3. Wiring the cells

Each channel connector carries **separate force and sense wires** at both cell
terminals. The holder in `mechanical/` provides a force plate plus a sense pogo
pin at each end:

```
JST pin 1  B+F  ─── + force plate     (≥ 0.5 mm² / 20 AWG, ≤ 30 cm)
JST pin 2  B+S  ─── + sense pogo      (any thin wire)
JST pin 3  B−S  ─── − sense pogo
JST pin 4  B−F  ─── − spring plate    (≥ 0.5 mm² / 20 AWG, ≤ 30 cm)
JST pin 5  NTC  ─── 10 k NTC (B 3950) on the cell body, other leg to pin 6
JST pin 6  GND
```

Do **not** join B+F and B+S (or B−F and B−S) at the connector. The
point of 4-wire sensing is that they meet only at the cell terminal.

## 4. Power supply

* 5.20 V (5.00–5.25 V) regulated, **≥ 10 A per board**, current-limited or
  fused. Five boards can share one 5 V / 50–60 A supply; run a separate pair of
  wires to each board.
* Adjust the PSU to 5.20 V **measured at J1 under load**. The web UI shows the
  rail voltage (`V_IN`).
* The board protects itself against reversed polarity with a crowbar diode
  (SS54) that shorts the input. Only a current-limited or fused supply survives
  that.

## 5. Bring-up checklist (per board)

1. Before connecting cells: power from a bench supply at 5.2 V with a 1 A limit.
   Idle current should be ≈ 80–120 mA. The red power LED (D10) is on and the
   green SAFE LED (D9) is off: SAFE_EN stays low until firmware runs the
   heartbeat.
2. Flash the firmware through J3 (USB D+/D− of the ESP32-C3), see
   `firmware/README.md`. After boot D9 turns on (SAFE_EN high).
3. Press E-STOP (SW1): D9 must go off and the web UI must show `ESTOP`.
4. Connect one cell. The UI shows its voltage within ±5 mV of a DMM measured
   at the cell terminals.
5. Start a charge at ISET 4. Current settles at ≈ 1.0 A. Raise the PSU current
   limit to 10 A before using all 8 channels.
6. Start a discharge. ≈ 1.07 A at 3.29 V, and the fan starts.
7. Run an IR measurement on a known cell and compare with a 4-wire IR meter.

## 6. Printing the holder

See `mechanical/README.md`. In short: 10 × cradle (5 of each label variant),
5 × board stand, PETG, 0.2 mm layers, no supports, all within the Ender 3 S1 Pro
volume.
