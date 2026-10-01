# LFP-8 firmware

Firmware for the **LFP-8** board: one ESP32-C3-WROOM-02 controlling 8
LiFePO4 channels (33140, 3.2 V, 15 Ah). Five boards test 40 cells and show up
together on one web dashboard. The hardware/firmware contract is
[`docs/INTERFACE.md`](../docs/INTERFACE.md); this firmware follows it
(GPIO map, 74HC595 bit map, CD4051 mux, ADS1115 configs, safety chain, test
policy, IR method, networking).

Features: charge (CC/CV with pulse precharge), discharge to 2.5 V, full
capacity test (charge -> rest -> discharge with mAh/mWh -> rest -> storage
charge to 50 % -> IR), DC-IR and ohmic IR (section 7.1), per-cell and board
temperature supervision, 24 h V/I history per channel, multi-board dashboard,
resistance calculator, pack builder (SxP grouping), CSV export, settings and
calibration in NVS, OTA.

## Layout

```
firmware/
  lfp8/                     Arduino sketch (arduino-cli compile ... firmware/lfp8)
    lfp8.ino                setup()/loop() -> app_esp32
    lfp8_config.h           board constants, pins, timing (INTERFACE.md sections 2-8)
    hal.h                   HAL interface: shift register, ADS1115 registers, ESP32 ADC, GPIO, time
    adc_conv.*              ADS1115 config words, code->V/I, NTC, VIN, board temperature
    shiftreg.*              32-bit 74HC595 word builder, CHG/DIS interlock
    channel.*               per-channel state machine + test programs, coulomb counters
    coulomb.h               trapezoidal mAh/mWh integration (double accumulators)
    ir_calc.*               R_ohmic (10 ms) / R_dc (1 s) computation
    supervisor.*            safety supervisor (SAFE_RB, VIN, temperatures, ADC/I2C faults)
    controller.*            scanner, IR engine, outputs, heartbeat, fan, LEDs, commands
    history.*, leds.*, settings.*, peers.*, packbuilder.*
    json_writer.*, json_reader.*   tiny JSON emitter / reader (no ArduinoJson)
    api.*                   JSON/CSV documents and request parsing for the HTTP API
    *_esp32.*               ESP32-only glue: HAL, control task, NVS, WiFi/mDNS/UDP/OTA, HTTP
    webui_html.h            the dashboard (HTML/CSS/JS, PROGMEM, no CDN)
  test/                     host tests (own mini framework) + simulated board
  API.md                    HTTP/UDP API
  Makefile                  make test | make build | make clean
```

Everything outside `*_esp32.*`, `lfp8.ino` and `webui_html.h` is plain C++17
without Arduino headers and is compiled and tested on the host against
`test/sim_board.cpp`, a model of the board (74HC595 decode, ADS1115 register
emulation, LFP cells with OCV curve + R0 + RC, CC/CV charger with ISET DAC and
CV limit, 2.75 ohm load with UV backstop, OV latch, NTCs, heartbeat charge
pump / SAFE_EN chain).

## Building

### arduino-cli

```sh
arduino-cli config add board_manager.additional_urls \
  https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32          # tested with 3.3.12
arduino-cli compile --fqbn esp32:esp32:esp32c3:PartitionScheme=min_spiffs firmware/lfp8
# or: make -C firmware build
```

Only libraries bundled with the core are used (WiFi, WebServer, ESPmDNS,
Preferences, Wire, Update, ArduinoOTA). The partition scheme is
**Minimal SPIFFS** (2 x 1.9 MB OTA app slots, 128 KB SPIFFS, same NVS
location as the default scheme). Result with core 3.3.12:

```
Sketch uses 1240851 bytes (63%) of program storage space. Maximum is 1966080 bytes.
Global variables use 165144 bytes (50%) of dynamic memory, leaving 162536 bytes for local variables.
```

(With the default scheme the same image would fill 94 % of a 1.25 MB slot.)
The LFP-8 code itself is ~90 KB of flash (37 KB of it the web page) and
~122 KB of static RAM (92 KB is the 24 h history ring). A board that was
flashed with a different partition scheme must be flashed once over USB with
this one (OTA cannot change the partition table; settings in NVS survive).

Flash over USB (native USB-Serial/JTAG on IO18/IO19):

```sh
arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32c3:PartitionScheme=min_spiffs firmware/lfp8
arduino-cli monitor -p /dev/ttyACM0 -c baudrate=115200    # console log
```

To produce `lfp8.ino.bin` for OTA:
`arduino-cli compile --fqbn esp32:esp32:esp32c3:PartitionScheme=min_spiffs --output-dir /tmp/lfp8-bin firmware/lfp8`.

