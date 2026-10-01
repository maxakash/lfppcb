# LFP-8 Hardware ⇄ Firmware Interface Specification

This document is the contract between the **LFP-8 channel board** (8 LiFePO4
channels per board, 5 boards = 40 cells) and the ESP32-C3 firmware. If the
schematic and this document disagree, the schematic generator
(`hardware/gen/lfp8_circuit.py`) is the source of truth and this file must be
updated.

## 1. Electrical domains

| Rail   | Source                      | Used by                                                     |
|--------|-----------------------------|-------------------------------------------------------------|
| `+5V`  | External PSU, **set to 5.20 V** (5.00–5.25 V allowed) | Charge path, LM324/LM393/LM358, 74HC595, CD4051, ADS1115     |
| `+3V3` | AMS1117-3.3 from `+5V`      | ESP32-C3-WROOM-02 only (+ NTC pull-ups)                     |
| `VREF` | CJ431 shunt reference 2.495 V | CV set-point, OV/UV thresholds, watchdog/rail/temp thresholds |

The ESP32 (3.3 V) talks to the 5 V domain through BSS138 level shifters
(I²C and the shift-register lines).

## 2. ESP32-C3-WROOM-02 GPIO map

| GPIO | Module pin | Direction | Net          | Function |
|------|-----------:|-----------|--------------|----------|
| IO0  | 18 | ADC1_CH0 in | `ADC_NTC`  | Output of mux D (cell NTC divider, 0–3.3 V) |
| IO1  | 17 | ADC1_CH1 in | `ADC_VCHK` | Mux A output ÷ 2 (1M/1M + 10 nF, τ = 5 ms) – independent cross-check of the selected cell's B+ sense. Keep the mux on that channel ≥ 25 ms before reading |
| IO2  | 16 | out (strap, pulled high) | `RCLK3` | 74HC595 storage-register clock (latch), rising edge latches |
| IO3  | 15 | ADC1_CH3 in | `ADC_VIN`  | `+5V` × 0.3197 (100k over 47k divider) |
| IO4  |  3 | ADC1_CH4 in | `ADC_TBRD` | Board temperature: Vbe of diode-connected MMBT3904 biased at ≈100 µA (0.601 V @ 25 °C, −1.97 mV/K) |
| IO5  |  4 | I²C SDA     | `SDA3`     | ADS1115 (addr 0x48) via level shifter, 100–400 kHz |
| IO6  |  5 | I²C SCL     | `SCL3`     | |
| IO7  |  6 | out         | `SER3`     | 74HC595 serial data |
| IO8  |  7 | out (strap, pulled high) | `SRCLK3` | 74HC595 shift clock, rising edge shifts |
| IO9  |  8 | in (strap, BOOT button to GND, pull-up) | `BOOT` | User button after boot (active low) |
| IO10 | 10 | out         | `HEARTBEAT`| Hardware watchdog charge pump input. **Must be toggled by software in the main control loop, ≥ 100 Hz square wave (toggle at least every 5 ms)** (see §6) |
| IO18 | 13 | USB D−      | `USB_DN`   | Native USB-Serial/JTAG (programming + console) |
| IO19 | 14 | USB D+      | `USB_DP`   | |
| IO20 | 11 | in          | `SAFE_RB`  | Read-back of `SAFE_EN` (high = hardware allows power stages) |
| IO21 | 12 | out         | `FAN_PWM`  | Fan low-side MOSFET gate (PWM allowed, 25 kHz recommended) |
| EN   |  2 | –           | `EN`       | Reset button |

Boot straps: IO2 and IO8 are held high by the level-shifter pull-ups, IO9 by
its pull-up. Do not drive IO2/IO8 low before `setup()` runs.

## 3. Shift-register chain (4 × 74HC595, 32 bits)

* Firmware shifts a 32-bit word **MSB first** on `SER3`/`SRCLK3`, then pulses
  `RCLK3`. After that, **bit *i* of the word appears on output position *i***,
  where position = 8·(chip−1) + (QA=0 … QH=7).
* The 74HC595 `OE` pin is driven by hardware (`OE_N = NOT SAFE_EN`). When the
  hardware safety chain is not satisfied, all outputs are high-Z and pulled
  to their safe (off) state. Firmware must still write 0s on any fault.
