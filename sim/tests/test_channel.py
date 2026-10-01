"""Functional + adversarial ngspice tests for one LFP-8 channel and the board
safety chain.  Every netlist is generated from hardware/gen/lfp8_circuit.py.

Run:  cd sim && python3 -m pytest -q tests/ -n0
Results: sim/results/summary.json, plots in docs/images/sim_*.png
"""
import os
import re

import numpy as np
import pytest
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

from benches import (channel_bench, probes, i_chg, i_cell, vcell_sense, T_SAFE, T_PWR)
from spicelib import run, n, src, netlist, channel_refs, SUPPORT_REFS, SAFETY_REFS, cell_connection

T_EN = T_SAFE + 10e-3          # firmware enables a channel 10 ms after SAFE_EN
ICELL = "i(v.xcell.vsense)"     # true current into the cell (+ = charging)
CHG_ON = f"PWL(0 0 {T_EN} 0 {T_EN + 1e-6} 5)"


def pulse_on(t_on, t_off=None, v=5):
    if t_off is None:
        return f"PWL(0 0 {t_on} 0 {t_on + 1e-6} {v})"
    return f"PWL(0 0 {t_on} 0 {t_on + 1e-6} {v} {t_off} {v} {t_off + 1e-6} 0)"


def val(w, key, t):
    return float(np.interp(t, w["x"], w[key] if isinstance(key, str) else key))


def plot(path, w, series, title, xlabel="time [ms]", tscale=1e3, xlim=None):
    fig, axes = plt.subplots(len(series), 1, figsize=(8, 2.2 * len(series)), sharex=True)
    if len(series) == 1:
        axes = [axes]
    for ax, (label, items) in zip(axes, series):
        for name, y in items:
            ax.plot(w["x"] * tscale, y, label=name, lw=1.2)
        ax.set_ylabel(label)
        ax.grid(alpha=0.3)
        ax.legend(loc="best", fontsize=8)
    axes[-1].set_xlabel(xlabel)
    if xlim:
        axes[-1].set_xlim(*xlim)
    fig.suptitle(title)
    fig.tight_layout()
    fig.savefig(path, dpi=110)
    plt.close(fig)


# =============================================================== charging
def test_cc_cv_charge_cycle(record, img_dir):
    """Accelerated charge (capacity scaled 1:1000) from 90 % SoC through CC->CV."""
    body = channel_bench(1, chg=CHG_ON, iset=4, cell=dict(soc0=0.90, qas=54))
    w = run(body, f"tran 50u 40 0 20m", probes(1, [ICELL, "v(soc_1)"]), name="t_charge_cycle", timeout=900)
    t, i, vc = w["x"], w[ICELL], vcell_sense(w)
    m = t > T_EN + 0.05
    i_cc = val(w, ICELL, T_EN + 1.0)
    v_max = vc[m].max()
    i_end = i[-1]
    v_end = vc[-1]
    record("cc_cv_charge_cycle", i_cc_A=i_cc, v_cell_max_V=v_max, v_cell_end_V=v_end, i_end_A=i_end,
           soc_end=w["v(soc_1)"][-1])
    plot(os.path.join(img_dir, "sim_charge_cycle.png"), w,
         [("cell V (Kelvin)", [("V_cell", vc)]), ("current [A]", [("I_cell", i)]), ("SoC", [("SoC", w["v(soc_1)"])])],
         "CC/CV charge, ISET=4, capacity scaled 1:1000 (40 s = 11 h real)", xlabel="time [s]", tscale=1)
    assert 0.90 < i_cc < 1.10, "CC current out of range"
    assert v_max <= 3.600, "cell over-charged above 3.60 V"
    assert 3.54 <= v_end <= 3.60, "CV level wrong"
    assert i_end < 0.15, "current did not taper in CV"


def test_iset_dac_table(record):
    """CC current for every ISET code at 3.30 V cell and 5.00 V supply (steady state)."""
    res = []
    for code in range(8):
        body = channel_bench(1, chg=CHG_ON, iset=code, cell=dict(soc0=0.5))
        w = run(body, f"tran 5u {T_EN + 0.06} {T_EN + 0.05} 5u", probes(1, [ICELL]), name=f"t_iset_{code}")
        res.append(float(np.mean(w[ICELL])))
    record("iset_dac_table", **{f"iset{c}_A": v for c, v in enumerate(res)})
    assert abs(res[0]) < 0.01
    assert all(b >= a - 0.005 for a, b in zip(res, res[1:])), "DAC not monotonic"
    assert all(res[c + 1] - res[c] > 0.2 for c in range(3)), "low codes not linear"
    assert 0.85 < res[4] < 1.15
    assert res[7] < 1.95


# =============================================================== discharge
def test_discharge_current(record):
    body = channel_bench(1, dis=pulse_on(T_EN), cell=dict(soc0=0.5))
    w = run(body, f"tran 5u {T_EN + 0.05} {T_EN - 0.002} 5u", probes(1, [ICELL]), name="t_discharge")
    i = val(w, ICELL, T_EN + 0.04)
    v = val(w, vcell_sense(w), T_EN + 0.04)
    record("discharge", i_dis_A=i, v_cell_V=v, r_load_eff_ohm=v / -i)
    assert -1.15 < i < -0.95, "discharge current off nominal"