The console uses the USB-Serial/JTAG peripheral regardless of the
"USB CDC On Boot" menu (an own `HWCDC` instance) - UART0 is never started,
because its pins IO20/IO21 are `SAFE_RB` and `FAN_PWM` on this board. The
ROM bootloader still prints on IO21 at reset (the fan may twitch briefly).

### Arduino IDE 2.x

1. *File -> Preferences -> Additional boards manager URLs*: add the URL above;
   install **esp32 by Espressif Systems** (3.x) in the Boards Manager.
2. Open `firmware/lfp8/lfp8.ino`. Board: **ESP32C3 Dev Module**,
   Partition Scheme **"Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)"**,
   Flash 4 MB, USB CDC On Boot either setting.
3. Upload via the USB port. If the board does not enter the bootloader, hold
   BOOT (IO9) while pressing EN.

### Host tests

```sh
make -C firmware test
```

Builds and runs 76 unit/integration tests (ADC conversions incl. sign,
ADS1115 config words, ISET set-points and the passive current ceiling,
shift-register bit map and interlock, state machine: empty/reversed
detection, CC/CV, I_TERM and plateau termination (hardware CV 3.51 V),
V_MAX_CHG, 2.5 V cut-off, thermal-policy pulsing (20 % below 2.80 V, 50 %
below 3.05 V), refusal below 2.0 V, OV faults and hardware OV-latch
detection/release, timeouts, temperature pause/resume, current plausibility;
contact-resistance hint, IO1 cross-check dwell with the RC filter, heartbeat
timing (VREF delay at boot, 15 ms hiccup tolerated, 40 ms trips); IR on a
simulated cell (R0 8 mOhm, R1 4 mOhm, tau 0.5 s -> R_ohmic 8.07, R_dc 11.47
mOhm) incl. the charge-pulse fallback; coulomb counting (1.000 A x 1 h =
1000 mAh, also through the full ADC chain: -0.006 %); supervisor (SAFE_RB
loss, stalled loop, ADC cross-check mismatch and stray current stop the
heartbeat, I2C faults, VIN window, board temperature derating); pack builder
(40 random cells, 4S10P spread < 1 %); JSON emitter/reader and every API
document), parses all emitted JSON with `python3 -c 'import json'`, checks the
embedded web UI (`node --check` when node is installed) and runs
`sim_cell_test`: a full capacity test on 8 simulated 15 Ah cells (37 h of
simulated time in ~70 s, hardware CV spread over 3.51-3.62 V, one cell
starting at 2.58 V) that must report every capacity within +-1 % and never
keep CHG_EN on longer than one 1 s pulse below 3.0 V.

## Power

Set the external +5V supply to **5.20 V** (5.00-5.25 V allowed; the
charge-current set-points scale with V_IN/5.20). The hardware drops SAFE_EN
below 4.48 V or above 5.50 V; the firmware aborts jobs outside its own window
(default 4.80-5.40 V, `vin_min`/`vin_max`) and shows a hint when the rail is
outside 5.00-5.25 V.

## First boot and WiFi

1. Power the board. Without stored credentials it opens the access point
   **`LFP8-<id>`**, password **`lfp8admin`**. Connect and open
   `http://192.168.4.1/`.
2. *Settings*: enter WiFi SSID/password and a unique **board id 1..5**
   (cells are numbered `(id-1)*8 + k`, so board 3 shows cells 17-24; the
   default id is derived from the MAC and may collide). *Save*, then
   *Reboot board*.
3. The board joins the WiFi (hostname `lfp8-<id>`, mDNS
   `http://lfp8-<id>.local/`). If the network is not reachable within 15 s it
   opens the AP again (and keeps retrying the WiFi in the background; the AP
   is switched off 2 minutes after the WiFi connection is back and no AP
   client is connected).
4. Holding **BOOT for 5 s** while running clears the WiFi credentials and
   restarts in AP mode.

On the first boot the board-temperature diode is calibrated assuming the
board is at 25 C (V25); recalibrate later in *Settings* if needed.

## Multi-board operation

* Every board broadcasts `{"t":"hello","id":..,"ip":..,"fw":..}` on UDP
  45454 every 2 s and keeps a peer table (`GET /api/peers`).
* Open the dashboard of **any** board: the browser fetches `/api/peers` from
  that board and then polls `http://<peer>/api/status` of every board
  directly (CORS `*`), so the grid shows all 40 cells. Commands go straight
  to the owning board (`POST http://<peer>/api/cmd`). Offline boards are
  marked, duplicate board ids are flagged.
* History charts and IR traces are fetched from the owning board; the pack
  builder posts the cells of all boards to the board that served the page.
