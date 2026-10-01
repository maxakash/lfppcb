"""ngspice harness for the LFP-8 board.

Builds testbench netlists directly from the board netlist in
hardware/gen/lfp8_circuit.py (single source of truth), runs ngspice in batch
mode and returns waveforms as numpy arrays.
"""
import os
import re
import subprocess
import sys
import tempfile
import uuid

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "hardware", "gen"))
from lfp8_circuit import build_board  # noqa: E402
from circuit import spice_net  # noqa: E402

MODELS = os.path.join(ROOT, "sim", "models", "lfp8_models.lib")
RESULTS = os.path.join(ROOT, "sim", "results")

# board-level parts that every channel testbench needs (references, VUV, VS)
SUPPORT_REFS = ["U2", "R1", "C8", "R4", "R5", "R6", "R7", "U3", "C11"]
# the hardware safety chain (SAFE_EN generation)
SAFETY_REFS = ["U4", "U5", "C14", "D7", "D8", "C15", "R8", "R9", "R10", "R11", "R12", "R46",
               "C16", "R13", "R14", "C17", "Q7", "R15", "C18", "R16", "R17", "R18", "Q8", "R20",
               "R21", "D9", "R22", "R23"]

_BOARD = None


def board():
    global _BOARD
    if _BOARD is None:
        _BOARD = build_board()
    return _BOARD


def channel_refs(k=1, exclude=("J",)):
    return [i.ref for i in board().insts if i.group == f"ch{k}" and not i.ref.startswith(exclude)]


def n(name):
    """SPICE node name for a board net."""
    return spice_net(name)


def netlist(refs, params=None, value_override=None, remove=()):
    """SPICE lines for the given references. value_override: {ref: 'value'}."""
    b = board()
    saved = {}
    if value_override:
        for ref, v in value_override.items():
            inst = b.get(ref)
            saved[ref] = inst.value
            inst.value = v
    try:
        refs = [r for r in refs if r not in remove]
        txt = b.spice(refs=set(refs), extra_params=params)
    finally:
        for ref, v in saved.items():
            b.get(ref).value = v
    return txt


def cell_connection(k=1, cell="XCELL", reverse=False, r_force=0.035, r_sense=0.045,
                    qas=54000, soc0=0.5, r0="8m", ntc=True, present=True, switch=None,
                    open_sense_p=False, open_sense_n=False, r1="4m", r2="5m"):
    """Connect an LFPCELL model to channel k's connector nets through wire/contact
    resistances (force: wire+contact, sense: thin sense wire).
    switch: control node name -> all four wires go through a switch closed when V(ctrl)>0.5."""
    p, m = (f"cellm_{k}", f"cellp_{k}") if reverse else (f"cellp_{k}", f"cellm_{k}")
    lines = []
    if present:
        lines.append(f"{cell} {p} {m} soc_{k} LFPCELL params: QAS={qas} SOC0={soc0} R0={r0} R1={r1} R2={r2}")
    wires = [("wf_p", f"cellp_{k}", n(f'BPF_{k}'), r_force, False),
             ("ws_p", f"cellp_{k}", n(f'BPS_{k}'), r_sense, open_sense_p),
             ("ws_n", f"cellm_{k}", n(f'BNS_{k}'), r_sense, open_sense_n),
             ("wf_n", f"cellm_{k}", n(f'BN_{k}'), r_force, False)]
    for name, a, b, rv, is_open in wires:
        if is_open:
            continue
        if switch:
            lines.append(f"R{name}{k} {a} {name}_{k}_sw {rv}")
            lines.append(f"S{name}{k} {name}_{k}_sw {b} {switch} 0 SWMOD")
        else:
            lines.append(f"R{name}{k} {a} {b} {rv}")
    if switch:
        lines.append(".model SWMOD SW(Ron=1m Roff=1G Vt=0.5 Vh=0.05)")
    if not present:
        lines.append(f"Rfloat_{k} cellp_{k} cellm_{k} 1G")
    lines.append(f"Rfloat2_{k} cellm_{k} 0 1G")
    if ntc:
        lines.append(f"Rntc_{k} {n(f'NTC_{k}')} 0 10k")
    return "\n".join(lines)


def run(body, analyses, probes, name="tb", timeout=600, options=""):
    """Run ngspice. analyses: '.tran ...' line(s). probes: list of vector expressions.
    Returns dict expr -> numpy array, plus 'time' or 'sweep'."""
    os.makedirs(RESULTS, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="lfp8_")
    out = os.path.join(tmp, "out.txt")
    cir = os.path.join(tmp, f"{name}.cir")
    probe_str = " ".join(probes)
    analyses = "\n".join(l.strip().lstrip(".") for l in analyses.strip().splitlines())
    deck = f"""* {name}
.include {MODELS}
.options {options or 'method=gear reltol=1e-3 itl1=500 itl4=100 rshunt=1e12 cshunt=1e-12'}
{body}
.control
set filetype=ascii
set wr_singlescale
set wr_vecnames
{analyses}
wrdata {out} {probe_str}
quit
.endc
.end
"""
    with open(cir, "w") as f:
        f.write(deck)
    r = subprocess.run(["ngspice", "-b", cir], capture_output=True, text=True, timeout=timeout)
    log = r.stdout + r.stderr
    with open(os.path.join(RESULTS, f"{name}.log"), "w") as f:
        f.write(log)
    with open(os.path.join(RESULTS, f"{name}.cir"), "w") as f:
        f.write(deck)
    if not os.path.exists(out):
        raise RuntimeError(f"ngspice failed for {name}:\n{log[-3000:]}")
    with open(out) as f:
        header = f.readline().split()
        data = np.loadtxt(f, ndmin=2)
    res = {}
    for i, h in enumerate(header):
        res[h] = data[:, i]
    # normalise keys: user expression -> column
    out_d = {}
    keys = list(res.keys())
    out_d["x"] = res[keys[0]]
    for expr, key in zip(probes, keys[1:]):
        out_d[expr] = res[key]
    if "error" in log.lower() and "aborted" in log.lower():
        raise RuntimeError(log[-3000:])
    return out_d


def at(w, xkey, t):
    """Value of waveform w at x=t (linear interpolation)."""
    return float(np.interp(t, w["x"], w[xkey]))


def src(name, node, spec):
    return f"V{name} {node} 0 {spec}"