def test_discharge_hw_uv_backstop(record, img_dir):
    """Firmware 'forgets' to stop: DISEN stays high. Hardware must cut off ~2.24 V
    under load and must not restart while the cell relaxes below 2.59 V."""
    body = channel_bench(1, dis=pulse_on(T_EN), cell=dict(soc0=0.06, qas=2.0))
    w = run(body, f"tran 50u 4 0 2m", probes(1, [ICELL, "v(soc_1)"]), name="t_uv_backstop", timeout=900)
    t, i, vc = w["x"], w[ICELL], vcell_sense(w)
    act = t > T_EN + 5e-3                       # ignore enable/connect spikes
    off = np.where(act & (i > -0.2))[0]
    assert len(off) >= 1, "discharge never stopped"
    j = off[0]
    v_trip = vc[j - 1]
    after = t > t[j] + 0.05
    restarted = bool(np.any(i[after] < -0.2))
    v_rest = vc[-1]
    record("uv_backstop", v_trip_under_load_V=v_trip, v_rest_after_V=v_rest, restarted=restarted)
    plot(os.path.join(img_dir, "sim_uv_backstop.png"), w,
         [("cell V", [("V_cell", vc)]), ("I [A]", [("I_cell", i)])],
         "Hardware UV backstop (DIS_EN stuck on)", xlabel="time [s]", tscale=1)
    assert 2.10 <= v_trip <= 2.40
    assert not restarted, "discharge restarted after UV trip"


def test_discharge_refused_below_rearm(record):
    body = channel_bench(1, dis=pulse_on(T_EN), cell=dict(soc0=0.0))
    w = run(body, f"tran 5u {T_EN + 0.03} {T_EN - 0.002} 5u", probes(1, [ICELL]), name="t_dis_refuse")
    i = val(w, ICELL, T_EN + 0.02)
    v = val(w, vcell_sense(w), T_EN + 0.02)
    record("discharge_refused_low_cell", v_cell_V=v, i_A=i)
    assert abs(i) < 0.01


# =============================================================== IR measurement
def test_ir_measurement_kelvin(record, img_dir):
    """DC-IR exactly as the firmware does it (§7.1). Cell model: R0 = 8 mOhm,
    R1 = 4 mOhm/0.5 s, R2 = 5 mOhm/60 s. Wires + contacts add 160 mOhm of
    force-path resistance that Kelvin sensing must reject."""
    t0 = T_EN + 0.05
    body = channel_bench(1, dis=pulse_on(t0, t0 + 1.2), cell=dict(soc0=0.5))
    w = run(body, f"tran 20u {t0 + 1.3} {t0 - 0.02} 20u",
            probes(1, [ICELL, "v(cellp_1)", "v(cellm_1)", "v(bn_1)"]), name="t_ir", timeout=900)
    vc = vcell_sense(w)
    ish = i_cell(w)
    v0, i0 = val(w, vc, t0 - 0.005), val(w, ish, t0 - 0.005)
    v10, i10 = val(w, vc, t0 + 0.010), val(w, ish, t0 + 0.010)
    v1s, i1s = val(w, vc, t0 + 1.0), val(w, ish, t0 + 1.0)
    r_ohm = (v0 - v10) / (i0 - i10) * 1e3
    r_dc = (v0 - v1s) / (i0 - i1s) * 1e3
    expect_ohm = 8 + 4 * (1 - np.exp(-0.01 / 0.5)) + 5 * (1 - np.exp(-0.01 / 60))
    expect_dc = 8 + 4 * (1 - np.exp(-1 / 0.5)) + 5 * (1 - np.exp(-1 / 60))
    # what a 2-wire meter at the board connector would report
    vforce = w["v(bp_1)"] - w["v(bn_1)"]
    r_2w = (val(w, vforce, t0 - 0.005) - val(w, vforce, t0 + 1.0)) / (i0 - i1s) * 1e3
    record("ir_measurement", r_ohmic_mohm=r_ohm, r_dc_mohm=r_dc, expected_ohmic_mohm=expect_ohm,
           expected_dc_mohm=expect_dc, two_wire_reading_mohm=r_2w, pulse_current_A=-i1s)
    plot(os.path.join(img_dir, "sim_ir_pulse.png"), w,
         [("V_cell (Kelvin)", [("V", vc)]), ("I [A]", [("shunt", ish)])],
         f"DC-IR pulse: R_ohmic={r_ohm:.2f} mOhm, R_dc(1s)={r_dc:.2f} mOhm (2-wire would read {r_2w:.0f} mOhm)",
         xlabel="time [s]", tscale=1)
    assert abs(r_ohm - expect_ohm) < 0.6
    assert abs(r_dc - expect_dc) < 0.6