* Max shift clock: 1 MHz (BSS138 shifters). Use ≤ 400 kHz.

| Bits | Name | Meaning (1 = active) |
|------|------|----------------------|
| 3·(k−1)+0 | `CHG_EN_k` | Enable charger of channel k (k = 1…8) |
| 3·(k−1)+1 | `DIS_EN_k` | Enable discharge load of channel k |
| 3·(k−1)+2 | `LED_k`    | Channel LED (green) |
| 24 | `MUX_S0` | CD4051 select A (all five muxes in parallel) |
| 25 | `MUX_S1` | CD4051 select B |
| 26 | `MUX_S2` | CD4051 select C |
| 27 | `ISET_B0` | Charge-current DAC bit 0 (LSB) |
| 28 | `ISET_B1` | Charge-current DAC bit 1 |
| 29 | `ISET_B2` | Charge-current DAC bit 2 (MSB) |
| 30 | `AUX_OUT` | Spare open output on header J_AUX pin 3 |
| 31 | – | unused, write 0 |

Mux select value `n = S2·4 + S1·2 + S0` selects **channel k = n + 1**.

**Interlock:** hardware forces the charger off while the discharge driver
output is high, but firmware must never set `CHG_EN_k` and `DIS_EN_k`
together.

## 4. Charge current DAC (board-wide)

`ISET = ISET_B2·4 + ISET_B1·2 + ISET_B0` (0…7) sets the CC limit of **all
8 channels** of the board (`I = VS / 1.2 Ω`, VS from a 3-bit DAC fed by the
5 V shift-register outputs, so it scales with the rail):

| ISET | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|---|---|---|---|---|---|---|---|
| set-point @ 5.20 V (A) | 0 (charger blocked) | 0.265 | 0.532 | 0.797 | 1.064 | 1.328 | 1.595 | 1.860 |
| simulated @ 3.30 V cell (A) | 0.000 | 0.260 | 0.517 | 0.772 | 1.004 | 1.003 | 1.003 | 1.003 |

`ISET_LUT_A[] = {0, 0.265, 0.532, 0.797, 1.064, 1.328, 1.595, 1.860}` × `V_IN/5.20`.
The linear charger also has a **passive current ceiling** set by the 1.2 Ω
ballast, the SS34 diode and the cell wiring:
`I_max ≈ (V_IN − 0.38 V − V_cell) / 1.5 Ω` (≈ 1.0 A at 3.3 V with a 5.20 V
rail). Codes above 4 therefore only help for low cell voltages. Default
ISET = 4. The *measured* current is used for all accounting.

**Hardware CV limit:** 3.576 V nominal; component tolerances give
3.51–3.62 V (Monte Carlo, 100 runs). Firmware still stops at `V_MAX_CHG`.

**Thermal policy (pass MOSFET is a SOT-23):** continuous CC only when
`V_cell ≥ 3.05 V`. Between 2.80 and 3.05 V pulse `CHG_EN_k` at 50 % duty
(1 s period); between 2.00 and 2.80 V precharge at 20 % duty; below 2.00 V
refuse (`DEAD_CELL`).

## 5. Analog measurement chain

Five CD4051 multiplexers share the select lines; for channel k:

| Mux | Input k | Output goes to | Signal |
|-----|---------|----------------|--------|
| A | `B+S_k` (via 1 kΩ) | ADS1115 **AIN0** and ESP32 IO1 (÷2) | Cell + Kelvin sense |
| B | `B−S_k` (via 1 kΩ) | ADS1115 **AIN1** | Cell − Kelvin sense |
| C | `SHP_k` (via 1 kΩ) | ADS1115 **AIN2** | Shunt top (Kelvin) |
| E | `SHN_k` (via 1 kΩ) | ADS1115 **AIN3** | Shunt bottom (Kelvin) |
| D | `NTC_k` | ESP32 IO0 | Cell NTC (10 k pull-up to 3.3 V) |

ADS1115 (address 0x48, VDD = 5 V):

