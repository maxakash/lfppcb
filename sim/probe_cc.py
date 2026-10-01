"""Quick exploratory run: channel 1 charging a cell held at fixed SoC."""
import sys
from spicelib import *

k = 1
body = "\n".join([
    netlist(channel_refs(k) + SUPPORT_REFS),
    src("5V", n("+5V"), "DC 5.0"),
    src("3V3", n("+3V3"), "DC 3.3"),
    src("SAFE", n("SAFE_EN"), "DC 4.6"),
    src("CHG", n("CHGEN_1"), "PULSE(0 5 1m 1u 1u 1 2)"),
    src("DIS", n("DISEN_1"), "DC 0"),
    src("LED", n("LED_1"), "DC 0"),
    src("B0", n("ISETB0"), "DC 0"), src("B1", n("ISETB1"), "DC 0"), src("B2", n("ISETB2"), "DC 5"),
    cell_connection(k, soc0=float(sys.argv[1]) if len(sys.argv) > 1 else 0.5),
])
w = run(body, ".tran 2u 30m 0 2u", ["v(bp_1)", "i(vsense)" if False else "v(ch2_1)", "v(g1_1)", "v(cco_1)", "v(cvo_1)", "v(vs)", "v(bps_1)", "v(bns_1)", "v(giso_1)", "v(nb_1)"], name="probe_cc")
import numpy as np
for t in [0.5e-3, 1.5e-3, 3e-3, 5e-3, 10e-3, 20e-3, 29e-3]:
    vals = {k2: at(w, k2, t) for k2 in w if k2 != "x"}
    ich = (vals["v(ch2_1)"] - vals["v(bp_1)"]) / 0.75
    print(f"t={t*1e3:5.1f}ms Ich={ich:6.3f}A  " + " ".join(f"{k2}={v:.3f}" for k2, v in vals.items()))
