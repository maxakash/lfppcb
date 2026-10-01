# LFP-8 HTTP / UDP API

Every board serves the same API on port 80. All JSON responses carry
`Access-Control-Allow-Origin: *` (plus `-Methods`/`-Headers: *`), so the
dashboard of one board can aggregate all boards. `OPTIONS` pre-flight requests
on any path are answered with `204`. Responses are `Cache-Control: no-store`.

Numbers that are unknown/not measured are `null` (never `NaN`). Channel `k`
is the **local** channel 1..8; `g = (board_id-1)*8 + k` is the global cell
number used on the UI. Current sign: **I > 0 charging, I < 0 discharging**.

Write requests accept a JSON body with `Content-Type: text/plain` (a CORS
"simple request", no pre-flight needed) or `application/json`.
Errors: `{"ok":false,"err":"reason"}` with HTTP 400 (bad request), 404, 409
(refused by the safety logic / state machine), 503 (controller busy) or 500.

| Method | Path | Purpose |
|---|---|---|
| GET  | `/` | dashboard (single page, embedded, works offline) |
| GET  | `/api/status` | board + 8 channels |
| GET  | `/api/history?ch=k` | 24 h V/I history of channel k (1 sample / 30 s) |
| GET  | `/api/irtrace` | samples of the last IR pulse on this board |
| GET  | `/api/peers` | boards discovered via UDP |
| POST | `/api/cmd` | channel / board commands |
| GET  | `/api/config` | settings (passwords are never returned) |
| POST | `/api/config` | change settings (admin password if set) |
| GET  | `/api/export.csv` | results of this board as CSV |
| POST | `/api/pack` | pack builder (S x P grouping) |
| GET  | `/update` | minimal OTA upload page |
| POST | `/update` | firmware upload (multipart, field `fw`) |

## GET /api/status

```json
{"id":2,"fw":"1.0.0","host":"lfp8-2","ip":"192.168.1.52","mode":"STA","rssi":-61,"dup_id":false,"uptime":5321,
 "board":{"vin":5.012,"tb":31.4,"safe":true,"safe_rb":true,"hb":true,"power":true,"meas":true,"fan":40,
          "iset":4,"iset_eff":4,"iset_a":0.970,"hot":false,"maint":false,"ident":false,
          "alarm":"","alarm_text":"","cond":"","loop_max_us":1180,"overruns":0,"i2c_err":0,
          "safe_losses":0,"scan_ms":168,"ir_busy":false,"ir_ch":0,"latch_reset":""},
 "ch":[{"k":1,"g":9,"st":"CHARGING_CC","prog":"captest","step":"charge","pause":"","valid":true,
        "v":3.3412,"i":0.9693,"t":24.6,"ntc":"ok","vchk":3.452,"vbm":0.1842,"bm_warn":false,
        "contact_mohm":34,"contact_warn":false,"ov_pending":false,"pulse_pct":0,
        "chg":true,"dis":false,"mah":1234.5,"mwh":4101.2,"target_mah":null,"el":4523,"step_el":4523,
        "cap_mah":null,"cap_mwh":null,"cap_s":0,"dis_term":"","chg_mah":null,"chg_mwh":null,"chg_term":"",
        "store_mah":null,"ir_ohm":null,"ir_dc":null,"ir_i":null,"ir_v0":null,"ir_mode":"","ir_at":0,"msg":""}, ...]}
```

Board fields: `vin` +5V rail (V), `tb` board temperature (C), `safe` debounced
SAFE_RB, `safe_rb` raw pin, `hb` heartbeat allowed by the supervisor, `power`
power stages may be enabled, `meas` measurements running (SAFE high),
`fan` %, `iset` configured DAC code, `iset_eff` after derating (>= 50 C limits
to 5), `iset_a` nominal current, `hot` board > T_BOARD_MAX (discharges
paused), `maint` OTA maintenance, `alarm` latched alarm
(`SAFE_LOST`, `VIN`, `BOARD_OVERTEMP`, `ADC_MISMATCH`, `I2C`, `ADS1115_MISSING`,
`STRAY_CURRENT`, `INTERLOCK`) with `alarm_text`, `cond` informational
condition, `loop_max_us` longest control-loop gap in the last 10 s,
`scan_ms` time for a full 8-channel scan, `latch_reset` `pending` (OV latch
release waits for an idle board) / `active` (heartbeat paused 120 ms) / empty.