# =============================================================== interlock
def test_charge_discharge_interlock(record):
    body = channel_bench(1, chg=CHG_ON, dis=pulse_on(T_EN + 0.02), cell=dict(soc0=0.5))
    w = run(body, f"tran 5u {T_EN + 0.05} {T_EN - 0.002} 5u", probes(1, [ICELL]), name="t_interlock")
    ich_before = val(w, i_chg(w), T_EN + 0.015)
    ich_after = val(w, i_chg(w), T_EN + 0.045)
    icell_after = val(w, ICELL, T_EN + 0.045)
    record("interlock", charge_path_before_A=ich_before, charge_path_after_A=ich_after, cell_after_A=icell_after)
    assert ich_before > 0.9
    assert abs(ich_after) < 0.005, "charge path still on while discharging"
    assert icell_after < -0.9


# =============================================================== reverse polarity
@pytest.mark.parametrize("mode", ["idle", "charge", "discharge"])
def test_reverse_polarity(record, img_dir, mode):
    chg = CHG_ON if mode == "charge" else "DC 0"
    dis = pulse_on(T_EN) if mode == "discharge" else "DC 0"
    body = channel_bench(1, chg=chg, dis=dis, cell=dict(soc0=0.5, reverse=True))
    nodes = ["v(cvs_1)", "v(cvp_1)", "v(ovs_1)", "v(ovp_1)", "v(sumd_1)", "v(uvn_1)", "v(ccp_1)", "v(ccn_1)",
             "v(mxa_1)", "v(mxb_1)", "v(mxc_1)", "v(mxe_1)"]
    w = run(body, f"tran 5u {T_EN + 0.1} 0 20u", probes(1, [ICELL] + nodes), name=f"t_reverse_{mode}")
    t = w["x"]
    i = np.abs(w[ICELL])
    i_ss = float(np.max(i[t > T_EN + 0.05]))
    i_pk = float(np.max(i))
    vmin = {k: float(np.min(w[k])) for k in nodes}
    worst = min(vmin.values())
    record(f"reverse_polarity_{mode}", i_steady_A=i_ss, i_peak_A=i_pk, min_ic_input_V=worst,
           giso_V=val(w, "v(giso_1)", t[-1]))
    if mode == "idle":
        plot(os.path.join(img_dir, "sim_reverse_polarity.png"), w,
             [("|I_cell| [A]", [("|I|", i)]), ("V", [("GISO", w["v(giso_1)"]), ("MXA (ADC in)", w["v(mxa_1)"])])],
             "Reversed cell inserted (board powered, SAFE_EN at 60 ms)")
    assert i_ss < 1e-3, "reversed cell is being discharged/charged"
    assert worst > -0.6, "IC input driven too far negative"


# =============================================================== power faults
def test_unpowered_drain(record):
    """PSU disconnected (rail held at 0 V through 1 ohm), cell left in the holder."""
    body = channel_bench(1, cell=dict(soc0=0.5), v5_spec="DC 0", safe="none")
    body = body.replace(f"V3V3 P3V3 0 PWL(0 0 {T_PWR} 3.3)", "V3V3 P3V3 0 DC 0")
    body += "\nVSAFE SAFE_EN 0 DC 0\n"
    w = run(body, "tran 1m 0.5 0.4 1m", probes(1, [ICELL]), name="t_unpowered")
    i = abs(float(np.mean(w[ICELL])))
    record("unpowered_drain", i_drain_uA=i * 1e6, mAh_per_month=i * 1e3 * 24 * 30)
    assert i < 100e-6, "unpowered self-discharge path too large"


def test_brownout_during_charge(record, img_dir):
    """+5 V collapses while charging; real safety chain, heartbeat running."""
    t_drop = T_SAFE + 0.15
    v5 = f"PWL(0 0 {T_PWR} 5.2 {t_drop} 5.2 {t_drop + 0.05} 0)"
    body = channel_bench(1, chg=pulse_on(T_SAFE + 0.03), cell=dict(soc0=0.5), safe="chain", v5_spec=v5,
                         heartbeat="PULSE(0 3.3 0.05 1u 1u 2.5m 5m)")
    w = run(body, f"tran 20u {t_drop + 0.2} 0 20u", probes(1, [ICELL, "v(safe_en)", "v(p5v)"]), name="t_brownout")
    t = w["x"]
    safe = w["v(safe_en)"]
    v5n = w["v(p5v)"]
    fall = np.where((t > t_drop) & (safe < 1.0))[0]
    v_at_trip = float(v5n[fall[0]]) if len(fall) else None
    i_after = w[ICELL][t > t_drop + 0.06]
    record("brownout", rail_V_when_SAFE_dropped=v_at_trip, max_reverse_current_mA=float(-np.min(i_after)) * 1e3,
           i_end_A=float(w[ICELL][-1]))
    plot(os.path.join(img_dir, "sim_brownout.png"), w,
         [("V", [("+5V", v5n), ("SAFE_EN", safe)]), ("I_cell [A]", [("I", w[ICELL])])],
         "Brown-out while charging (real safety chain)")
    assert v_at_trip is not None and 4.3 < v_at_trip < 4.75
    assert -np.min(i_after) < 1e-3, "cell discharges into the dead rail"