| Quantity | MUX config | PGA | Formula |
|----------|-----------|-----|---------|
| Cell voltage | AIN0 − AIN1 (`0b000`) | ±4.096 V (LSB 125 µV) | `V_cell = code · 4.096/32768 · CAL_V[k]` |
| Cell current | AIN2 − AIN3 (`0b011`) | ±0.256 V (LSB 7.8125 µV) | `I_cell = code · 0.256/32768 / R_SHUNT · CAL_I[k]` with `R_SHUNT = 0.100 Ω` |
| Neg. sense vs shunt-gnd (diagnostic) | AIN1 − AIN3 (`0b010`) | ±1.024 V | B− contact / wiring resistance: `R = V/I − 0.156 Ω` while ≥ 0.2 A flows; > 300 mΩ ⇒ `contact_warn` |

* **Sign convention:** `I_cell > 0` = charging, `I_cell < 0` = discharging.
* `CAL_V[k]`, `CAL_I[k]` default to 1.000 (stored in NVS, editable on the web UI).
* After changing the mux select, wait ≥ 2 ms before starting a conversion
  (1 kΩ + mux R into 10 nF filter caps).
* Use 128 SPS for routine scanning, 860 SPS for IR pulses.
* `V_cell` reads ≈ −1 V to −3.3 V for a **reversed cell** (hardware isolates it
  within ~50 µs; firmware must flag `REVERSED`).
* `V_cell` < 0.5 V with no current ⇒ `EMPTY` (no cell; an empty slot reads ≈ 0.03 V). A full channel scan
  (both conversions × 8 channels) at 128 SPS takes ≈ 150 ms.

ESP32 ADC (12-bit, use `analogReadMilliVolts`, 11 dB attenuation):

| Pin | Conversion |
|-----|-----------|
| IO0 `ADC_NTC` | `R_ntc = 10k · V/(3.3 − V)`; Beta model B=3950, R25=10k. V > 3.2 V ⇒ no NTC fitted |
| IO1 `ADC_VCHK` | `V_B+S = 2 · V`. Must agree with ADS1115 `B+S` within ±60 mV (ESP32 ADC is coarse) else `ADC_FAULT` |
| IO3 `ADC_VIN` | `V_IN = V / 0.3197` |
| IO4 `ADC_TBRD` | `T = 25 + (V − V25)/(−0.00197)` °C, `V25` calibrated at first boot assuming ambient (default 0.601 V) |

## 6. Hardware safety chain (`SAFE_EN`) – what firmware must respect

`SAFE_EN` is the AND (open-collector wired-AND) of:

1. **Watchdog:** `HEARTBEAT` square wave ≥ 100 Hz (a charge pump keeps a
   comparator happy; ≤ 50 Hz is rejected). If the toggling stops (stuck high
   or low) `SAFE_EN` drops within 23–33 ms. `SAFE_EN` also stays low until
   VREF has settled (≈ 50 ms after power-up).
   **Never use LEDC/RMT/timer hardware to generate the heartbeat** – it must
   come from the main control loop so a hung loop really trips the watchdog.
2. **Rail UV:** `+5V` > 4.48 V (with ~40 mV hysteresis).
3. **Rail OV:** `+5V` < 5.50 V.
4. **Board over-temperature:** board sensor < ≈ 80–90 °C (trips between 70 and 95 °C in simulation).
5. **E-STOP button** not pressed.

When `SAFE_EN` is low: every cell is galvanically isolated by its low-side
MOSFET, the 74HC595 outputs are high-Z, chargers and loads are off.
Firmware behaviour on `SAFE_RB` = low while it expected high: abort all
channel jobs, record the reason (heartbeat stopped? VIN? temperature? e-stop),
write all-zero to the shift registers, show the alarm on the web UI.

Per-channel hardware protections that firmware will observe:

| Protection | Trip | Firmware sees |
|------------|------|---------------|
| CV limit (charger) | Kelvin V_cell ≥ 3.576 V (3.51–3.62) | current tapers to 0 |
| OV latch | Kelvin V_cell ≥ 3.82 V (3.78–3.87) | cell isolated **and** charger drive killed; releases only when V_cell < 2.74 V (cell removed) **or when SAFE_EN goes low** — firmware can reset all latches by stopping the heartbeat for ≥ 100 ms (firmware uses 120 ms) |
| Discharge UV backstop | Kelvin V_cell < 2.22 V under load; discharge cannot (re)start below 2.59 V | discharge current stops; use a charge pulse for IR on cells below 2.6 V |
| Reverse polarity | B− more than ~1.6 V above B+ | cell isolated (~0.1 mA residual), charger drive killed; `V_cell` negative |
| Unpowered | PSU off | cell isolated by back-to-back MOSFETs; residual drain ≈ 80 µA via the sense networks (≈ 0.4 %/month) |
| PTC fuse 2 A hold | short / > 4 A | `V_cell` collapses / channel dead until PTC cools |
| Charge path current limit | `I_chg` > ISET setpoint | measured current never exceeds setpoint (+10 %) |