* All boards and the browser must be in the same broadcast domain (same
  WiFi/subnet). Through the AP (192.168.4.1) only that board is visible.

## Using the tester

Per cell: **Charge**, **Discharge**, **Cap test**, **IR**, **Stop**,
**Reset** (clears a fault and the stored results). Board panel: VIN, board
temperature, SAFE status, heartbeat, fan, **ISET** (0..7, 0.24-1.69 A,
board-wide; >= 6 only below 50 C), alarm acknowledge, stop board, identify.
Header: **Stop all** (all boards).

| Program | Sequence |
|---|---|
| charge | rest voltage < 2.00 V -> `FAULT_DEAD_CELL` (refused); 2.00-2.80 V -> precharge at 20 % duty (1 s on / 4 s off); 2.80-3.05 V -> 50 % duty (0.5 s on / 0.5 s off) - thermal policy of the SOT-23 pass FET, decided on the rest voltage in the off phases; >= 3.05 V continuous CC; CV detected at >= 3.50 V or when the current falls below 85 % of what the charger should deliver (set-point and passive ceiling (V_IN-0.38-V)/1.5 ohm). End of charge by **current**: I < 0.05 A for 60 s, or a CV current below 0.2 A that stopped falling for 10 min (plateau); the hardware CV is 3.51-3.62 V so no particular voltage is required. V >= 3.60 V (V_MAX_CHG) stops the charge as well. |
| discharge | 2.75 ohm load (~1.07 A at 3.29 V) until 2.50 V under load (3 consecutive samples); mAh/mWh reported. Refused below 2.62 V (the load cannot start below 2.59 V; hardware backstop 2.22 V under load). |
| captest | charge -> rest 30 min -> discharge (capacity) -> rest 30 min -> charge 50 % of the measured Ah -> rest 5 min -> IR -> DONE |
| ir | rest >= 5 s, 16-sample V0/I0, 1 s load pulse sampled at 860 SPS, R at 10 ms and 1 s; charge pulse (ISET 4) below 2.65 V |

The **B- path** value on each cell is the wire + contact resistance of the
negative connection, estimated whenever >= 0.2 A flows from the B-
diagnostic (AIN1-AIN3 = I x (R + 2 x 28 mOhm + 0.1 ohm), measured at
+-1.024 V full scale). Above 300 mOhm it shows `CONTACT_WARN` (warning only).

The measured current is used for all accounting (trapezoidal integration at
~6 Hz per channel, double accumulators); results survive a reboot (NVS).

LED codes (green LED per channel): EMPTY off, IDLE 50 ms blip / 2 s,
REVERSED 4 Hz, PRECHARGE double blink, CC 1 Hz, CV long on, DISCHARGING 2 Hz,
RESTING short flash, IR 10 Hz, PAUSED 0.5 Hz, DONE steady. Faults: N blinks
then a pause - 2 OV, 3 DEAD_CELL, 4 TIMEOUT, 5 OVERTEMP, 6 ADC, 7 CURRENT,
8 SAFETY. Board alarm: all LEDs 2 Hz in sync; identify: all LEDs 5 Hz.

## Calibration

Defaults are 1.000 for every `CAL_V[k]`/`CAL_I[k]`; factors are limited to
0.90-1.10 and stored in NVS.

1. **Voltage**: insert a cell, measure it with a calibrated DMM directly at
   the cell terminals, enter the value in *Settings -> Calibration ->
   Reference V* of that channel, press *Cal V*. (`{"ch":k,"cmd":"calv","value":3.3012}`)
2. **Current**: start a charge (or discharge) on the channel, put a DMM in
   series (or use a calibrated clamp), enter the reading (negative while
   discharging), press *Cal I*. Repeat per channel; the shunt is +-1 %.
3. **Board temperature**: enter the true board temperature and press
   *Calibrate* (sets V25; diode 0.601 V at 25 C, -1.97 mV/K). Done
   automatically at first boot assuming 25 C.
4. Check: the ESP32 IO1 cross-check (`vchk` in `/api/status`) must stay
   within 60 mV of the ADS1115.

## Safety design

The hardware safety chain (INTERFACE.md section 6) isolates every cell when
SAFE_EN drops; the firmware is designed to fail into that state.

* **Heartbeat** (IO10) is a plain GPIO write at the end of every control-loop
  iteration (`Controller::step()`, edges every ~1 ms = ~500 Hz square wave;
  the hardware needs >= 100 Hz and rejects <= 50 Hz), executed only when the
  supervisor is happy. It is never generated by LEDC/RMT/timers. The control
  loop is a dedicated FreeRTOS task (priority 10, above the web server; it
  waits at most 3 ms for the shared lock) so slow HTTP clients cannot starve
  it; if it stalls, the hardware drops SAFE_EN within 23-33 ms; the task
  watchdog resets the chip after 3 s. SAFE_EN is low for ~50 ms after
  power-up (VREF settling); that is not treated as a fault.