@pytest.mark.parametrize("stuck", ["low", "high"])
def test_watchdog_heartbeat_loss(record, img_dir, stuck):
    t_stop = 0.25
    lvl = "0" if stuck == "low" else "3.3"
    hb = f"PULSE(0 3.3 0.05 1u 1u 2.5m 5m)"
    body = channel_bench(1, chg=pulse_on(0.1), cell=dict(soc0=0.5), safe="chain", heartbeat=hb)
    # replace heartbeat with a PWL: toggling until t_stop then stuck
    pts = ["0 0"]
    tt, state = 0.05, 0
    while tt < t_stop:
        state ^= 1
        pts.append(f"{tt:.6f} {3.3 * (1 - state)}")
        pts.append(f"{tt + 1e-6:.6f} {3.3 * state}")
        tt += 2.5e-3
    pts.append(f"{t_stop + 1e-5:.6f} {lvl}")
    body = re.sub(r"^VHB .*$", f"VHB HEARTBEAT 0 PWL({' '.join(pts)})", body, flags=re.M)
    w = run(body, f"tran 20u {t_stop + 0.25} 0 50u", probes(1, [ICELL, "v(safe_en)", "v(pq)", "v(oe_n)"]),
            name=f"t_watchdog_{stuck}")
    t = w["x"]
    safe = w["v(safe_en)"]
    up = np.where(safe > 4.0)[0]
    t_up = float(t[up[0]]) if len(up) else None
    dn = np.where((t > t_stop) & (safe < 1.0))[0]
    t_trip = float(t[dn[0]] - t_stop) if len(dn) else None
    i_after = float(w[ICELL][-1])
    record(f"watchdog_{stuck}", safe_en_rise_s=t_up, trip_delay_ms=None if t_trip is None else t_trip * 1e3,
           i_after_A=i_after, oe_n_after_V=float(w["v(oe_n)"][-1]))
    if stuck == "low":
        plot(os.path.join(img_dir, "sim_watchdog.png"), w,
             [("V", [("SAFE_EN", safe), ("pump PQ", w["v(pq)"])]), ("I_cell [A]", [("I", w[ICELL])])],
             "Watchdog: heartbeat stops at 250 ms")
    assert t_up is not None and t_up < 0.2
    assert t_trip is not None and t_trip < 0.12
    assert abs(i_after) < 1e-3
    assert w["v(oe_n)"][-1] > 4.0


def test_watchdog_frequency_window(record):
    """Which heartbeat rates keep SAFE_EN high (firmware requirement >= 100 Hz)."""
    out = {}
    for f in (20, 50, 100, 200, 1000):
        half = 0.5 / f
        body = channel_bench(1, cell=dict(soc0=0.5), safe="chain", heartbeat=f"PULSE(0 3.3 0.05 1u 1u {half} {2 * half})")
        w = run(body, "tran 50u 0.6 0.4 50u", probes(1, ["v(safe_en)"]), name=f"t_wdf_{f}")
        out[f"{f}Hz_safe_min_V"] = float(np.min(w["v(safe_en)"]))
    record("watchdog_frequency", **out)
    assert out["100Hz_safe_min_V"] > 4.0 and out["1000Hz_safe_min_V"] > 4.0
    assert out["20Hz_safe_min_V"] < 1.0, "watchdog accepts a too-slow heartbeat"


def test_rail_overvoltage(record):
    t_ov = 0.25
    v5 = f"PWL(0 0 {T_PWR} 5.2 {t_ov} 5.2 {t_ov + 1e-3} 6.0 {t_ov + 0.05} 6.0 {t_ov + 0.051} 5.2)"
    body = channel_bench(1, chg=pulse_on(0.1), cell=dict(soc0=0.5), safe="chain", v5_spec=v5)
    w = run(body, f"tran 20u {t_ov + 0.2} 0 20u", probes(1, [ICELL, "v(safe_en)"]), name="t_rail_ov")
    t = w["x"]
    safe_during = float(np.max(w["v(safe_en)"][(t > t_ov + 0.005) & (t < t_ov + 0.05)]))
    i_during = float(np.max(np.abs(w[ICELL][(t > t_ov + 0.01) & (t < t_ov + 0.05)])))
    safe_after = float(w["v(safe_en)"][-1])
    record("rail_ov_6V", safe_en_during_V=safe_during, cell_current_during_A=i_during, safe_en_recovered_V=safe_after)
    assert safe_during < 1.0
    assert i_during < 1e-3
    assert safe_after > 4.0


@pytest.mark.parametrize("temp", [25, 70, 95])
def test_board_overtemperature(record, temp):
    body = channel_bench(1, cell=dict(soc0=0.5), safe="chain")
    w = run(body, "tran 50u 0.3 0.25 50u", probes(1, ["v(safe_en)", "v(ts)"]), name=f"t_ot_{temp}",
            options=f"method=gear reltol=1e-3 rshunt=1e12 cshunt=1e-12 temp={temp} tnom=25")
    s = float(np.mean(w["v(safe_en)"]))
    record(f"overtemp_{temp}C", safe_en_V=s, ts_V=float(np.mean(w["v(ts)"])))
    if temp <= 70:
        assert s > 4.0
    else:
        assert s < 1.0


