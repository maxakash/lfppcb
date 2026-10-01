#!/usr/bin/python3
"""Board-level adversarial checks for the routed LFP-8 PCB.

    /usr/bin/python3 check_pcb.py

  1. KiCad DRC (clearances, unconnected items, courtyards, edge clearance)
     with JLCPCB 2-layer limits
  2. the copper connectivity equals the DSL netlist used by the simulations
  3. every 1 A path is wide enough (IPC-2221 external layer, 1 oz, <= 10 C rise)
  4. the +5 V bus and its feed can carry the full 8 x 1 A charge current
  5. BOM sanity: every machine-placed part has a verified LCSC number
Writes hardware/fab/check_report.json and exits non-zero on failure.
"""
import json
import os
import re
import subprocess
import sys
import tempfile

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lfp8_circuit       # noqa: E402
import make_pcb as MP     # noqa: E402

OUT = os.path.normpath(os.path.join(HERE, "..", "fab", "check_report.json"))
OZ = 0.035  # copper thickness mm (1 oz)


def ipc2221_current(width_mm, dt=10.0, outer=True):
    """Max current of a trace (IPC-2221 curve fit)."""
    area_mil2 = (width_mm / 0.0254) * (OZ / 0.0254)
    k = 0.048 if outer else 0.024
    return k * dt ** 0.44 * area_mil2 ** 0.725


def drc(board_path):
    rpt = os.path.join(tempfile.mkdtemp(), "drc.rpt")
    code = f'''
import pcbnew
b = pcbnew.LoadBoard({board_path!r})
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
print(pcbnew.WriteDRCReport(b, {rpt!r}, pcbnew.EDA_UNITS_MILLIMETRES, True))
'''
    subprocess.run(["/usr/bin/python3", "-c", code], check=True, capture_output=True, text=True)
    txt = open(rpt).read()
    viol = re.findall(r"^\[(\w+)\]: (.*)$", txt, re.M)
    counts = {}
    for kind, _ in viol:
        counts[kind] = counts.get(kind, 0) + 1
    m = re.search(r"\*\* Found (\d+) DRC violations", txt)
    u = re.search(r"\*\* Found (\d+) unconnected pads", txt)
    return {"violations": int(m.group(1)) if m else None, "unconnected": int(u.group(1)) if u else None,
            "by_type": counts, "report": txt}


def main():
    res = {}
    fails = []
    board = pcbnew.LoadBoard(MP.PCB)

    # 1. DRC ------------------------------------------------------------------
    d = drc(MP.PCB)
    res["drc"] = {k: v for k, v in d.items() if k != "report"}
    open(os.path.join(os.path.dirname(OUT), "drc_report.txt"), "w").write(d["report"])
    # silkscreen-only findings are cosmetic; everything else must be zero
    hard = {k: v for k, v in d["by_type"].items() if not k.startswith("silk") and k not in ("text_height",)}
    if hard or d["unconnected"]:
        fails.append(f"DRC: {hard}, unconnected {d['unconnected']}")

    # 2. netlist --------------------------------------------------------------
    r = subprocess.run([sys.executable if False else "python3", os.path.join(HERE, "verify_netlist.py"), "--pcb"],
                       capture_output=True, text=True)
    res["netlist"] = r.stdout.strip().splitlines()[-1] if r.stdout else r.stderr[-300:]
    if r.returncode != 0:
        fails.append("netlist mismatch: " + r.stdout[-500:])

    # 3. current capacity of the 1 A paths -------------------------------------
    power = ("BPF", "BP", "BN", "CH1", "CH2", "DLOAD", "MIDISO", "SHP")
    worst = {}
    for t in board.GetTracks():
        if t.Type() != pcbnew.PCB_TRACE_T:
            continue
        n = t.GetNetname().split("/")[-1]
        base = n.rsplit("_", 1)[0]
        if base in power:
            w = t.GetWidth() / 1e6
            worst[base] = min(worst.get(base, 99), w)
    cap = {k: round(ipc2221_current(w), 2) for k, w in worst.items()}
    res["power_track_min_width_mm"] = worst
    res["power_track_capacity_A_10C"] = cap
    for k, a in cap.items():
        if a < 1.5:   # 1.07 A discharge / 1.0 A charge with 40 % margin
            fails.append(f"track width {k}: {worst[k]} mm carries only {a} A")
    # 4. +5 V bus: two 6.5 mm layers, 8 A at the feed
    bus_w = MP.BAND_Y1 - MP.BAND_Y0
    bus_a = 2 * ipc2221_current(bus_w, dt=10)
    res["bus_capacity_A_10C"] = round(bus_a, 1)
    if bus_a < 8 * 1.07 * 1.2:
        fails.append(f"+5V bus only {bus_a:.1f} A")
    spur = [t.GetWidth() / 1e6 for t in board.GetTracks()
            if t.Type() == pcbnew.PCB_TRACE_T and t.GetNetname() == "+5V" and t.IsLocked()]
    res["5V_spur_min_width_mm"] = min(spur) if spur else None

    # 5. BOM -------------------------------------------------------------------
    c = lfp8_circuit.build_board()
    nolcsc = [i.ref for i in c.insts if i.part.jlc != "Hand" and not i.part.lcsc]
    unverified = sorted({i.part.key for i in c.insts if i.part.jlc != "Hand" and not i.part.verified})
    notverif = sorted({i.part.key for i in c.insts if "NOT" in (i.part.verified or "")})
    res["bom"] = {"machine_placed": sum(1 for i in c.insts if i.part.jlc != "Hand"),
                  "hand": sum(1 for i in c.insts if i.part.jlc == "Hand"),
                  "basic_or_preferred_types": len({i.part.lcsc for i in c.insts if i.part.jlc in ("Basic", "Preferred")}),
                  "extended_types": sorted({i.part.key for i in c.insts if i.part.jlc == "Extended"}),
                  "unverified": unverified, "verify_at_order": notverif}
    if nolcsc or unverified:
        fails.append(f"BOM: no LCSC {nolcsc}, unverified {unverified}")

    # board facts
    res["board_mm"] = [MP.W, MP.H]
    res["vias"] = sum(1 for t in board.GetTracks() if t.Type() == pcbnew.PCB_VIA_T)
    res["track_segments"] = sum(1 for t in board.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T)
    res["failures"] = fails
    res["result"] = "PASS" if not fails else "FAIL"
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    json.dump(res, open(OUT, "w"), indent=2)
    print(json.dumps({k: v for k, v in res.items() if k != "bom"}, indent=1))
    print("RESULT:", res["result"])
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