Channel fields: `st` state: `EMPTY IDLE REVERSED CHARGING_PRE CHARGING_CC
CHARGING_CV DISCHARGING RESTING IR_MEASURE PAUSED DONE FAULT_OV
FAULT_DEAD_CELL FAULT_TIMEOUT FAULT_OVERTEMP FAULT_ADC FAULT_CURRENT
FAULT_SAFETY`; `prog` `none|charge|discharge|captest|ir`; `step`
`none|charge|rest_chg|discharge|rest_dis|storage|rest_ir|ir`; `pause`
`cell_hot|cell_cold|board_hot|iset0`; `ntc` `ok|none|short|invalid`;
`vchk` ESP32 IO1 cross-check of B+ (V), `vbm` B- diagnostic AIN1-AIN3 (V,
±1.024 V range); `contact_mohm` implied B- force-path (wire + contact)
resistance `vbm/I - 0.156 ohm`, estimated while >= 0.2 A flows (null if
unknown), `contact_warn` true above 300 mOhm (warning only), `bm_warn` B-
offset with no current (open B- wire?, warning only); `ov_pending` OV latch
release pending for this channel; `pulse_pct` 20/50 during pulsed charging
(thermal policy), else 0;
`chg`/`dis` actual CHG_EN/DIS_EN bits; `mah`/`mwh` live counter of the current
step (charged or discharged); `el`/`step_el` elapsed seconds of job/step.
Results (kept until the next job or a channel `reset`, persisted in NVS):
`cap_mah`/`cap_mwh`/`cap_s` discharge capacity to V_MIN_DIS and its duration,
`dis_term` (`cutoff|uv_backstop`), `chg_mah`/`chg_mwh`/`chg_term`
(`i_term|plateau|v_max`) last full charge, `store_mah` storage charge,
`ir_ohm` (10 ms) and `ir_dc` (1 s) resistance in mOhm, `ir_i` pulse current
(A, signed), `ir_v0` rest voltage, `ir_mode` `dis|chg`, `ir_at` uptime (s).

## GET /api/history?ch=k

```json
{"ch":1,"g":9,"interval":30,"count":2880,"age_s":12,"fmt":"mv,ma pairs, oldest first",
 "d":[3301,0,3305,968,null,null, ...]}
```

`d` holds `count` pairs (mV, mA), oldest first; the newest pair is `age_s`
seconds old. `null,null` = no valid measurement in that interval. Use
`d.length/2` as the authoritative count (the ring may advance while streaming).

## GET /api/irtrace

```json
{"valid":true,"ch":3,"g":11,"ok":true,"err":"","mode":"dis","v0":3.3001,"i0":0.0001,
 "v_10ms":3.2908,"i_10ms":-1.1462,"v_1s":3.2869,"i_1s":-1.1463,"r_ohmic_mohm":8.067,"r_dc_mohm":11.471,
 "v":[[0.58,3.2911],[2.91,3.2910], ...],"i":[[1.75,-1.1462], ...]}
```

`v`/`i` are `[ms after load on, value]`, decimated to <= 220 points.

## GET /api/peers

```json
{"self":{"id":1,"ip":"192.168.1.51","fw":"1.0.0","host":"lfp8-1"},"dup_id":false,
 "peers":[{"id":2,"ip":"192.168.1.52","fw":"1.0.0","age":0.8}]}
```

## POST /api/cmd

Channel commands (`ch` = local channel 1..8):

| Body | Effect |
|---|---|
| `{"ch":k,"cmd":"charge"}` | charge: 20 % duty below 2.80 V, 50 % duty below 3.05 V, then CC/CV; refuses below 2.00 V; ends on current taper |
| `{"ch":k,"cmd":"discharge"}` | discharge to 2.50 V, reports mAh/mWh (refused below 2.62 V) |
| `{"ch":k,"cmd":"captest"}` | charge -> rest -> discharge -> rest -> storage charge (50 % by Ah) -> rest -> IR |
| `{"ch":k,"cmd":"ir"}` | rest >= 5 s, IR pulse (section 7.1) |
| `{"ch":k,"cmd":"stop"}` | stop the job (results kept) |
| `{"ch":k,"cmd":"reset"}` | clear a fault **and the results** of the channel; for `FAULT_OV` also schedules the OV-latch release (heartbeat pause >= 100 ms when no job runs on the board) |
| `{"ch":k,"cmd":"calv","value":3.3012}` | set CAL_V[k] so that V reads the reference (cell present) |
| `{"ch":k,"cmd":"cali","value":-1.052}` | set CAL_I[k] from a reference current (>= 0.2 A flowing) |

Board commands:

| Body | Effect |
|---|---|
| `{"cmd":"iset","value":n}` | charge-current DAC 0..7 (persisted; >= 6 only below 50 C) |
| `{"cmd":"stop"}` / `{"cmd":"stopall"}` | stop all channels |
| `{"cmd":"ack"}` (or `reset` without `ch`) | acknowledge latched board alarms; restarts a stopped heartbeat (VIN / over-temperature alarms only clear once the cause is gone); schedules the OV-latch release if a channel is in `FAULT_OV` |
| `{"cmd":"identify"}` | blink all LEDs for 10 s |
| `{"cmd":"caltb","value":24.5}` | calibrate the board temperature sensor to the given C |
| `{"cmd":"reboot"}` | restart the board (outputs off first) |