def test_estop(record):
    body = channel_bench(1, chg=pulse_on(0.1), cell=dict(soc0=0.5), safe="chain")
    body += "\nVES esc 0 PWL(0 0 0.2 0 0.2001 1)\nSES SAFE_EN 0 esc 0 ESMOD\n.model ESMOD SW(Ron=0.1 Roff=1G Vt=0.5 Vh=0.05)\n"
    w = run(body, "tran 10u 0.25 0.15 10u", probes(1, [ICELL, "v(safe_en)", "v(giso_1)"]), name="t_estop")
    t = w["x"]
    g = w["v(giso_1)"]
    dn = np.where((t > 0.2) & (g < 0.5))[0]
    t_iso = float(t[dn[0]] - 0.2) if len(dn) else None
    record("estop", isolation_delay_us=None if t_iso is None else t_iso * 1e6, i_after_A=float(w[ICELL][-1]))
    assert t_iso is not None and t_iso < 1e-3
    assert abs(w[ICELL][-1]) < 1e-3


# =============================================================== component failures
def test_pass_fet_shorted(record, img_dir):
    """Q101 fails short while charging an almost full cell: CV loop is powerless,
    the differential OV latch must isolate the cell."""
    body = channel_bench(1, chg=CHG_ON, cell=dict(soc0=0.97, qas=30), remove=("Q101",))
    body += "\nRshortQ101 P5V CH1_1 0.02\n"
    w = run(body, "tran 50u 6 0 5m", probes(1, [ICELL, "v(soc_1)"]), name="t_q1_short", timeout=900)
    vc = vcell_sense(w)
    t = w["x"]
    vmax = float(np.max(vc))
    i_end = float(w[ICELL][-1])
    record("pass_fet_short", v_cell_max_V=vmax, i_end_A=i_end, ovo_end_V=float(w["v(ovo_1)"][-1]),
           giso_end_V=float(w["v(giso_1)"][-1]))
    plot(os.path.join(img_dir, "sim_pass_fet_short.png"), w,
         [("V_cell", [("V", vc)]), ("I [A]", [("I", w[ICELL])]), ("V", [("OVO", w["v(ovo_1)"]), ("GISO", w["v(giso_1)"])])],
         "Pass MOSFET shorted: OV latch isolates the cell", xlabel="time [s]", tscale=1)
    assert vmax < 3.90
    assert abs(i_end) < 2e-3


@pytest.mark.parametrize("which", ["p", "n"])
def test_open_kelvin_sense(record, which):
    cell = dict(soc0=0.95, qas=30)
    cell["open_sense_" + which] = True
    body = channel_bench(1, chg=CHG_ON, cell=cell)
    w = run(body, "tran 50u 20 0 10m", probes(1, [ICELL, "v(cellp_1)", "v(cellm_1)"]), name=f"t_open_sense_{which}",
            timeout=900)
    v_true = w["v(cellp_1)"] - w["v(cellm_1)"]
    vmax = float(np.max(v_true))
    record(f"open_sense_{which}", v_cell_true_max_V=vmax, i_end_A=float(w[ICELL][-1]))
    assert vmax <= 3.62


def test_board_side_short(record, img_dir):
    """BP shorted to GND on the board (solder bridge / damaged part) with a cell in."""
    t_s = T_EN
    body = channel_bench(1, cell=dict(soc0=0.5))
    body += f"\nVSH shc 0 PWL(0 0 {t_s} 0 {t_s + 1e-6} 1)\nSSH BP_1 0 shc 0 SHMOD\n.model SHMOD SW(Ron=2m Roff=1G Vt=0.5 Vh=0.05)\n"
    w = run(body, f"tran 20u {t_s + 3} {t_s - 0.01} 1m", probes(1, [ICELL]), name="t_board_short", timeout=900)
    i = -w[ICELL]
    t = w["x"]
    pk = float(np.max(i))
    jpk = int(np.argmax(i))
    below = np.where((t > t[jpk]) & (i < 0.5))[0]
    t_trip = float(t[below[0]] - t_s) if len(below) else None
    record("board_short", i_peak_A=pk, ptc_trip_s=t_trip, i_end_A=float(i[-1]))
    plot(os.path.join(img_dir, "sim_board_short.png"), w, [("I_cell out [A]", [("I", i)])],
         "B+ shorted to GND on the board: PTC trips", xlabel="time [s]", tscale=1)
    assert t_trip is not None and t_trip < 2.0
    assert i[-1] < 0.3