## 7. Firmware cell-test policy (LiFePO4 33140, 15 Ah nominal)

| Parameter | Default | Notes |
|-----------|---------|-------|
| `V_MAX_CHG` | 3.60 V | firmware stops CV phase if exceeded (hardware CV is 3.575 V) |
| `V_ABS_MAX` | 3.70 V | > this for 2 s with CHG_EN=0 ⇒ `OV_FAULT` |
| `I_TERM` | 0.05 A | end of charge when CV current < I_TERM for 60 s |
| `V_MIN_DIS` | 2.50 V | discharge cut-off (measured under load) |
| `V_PRECHARGE` | 2.80 V | below this: pulse charge 1 s on / 4 s off |
| `V_DEAD` | 2.00 V | below this: refuse to charge (`DEAD_CELL`) |
| `T_CELL_MAX` | 55 °C | pause channel; resume < 45 °C |
| `T_BOARD_FAN` | 40 °C | fan 100 % above, else 40 % when any channel active |
| `T_BOARD_MAX` | 70 °C | firmware derates (stop new discharges) |
| `MAX_CHG_TIME` | 30 h | timeout ⇒ `TIMEOUT` |
| `MAX_DIS_TIME` | 30 h | |
| Capacity | coulomb counting at ≥ 5 Hz | report mAh and mWh |
| DC-IR | see §7.1 | report mΩ |

### 7.1 Internal resistance ("resistance calculator")

For each cell (on demand or automatically at 50 % SoC during a test):

1. Charger and load off; rest ≥ 5 s. Measure `V0`, `I0` (16-sample average,
   mux locked on the channel).
2. Enable `DIS_EN_k` (≈ 1.05 A). Sample `V_cell`/`I_cell` alternately at
   860 SPS for 1.0 s.
3. `R_ohmic = (V0 − V(10 ms)) / (I(10 ms) − I0)` — fast (≈ AC-IR like).
4. `R_dc = (V0 − V(1 s)) / (I(1 s) − I0)` — IEC 61960-style DC-IR.
5. Disable load. Report both in mΩ with the measurement current.

If the cell is below 2.65 V (UV backstop blocks the load), do the same with a
charge pulse instead (`CHG_EN_k`, ISET 4) and report with the sign flipped.

## 8. Fixed board constants

```
R_SHUNT        = 0.100 Ω  (2512, ±1 %, 50 ppm/K)
R_CHG_BALLAST  = 1.2 Ω    (charge path, also the CC sense resistor)
R_LOAD         = 2.75 Ω   (4 × 11 Ω 2512 in parallel) -> ≈1.07 A at 3.29 V, 1.2 A at 3.6 V
DIV_VCHK       = 0.5      (1M/1M, 10 nF)
DIV_VIN        = 0.3197   (+5V → 100k → ADC_VIN → 47k → GND)
NTC_PULLUP     = 10 kΩ to 3.3 V
VREF           = 2.495 V
CV_SET         = 3.576 V nominal (differential Kelvin sense)
OV_TRIP        = 3.82 V (latching, release 2.74 V or SAFE_EN low)
UV_TRIP        = 2.22 V under load (DIS_EN=1), re-arm 2.59 V
TBRD           = 0.601 V @ 25 °C, −1.97 mV/K
```

## 9. Multi-board networking (firmware convention)

* Each board has a `board_id` 1–5 (NVS, default from MAC) and hostname
  `lfp8-<id>`; channel numbers on the UI are global: `(board_id−1)·8 + k`.
* Boards announce themselves with a UDP broadcast on port **45454** every 2 s:
  `{"t":"hello","id":<id>,"ip":"x.x.x.x","fw":"x.y.z"}`.
* Every board serves the full dashboard at `/` and `GET /api/status`
  (JSON for its own 8 cells), `GET /api/peers`, `POST /api/cmd`. The
  dashboard aggregates all peers by fetching `http://<peer>/api/status`
  (CORS `Access-Control-Allow-Origin: *`).