Response: `{"ok":true}` or `{"ok":false,"err":"..."}` (HTTP 409 when refused,
e.g. `"dead cell (V < V_DEAD) - refusing to charge"`, `"no cell"`,
`"power stages not enabled (SAFE/alarm)"`).

## GET / POST /api/config

GET returns all settings; POST accepts any subset of the same keys (values
are clamped to safe ranges; malformed values are rejected and nothing is
changed). With an admin password set, POST requires HTTP basic auth (user
`admin`).

```json
{"board_id":1,"iset":4,"v25":0.6400,"v_max_chg":3.600,"v_abs_max":3.700,"i_term":0.050,"t_term_s":60,
 "v_min_dis":2.500,"v_precharge":2.800,"v_cc_min":3.050,"v_dead":2.000,"t_cell_max":55.0,"t_cell_resume":45.0,
 "t_cell_fault":65.0,"t_cell_min_chg":0.0,"t_board_fan":40.0,"t_board_max":70.0,"max_chg_h":30.0,
 "max_dis_h":30.0,"rest_chg_min":30,"rest_dis_min":30,"rest_ir_s":300,"ir_rest_s":5,"storage_pct":50,
 "captest_ir":1,"nominal_mah":15000,"xchk_tol_v":0.060,"vin_min":4.80,"vin_max":5.40,
 "wifi_ssid":"lab","wifi_pass_set":true,"admin_pass_set":false,
 "cal_v":[1.00000,...8],"cal_i":[1.00000,...8]}
```

Write-only keys: `wifi_pass` (>= 8 characters or empty), `admin_pass`.
Response: `{"ok":true,"changed":3,"reboot_required":false,"persisted":true}`
- a reboot is required after changing `wifi_ssid`, `wifi_pass` or
`board_id`. Changes apply immediately; `persisted:false` means they are
written to flash once no job is running (flash erases can stall the CPU
longer than the 23 ms hardware watchdog window).

Safe ranges enforced by the firmware: `v_max_chg` 3.40-3.65, `v_abs_max`
3.60-3.80 (and >= v_max_chg+0.05), `v_min_dis` 2.30-3.00, `v_precharge`
2.50-3.00, `v_cc_min` 3.05-3.20, `v_dead` 1.50-2.50 (and <= v_precharge-0.2), `t_cell_max` 35-60,
`t_board_max` 50-75, `max_*_h` 1-48, `ir_rest_s` >= 5, `cal_*` 0.90-1.10,
`vin_min` 4.55-4.95, `vin_max` 5.25-5.50.

## GET /api/export.csv

`board,ch,cell,state,program,step,v,i,t_cell,cap_mah,cap_mwh,cap_s,dis_term,chg_mah,chg_mwh,chg_term,store_mah,ir_dc_mohm,ir_ohmic_mohm,ir_i_a,ir_v0,ir_mode,elapsed_s,msg`
(one row per channel, CRLF line ends). The dashboard's "Export" tab builds
the same CSV for all boards.

## POST /api/pack

```json
{"s":4,"p":10,"sel":"top","irw":0.1,"cells":[9,15012.5,8.3, 10,14890.1,9.1, ...]}
```

`cells` is a flat array of `[id, capacity_mAh, dc_ir_mOhm]` triples (IR 0 =
unknown), up to 128 cells. `sel`: `top` (highest capacities) or `tight`
(narrowest capacity window) when more cells than S*P are given; `irw` weight
of the group-resistance balancing (0..10). Response:

```json
{"ok":true,"s":4,"p":10,"cap_spread_pct":0.157,"ir_spread_pct":0.16,"pack_cap_mah":148960.2,
 "pack_ir_mohm":3.52,"swaps":5,
 "groups":[{"n":1,"cap_mah":149021.4,"ir_mohm":0.881,"cells":[[9,15012.5,8.30], ...]}, ...],
 "unused":[17,23]}
```

## POST /update

Multipart upload of `lfp8.ino.bin` (field name `fw`). Refused with 409 while
any job is running unless `?force=1`. During the update all outputs are off
and the heartbeat is stopped on purpose. On success the board restarts.

## UDP discovery (port 45454)

Every 2 s each board broadcasts (subnet broadcast of STA and/or AP interface):

```json
{"t":"hello","id":1,"ip":"192.168.1.51","fw":"1.0.0"}
```

Peers not heard for 10 s are dropped. A board that hears its own `id` from a
different IP reports `dup_id:true`.