def test_hotplug_while_charging(record):
    """Cell inserted into a slot whose charger is already enabled."""
    t_in = T_EN + 0.05
    body = channel_bench(1, chg=CHG_ON, cell=dict(soc0=0.3, switch="plug"))
    body += f"\nVPLUG plug 0 PWL(0 0 {t_in} 0 {t_in + 1e-6} 1)\n"
    w = run(body, f"tran 5u {t_in + 0.08} {T_EN - 0.002} 5u", probes(1, [ICELL]), name="t_hotplug")
    t = w["x"]
    ipk = float(np.max(w[ICELL][t > t_in]))
    i_end = float(w[ICELL][-1])
    bp_open = float(np.max(w["v(bp_1)"][(t > T_EN) & (t < t_in)]))
    record("hotplug", i_inrush_peak_A=ipk, i_settled_A=i_end, bp_open_circuit_max_V=bp_open,
           ov_latched=bool(w["v(ovo_1)"][-1] < 1))
    assert ipk < 2.5, "inrush too high"


def test_cell_removed_while_charging(record):
    t_out = T_EN + 0.05
    t_off = t_out + 0.05      # firmware notices removal and drops CHG_EN
    body = channel_bench(1, chg=f"PWL(0 0 {T_EN} 0 {T_EN + 1e-6} 5 {t_off} 5 {t_off + 1e-6} 0)",
                         cell=dict(soc0=0.5, switch="plug"))
    body += f"\nVPLUG plug 0 PWL(0 0 {t_out} 1 {t_out + 1e-6} 0)\n"
    w = run(body, f"tran 5u {t_off + 0.4} {T_EN - 0.002} 20u", probes(1, [ICELL]), name="t_removal")
    t = w["x"]
    bp_max = float(np.max(w["v(bp_1)"][t > t_out]))
    ovo_end = float(w["v(ovo_1)"][-1])
    record("cell_removed", bp_max_V=bp_max, ov_latch_released=bool(ovo_end > 3))
    assert bp_max < 5.35, "open-circuit output above the rail"
    assert ovo_end > 3, "OV latch stuck after removal"


def test_sr_outputs_high_z(record):
    """74HC595 outputs high-Z (OE disabled): channel must stay off."""
    body = channel_bench(1, chg="DC 0", dis="DC 0", cell=dict(soc0=0.5))
    body = re.sub(r"^VCHG .*$", "RCHGZ CHGEN_1 0 1G", body, flags=re.M)
    body = re.sub(r"^VDIS .*$", "RDISZ DISEN_1 0 1G", body, flags=re.M)
    w = run(body, f"tran 50u {T_EN + 0.05} {T_EN} 50u", probes(1, [ICELL]), name="t_sr_hiz")
    i = float(np.max(np.abs(w[ICELL])))
    record("sr_high_z", i_cell_A=i)
    assert i < 1e-3


# =============================================================== Monte Carlo
def _mc_values(rng, refs_tol):
    vo = {}
    for ref, nominal, tol in refs_tol:
        vo[ref] = f"{nominal * (1 + rng.uniform(-tol, tol)):.6g}"
    return vo


def _parse_ohm(v):
    v = v.replace("k", "e3").replace("M", "e6")
    return float(v)


def test_monte_carlo_cv_ov_uv(record, img_dir):
    """Resistors +-1 %, VREF +-0.5 %, LM324 Vos +-7 mV (truncated normal, s=2.3 mV)."""
    rng = np.random.default_rng(42)
    N = int(os.environ.get("MC_RUNS", "60"))
    cv_refs = [("R115", 47e3, .01), ("R116", 30e3, .01), ("R141", 47e3, .01), ("R142", 33e3, .01), ("R143", 360e3, .01)]
    ov_refs = [("R128", 68e3, .01), ("R129", 33e3, .01), ("R147", 68e3, .01), ("R130", 62e3, .01), ("R148", 100e3, .01),
               ("R131", 240e3, .01)]
    cv, ovt = [], []
    for k in range(N):
        vo = _mc_values(rng, cv_refs + ov_refs)
        vos = np.clip(rng.normal(0, 2.3e-3, 4), -7e-3, 7e-3)
        params = {"U101": {f"vos{j + 1}": f"{vos[j]:.6g}" for j in range(4)},
                  "U2": {"vref0": f"{2.495 * (1 + rng.uniform(-0.005, 0.005)):.6g}"}}
        # CV: open-circuit regulation level (no cell)
        body = channel_bench(1, chg=CHG_ON, cell=None, value_override=vo, params=params)
        w = run(body, f"tran 20u {T_EN + 0.05} {T_EN + 0.04} 20u", probes(1), name=f"mc_cv_{k}")
        cv.append(float(np.mean(w["v(bps_1)"] - w["v(bns_1)"])))
        # OV trip: ideal source as 'cell', ramp 3.60 -> 3.95 V
        body = channel_bench(1, cell=None, value_override=vo, params=params)
        body += (f"\nVRAMP BPS_1 BNS_1 PWL(0 3.5 {T_EN} 3.6 {T_EN + 0.35} 3.95)\nRBN BNS_1 0 1m\n")
        w = run(body, f"tran 50u {T_EN + 0.36} {T_EN} 50u", probes(1), name=f"mc_ov_{k}")
        vo_ = w["v(ovo_1)"]
        j = np.where(vo_ < 1.5)[0]
        ovt.append(float((w["v(bps_1)"] - w["v(bns_1)"])[j[0]]) if len(j) else float("nan"))
    cv, ovt = np.array(cv), np.array(ovt)
    record("monte_carlo", runs=N, cv_mean_V=cv.mean(), cv_std_mV=cv.std() * 1e3, cv_min_V=cv.min(), cv_max_V=cv.max(),
           ov_mean_V=np.nanmean(ovt), ov_min_V=np.nanmin(ovt), ov_max_V=np.nanmax(ovt),
           margin_ov_minus_cv_mV=(np.nanmin(ovt) - cv.max()) * 1e3)
    fig, ax = plt.subplots(1, 2, figsize=(9, 3))
    ax[0].hist(cv, bins=20); ax[0].set_title("CV set-point [V]"); ax[0].axvline(3.65, color="r")
    ax[1].hist(ovt, bins=20); ax[1].set_title("OV latch trip [V]")
    fig.suptitle(f"Monte Carlo, {N} runs (R 1 %, VREF 0.5 %, Vos 7 mV)")
    fig.tight_layout(); fig.savefig(os.path.join(img_dir, "sim_monte_carlo.png"), dpi=110); plt.close(fig)
    assert cv.max() <= 3.65 and cv.min() >= 3.50
    assert np.nanmin(ovt) > cv.max() + 0.1, "OV latch too close to CV"
    assert np.nanmax(ovt) < 3.95


