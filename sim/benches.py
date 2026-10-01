"""Reusable ngspice testbenches for one LFP-8 channel (+ board support circuits).

Every bench powers the board up from 0 V (realistic initial state of the
latching comparators), then applies firmware-like stimuli.
"""
import numpy as np
from spicelib import (netlist, channel_refs, SUPPORT_REFS, SAFETY_REFS, n, src, run,
                      cell_connection, at)

T_PWR = 1e-3        # +5 V ramp time
T_SAFE = 60e-3     # SAFE_EN asserted (firmware boot; VREF settled)


def iset_sources(code):
    b = [(code >> i) & 1 for i in range(3)]
    return "\n".join(src(f"B{i}", n(f"ISETB{i}"), f"PWL(0 0 {T_PWR} {5 * b[i]})") for i in range(3))


def channel_bench(k=1, *, chg="0", dis="0", iset=4, vin=5.2, cell=None, safe="source",
                  extra="", value_override=None, params=None, remove=(), v5_spec=None,
                  heartbeat=None, with_safety=False):
    """Return a netlist body for channel k.

    chg / dis : SPICE source specs (e.g. 'PWL(0 0 2m 0 2.001m 5)') for CHGEN_k / DISEN_k
    cell      : dict of cell_connection() kwargs, or None for no cell
    safe      : 'source' -> SAFE_EN driven by a source, 'chain' -> real safety chain
    """
    refs = channel_refs(k) + SUPPORT_REFS
    if with_safety or safe == "chain":
        refs += SAFETY_REFS
    parts = [netlist(refs, params=params, value_override=value_override, remove=remove)]
    v5 = v5_spec or f"PWL(0 0 {T_PWR} {vin})"
    parts.append(src("5V", n("+5V"), v5))
    parts.append(src("3V3", n("+3V3"), f"PWL(0 0 {T_PWR} 3.3)"))
    if safe == "source":
        parts.append(src("SAFE", n("SAFE_EN"), f"PWL(0 0 {T_SAFE} 0 {T_SAFE + 1e-6} 4.6)"))
    elif safe == "chain":
        hb = heartbeat or f"PULSE(0 3.3 {T_SAFE} 1u 1u 2.5m 5m)"
        parts.append(src("HB", n("HEARTBEAT"), hb))
    parts.append(src("CHG", n(f"CHGEN_{k}"), chg))
    parts.append(src("DIS", n(f"DISEN_{k}"), dis))
    parts.append(src("LED", n(f"LED_{k}"), "DC 0"))
    parts.append(iset_sources(iset))
    if cell is not None:
        parts.append(cell_connection(k, **cell))
    else:
        # no cell: connector pins float
        parts.append(f"Rntc_{k} {n(f'NTC_{k}')} 0 10k")
    parts.append(extra)
    return "\n".join(parts)


def i_chg(w, k=1):
    return (w[f"v(ch2_{k})"] - w[f"v(bp_{k})"]) / 1.2


def i_cell(w, k=1):
    """Cell current from the shunt (positive = charging)."""
    return (w[f"v(shpk_{k})"] - w[f"v(shnk_{k})"]) / 0.100


def vcell_sense(w, k=1):
    return w[f"v(bps_{k})"] - w[f"v(bns_{k})"]


STD_PROBES = ["v(bp_{k})", "v(ch2_{k})", "v(g1_{k})", "v(cco_{k})", "v(cvo_{k})", "v(nb_{k})",
              "v(bps_{k})", "v(bns_{k})", "v(shpk_{k})", "v(shnk_{k})", "v(giso_{k})", "v(ddrv_{k})",
              "v(ovo_{k})", "v(dload_{k})", "v(vs)", "v(vref)"]


def probes(k=1, extra=()):
    return [p.format(k=k) for p in STD_PROBES] + list(extra)


def window(w, key, t0, t1):
    m = (w["x"] >= t0) & (w["x"] <= t1)
    return w[key][m] if isinstance(key, str) else key[m]