* **Flash writes** (NVS) can stall the CPU longer than 23 ms during a sector
  erase, so settings/results are written only while no job is running (or
  just before a reboot, after the outputs are off); changes take effect in RAM
  immediately. OTA is refused while jobs run.
* **Heartbeat is stopped** (all jobs aborted, all-zero written to the
  74HC595, hardware isolates every cell; latched until *Acknowledge*) on:
  SAFE_RB lost while expected high (reason recorded: VIN, temperature,
  control-loop stall, otherwise E-STOP/unknown - SAFE_EN therefore cannot come
  back by itself when the E-STOP is released), ADC cross-check mismatch
  (ESP32 IO1 vs ADS1115 B+ > 60 mV, 3 consecutive checks), repeated
  I2C/ADS1115 failures, ADS1115 missing at boot, current > 0.1 A on a channel
  whose outputs are off ("stray current", e.g. a shorted MOSFET), CHG+DIS
  requested together (interlock). Acknowledging restarts the heartbeat; if
  the cause persists the alarm trips again.
* **VIN** outside 4.80-5.40 V for 1 s, **board** > T_BOARD_MAX + 7 C: abort all
  jobs (latched alarm). Board > 70 C: discharges paused, no new discharges;
  >= 50 C: ISET limited to 5; unreadable board sensor: treated as hot.
* **OV latch** (hardware, 3.78-3.87 V): isolates the cell and kills its
  charger; firmware detects it (CHG_EN on, I ~ 0, V >= 3.75 V -> `FAULT_OV`).
  The latch only releases when SAFE_EN drops, so *Reset* of a `FAULT_OV`
  channel (or *Acknowledge* while an OV fault exists) pauses the heartbeat
  for 120 ms as soon as no job is running on the board (shown as "OV latch
  release pending/active"; new jobs on that channel are refused until then).
* **Per channel**: V > V_ABS_MAX with the charger on -> `FAULT_OV`
  immediately, for 2 s at rest -> `FAULT_OV`; V >= V_MAX_CHG ends the charge;
  cell > 55 C pause / < 45 C resume, > 65 C `FAULT_OVERTEMP`; < 0 C no charging;
  NTC short or lost during a job -> `FAULT_ADC`; no or wrong-direction current,
  current above ISET, > 2.2 A -> `FAULT_CURRENT`; no valid measurement for
  3 s or any I2C error during a job -> `FAULT_ADC`; charge/discharge
  timeouts 30 h; precharge that does not recover the cell in 4 h -> dead.
* CHG_EN and DIS_EN are never set together (word builder interlock, tested),
  and every change between charge and discharge goes through >= 2 s of rest.
* Settings are clamped to LFP-safe ranges (e.g. V_MAX_CHG <= 3.65 V,
  V_MIN_DIS >= 2.30 V) whatever is sent to the API.
* OTA (web or ArduinoOTA) is refused while any job runs (unless forced);
  during an update the outputs are off and the heartbeat stops on purpose.

Practical notes: never leave lithium cells charging unattended in a place
where a fire could spread; use a fused 5 V supply sized for 8 x 1.7 A plus
fans; the hardware CV limit (3.575 V +-1.5 %) and OV latch (3.82 V) work
independently of this firmware.

## Known limitations / open points

* IO1 (ADC_VCHK) has a 1M/1M + 10 nF divider (tau 5 ms): the cross-check
  keeps the multiplexer on the channel >= 26 ms before reading IO1, which
  is done for one channel per scan cycle (full scan ~175 ms, >= 5 Hz/channel).
* Results finished while other jobs keep running are held in RAM until the
  board is idle (NVS write deferral); a power loss before that loses them.
* The ESP32-C3 ADC at 11 dB saturates around 2.9-3.1 V, below the 3.2 V
  "no NTC" threshold of INTERFACE.md section 5; readings that convert to
  < -20 C are therefore also treated as "no NTC fitted".
* An open B- *sense* wire cannot be told apart reliably from the data the
  hardware provides; the B- diagnostic only gives warnings (idle offset,
  CONTACT_WARN).
* While an IR pulse runs (~1.1 s) the other 7 channels are not sampled
  (coulomb counting interpolates across the gap; hardware protections remain
  active).
* The interface leaves the sign convention of the section 7.1 formula open; the
  firmware reports positive resistances for both pulse types
  (R = dV/dI with signed current) and the pulse current with its sign.
* Unauthenticated LAN API for commands (settings/OTA can be protected with the
  admin password). Do not expose the boards to untrusted networks.
