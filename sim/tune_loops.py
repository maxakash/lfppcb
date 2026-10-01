"""Loop-compensation sweep for the CC / CV error amplifiers.
Run: python3 tune_loops.py  (uses all CPUs). Prints a table; used to pick values
for CC_INT_*, CV_INT_*, G1_CAP, Q2_RE in hardware/gen/lfp8_circuit.py."""
import itertools, sys
from multiprocessing import Pool
import numpy as np
from benches import *

T_EN = 70e-3

def vo_for(cfg):
    return {}

def startup(cfg, soc):
    body = channel_bench(1, chg=f"PWL(0 0 {T_EN} 0 {T_EN+1e-6} 5)", iset=4, cell=dict(soc0=soc), value_override=vo_for(cfg))
    w = run(body, f"tran 2u {T_EN+15e-3} {T_EN-1e-3} 2u", probes(1), name=f"tune_su_{abs(hash(cfg))%10**8}")
    i = i_chg(w); t = w["x"]; m = t > T_EN
    ifin = np.mean(i[t > T_EN + 12e-3])
    bad = np.where(np.abs(i[m] - ifin) > 0.03 * max(ifin, 0.05))[0]
    ts = (t[m][bad[-1]] - T_EN) * 1e3 if len(bad) else 0
    tail = i[t > T_EN + 10e-3]
    return i[m].max(), ifin, ts, tail.max() - tail.min()

def opencv(cfg):
    body = channel_bench(1, chg=f"PWL(0 0 {T_EN} 0 {T_EN+1e-6} 5)", iset=4, cell=None, value_override=vo_for(cfg))
    w = run(body, f"tran 2u {T_EN+20e-3} {T_EN-1e-3} 2u", probes(1), name=f"tune_oc_{abs(hash(cfg))%10**8}")
    v = w["v(bp_1)"]; t = w["x"]
    tail = v[t > T_EN + 15e-3]
    return v[t > T_EN].max(), tail.mean(), tail.max() - tail.min()

def nearfull(cfg):
    # small capacity cell that reaches CV during the run
    body = channel_bench(1, chg=f"PWL(0 0 {T_EN} 0 {T_EN+1e-6} 5)", iset=4,
                         cell=dict(soc0=0.985, qas=54), value_override=vo_for(cfg))
    w = run(body, f"tran 20u {T_EN+1.5} {T_EN-1e-3} 20u", probes(1), name=f"tune_nf_{abs(hash(cfg))%10**8}")
    t = w["x"]; vc = vcell_sense(w); i = i_chg(w)
    m = t > T_EN + 0.05
    di = np.abs(np.diff(i[m]))
    return vc[t > T_EN].max(), i[-1], di.max()

def evaluate(cfg):
    soc, vin = cfg
    try:
        return cfg, startup_v(soc, vin)
    except Exception as e:
        return cfg, str(e)[:200]

def startup_v(soc, vin):
    body = channel_bench(1, chg=f"PWL(0 0 {T_EN} 0 {T_EN+1e-6} 5)", iset=4, vin=vin, cell=dict(soc0=soc))
    w = run(body, f"tran 2u {T_EN+30e-3} {T_EN-1e-3} 2u", probes(1), name=f"su_{soc}_{vin}")
    i = i_chg(w); t = w["x"]; m = t > T_EN
    ifin = np.mean(i[t > T_EN + 25e-3])
    bad = np.where(np.abs(i[m] - ifin) > 0.03 * max(ifin, 0.05))[0]
    ts = (t[m][bad[-1]] - T_EN) * 1e3 if len(bad) else 0
    tail = i[t > T_EN + 20e-3]
    return i[m].max(), ifin, ts, tail.max() - tail.min()

if __name__ == "__main__":
    grid = [(soc, vin) for soc in (0.0, 0.02, 0.2, 0.5, 0.95, 0.99) for vin in (4.75, 5.0, 5.25)]
    with Pool(4) as p:
        for cfg, r in p.imap_unordered(evaluate, grid):
            if isinstance(r, str):
                print(cfg, "ERROR", r); continue
            print(f"soc={cfg[0]:5} vin={cfg[1]:4}: ipk={r[0]:.3f} A  ifinal={r[1]:.3f} A  settle={r[2]:5.2f} ms  ripple={r[3]*1000:.1f} mA", flush=True)
