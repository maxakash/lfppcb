"""Sweep CC-loop compensation with small-signal loop gain over operating corners."""
import itertools, sys
from multiprocessing import Pool
from loopgain import loop_gain

CORNERS = [(soc, vin, iset) for soc in (0.0, 0.02, 0.5, 0.99) for vin in (4.75, 5.25) for iset in (2, 4, 7)]

def ev(cfg):
    c05, r14, r05, c09 = cfg
    ov = {"C105": c05, "C110": c05, "R114": r14, "R105": r05, "C109": c09, "C106": "10n", "C111": "10n", "R117": "10k", "C103": "1u"}
    worst = (999, None)
    fcs = []
    for soc, vin, iset in CORNERS:
        try:
            r = loop_gain(soc, ov, "cc", vin=vin, iset=iset, name=f"lg_{abs(hash((cfg, soc, vin, iset))) % 10**9}")
        except Exception as e:
            return cfg, None, str(e)[:100]
        if r["pm"] is None:
            # no crossover: loop saturated (current-limited by passive path) -> not regulating, skip
            if r["mag"].max() < 0:
                continue
            pm = -999
        else:
            pm = r["pm"]
        if pm < worst[0]:
            worst = (pm, (soc, vin, iset, r["fc"]))
        if r["fc"]: fcs.append(r["fc"])
    return cfg, worst, (min(fcs) if fcs else None, max(fcs) if fcs else None)

if __name__ == "__main__":
    grid = list(itertools.product(["47n", "100n", "220n"], ["0", "470", "1k"], ["1k", "2.2k"], ["1n", "10n"]))
    with Pool(4) as p:
        for cfg, worst, extra in p.imap_unordered(ev, grid):
            print(" ".join(cfg), "->", worst, extra, flush=True)