# =============================================================== thermal budget
def test_thermal_budget(record):
    """Worst-case per-part dissipation.
    Charge: supply 5.25 V (max), cell at 3.05 V (firmware runs continuous CC only
    above 3.05 V; below it CHG_EN is pulsed at <= 50 % duty, checked at 2.85 V).
    Discharge: full cell (3.6 V at start)."""
    out = {}
    for tag, soc, duty in (("cont_3V05", 0.035, 1.0), ("pulsed_2V85", 0.015, 0.5)):
        body = channel_bench(1, chg=CHG_ON, iset=4, vin=5.25, cell=dict(soc0=soc))
        w = run(body, f"tran 20u {T_EN + 0.05} {T_EN + 0.04} 20u", probes(1, ["v(p5v)", "v(ch1_1)"]), name=f"t_thermal_{tag}")
        ich = i_chg(w)
        out[f"{tag}_Vcell_V"] = float(np.mean(vcell_sense(w)))
        out[f"{tag}_I_A"] = float(np.mean(ich))
        out[f"{tag}_P_Q1_avg_W"] = duty * float(np.mean((w["v(p5v)"] - w["v(ch1_1)"]) * ich))
        out[f"{tag}_P_D1_avg_W"] = duty * float(np.mean((w["v(ch1_1)"] - w["v(ch2_1)"]) * ich))
        out[f"{tag}_P_Rch_avg_W"] = duty * float(np.mean((w["v(ch2_1)"] - w["v(bp_1)"]) * ich))
    body = channel_bench(1, dis=pulse_on(T_EN), cell=dict(soc0=1.03))
    w2 = run(body, f"tran 20u {T_EN + 0.05} {T_EN + 0.04} 20u", probes(1, [ICELL]), name="t_thermal_dis")
    idis = -float(np.mean(w2[ICELL]))
    v_load = float(np.mean(w2["v(bp_1)"] - w2["v(dload_1)"]))
    p_rdis_each = v_load ** 2 / 11.0
    p_q4 = float(np.mean(w2["v(dload_1)"])) * idis
    out.update(discharge_I_A=idis, P_Rload_each_W=p_rdis_each, P_Q4_W=p_q4, P_shunt_W=idis ** 2 * 0.1,
               P_channel_discharge_W=v_load * idis + p_q4, P_board_8ch_discharge_W=8 * (v_load * idis + p_q4))
    record("thermal", **out)
    for tag in ("cont_3V05", "pulsed_2V85"):
        assert out[f"{tag}_P_Q1_avg_W"] < 0.6, "pass FET too hot for SOT-23"
        assert out[f"{tag}_P_Rch_avg_W"] < 1.3, "ballast above 65 % of its 2 W rating"
    assert p_rdis_each < 1.3, "2 W load resistors above 65 % rating"
    assert p_q4 < 0.15


# =============================================================== small-signal stability
def test_loop_stability(record, img_dir):
    """CC loop phase margin over cell voltage (2.5-3.45 V), supply (4.75-5.25 V) and
    ISET (2/4/7); CV loop near the CV point. Corners where the loop is saturated
    (current-limited by the passive path) have no crossover and are skipped."""
    from loopgain import loop_gain
    worst_cc, fcs, rows = 999.0, [], []
    for soc in (0.0, 0.02, 0.5, 0.99):
        for vin in (4.75, 5.0, 5.2, 5.25):
            for iset in (2, 4, 7):
                r = loop_gain(soc, {}, "cc", vin=vin, iset=iset, name=f"lg_cc_{soc}_{vin}_{iset}")
                if r["pm"] is None:
                    assert r["mag"].max() < 0, f"no crossover but gain > 0 dB at {soc},{vin},{iset}"
                    continue
                worst_cc = min(worst_cc, r["pm"])
                fcs.append(r["fc"])
                rows.append((soc, vin, iset, r["fc"], r["pm"]))
    worst_cv, cv_rows = 999.0, []
    # CV loop: only active in a narrow window just below the CV point; the loop gain grows
    # with cell resistance, so aged (30 / 100 mOhm) cells are the stability worst case.
    for r0 in ("8m", "30m", "100m"):
        for soc in np.arange(1.004, 1.0128, 0.0012):
            for vin in (4.75, 5.2):
                r = loop_gain(float(soc), {}, "cv", vin=vin, iset=4, name=f"lg_cv_{r0}_{soc:.4f}_{vin}", r0=r0)
                if r["pm"] is None:
                    continue
                worst_cv = min(worst_cv, r["pm"])
                cv_rows.append((r0, soc, vin, r["fc"], r["pm"]))
    record("loop_stability", cc_worst_pm_deg=worst_cc, cc_fc_min_Hz=min(fcs), cc_fc_max_Hz=max(fcs),
           cc_corners=len(rows), cv_worst_pm_deg=worst_cv, cv_corners=len(cv_rows))
    # Bode plot of the nominal CC loop
    r = loop_gain(0.5, {}, "cc", vin=5.2, iset=3, name="lg_cc_plot")
    fig, ax = plt.subplots(2, 1, figsize=(7, 4.5), sharex=True)
    ax[0].semilogx(r["f"], r["mag"]); ax[0].axhline(0, color="k", lw=0.5); ax[0].set_ylabel("|T| [dB]")
    ax[1].semilogx(r["f"], np.unwrap(np.radians(r["ph"])) * 180 / np.pi); ax[1].axhline(-180, color="r", lw=0.5)
    ax[1].set_ylabel("phase [deg]"); ax[1].set_xlabel("f [Hz]")
    fig.suptitle(f"CC loop gain (3.3 V cell, 5.2 V, ISET 3): fc={r['fc']:.0f} Hz, PM={r['pm']:.0f} deg")
    fig.tight_layout(); fig.savefig(os.path.join(img_dir, "sim_cc_loop_bode.png"), dpi=110); plt.close(fig)
    assert worst_cc >= 45, f"CC phase margin {worst_cc:.1f} deg"
    assert len(cv_rows) > 0 and worst_cv >= 45, f"CV phase margin {worst_cv:.1f} deg"


def test_empty_slot(record):
    """No cell fitted: the channel must read ~0 V (EMPTY, not REVERSED) and stay connected
    (isolation on, so B- sense is tied to ground); with the charger enabled the open
    terminals must regulate at CV without sustained oscillation."""
    body = channel_bench(1, chg=pulse_on(T_EN + 0.05), cell=None)
    w = run(body, f"tran 20u {T_EN + 0.15} {T_EN - 0.02} 20u", probes(1), name="t_empty_slot")
    t = w["x"]
    v_idle = float(np.interp(T_EN + 0.04, t, w["v(bps_1)"] - w["v(bns_1)"]))
    gen_idle = float(np.interp(T_EN + 0.04, t, w["v(giso_1)"]))
    tail = (w["v(bps_1)"] - w["v(bns_1)"])[t > T_EN + 0.12]
    record("empty_slot", v_reading_idle_V=v_idle, giso_idle_V=gen_idle, v_open_charging_V=float(tail.mean()),
           ripple_open_mV=float((tail.max() - tail.min()) * 1e3))
    assert abs(v_idle) < 0.5, "empty slot not read as EMPTY"
    assert gen_idle > 4.0
    assert 3.4 < tail.mean() < 3.7
    assert tail.max() - tail.min() < 0.05


def test_reverse_cell_hotplugged_while_charging(record):
    """Worst case: charger running into an empty slot (output at CV), then a cell is
    pushed in backwards."""
    t_in = T_EN + 0.05
    body = channel_bench(1, chg=CHG_ON, cell=dict(soc0=0.5, reverse=True, switch="plug"))
    body += f"\nVPLUG plug 0 PWL(0 0 {t_in} 0 {t_in + 1e-6} 1)\n"
    w = run(body, f"tran 2u {t_in + 0.1} {t_in - 0.005} 2u", probes(1, [ICELL]), name="t_reverse_hotplug")
    t = w["x"]
    i = np.abs(w[ICELL])
    m = t > t_in
    pk = float(np.max(i[m]))
    above = np.where(m & (i > 0.05))[0]
    dur = float(t[above[-1]] - t_in) if len(above) else 0.0
    i_ss = float(np.max(i[t > t_in + 0.05]))
    # charge drawn from the cell during the event
    q_mC = float(np.trapezoid(i[m], t[m]) * 1e3)
    record("reverse_hotplug_charging", i_peak_A=pk, duration_above_50mA_ms=dur * 1e3, i_steady_A=i_ss, charge_mC=q_mC)
    assert i_ss < 1e-3
    assert dur < 2e-3, "reverse current lasted too long"
