#!/usr/bin/python3
"""JLCPCB production files for the routed LFP-8 board.

    /usr/bin/python3 make_fab.py

writes hardware/fab/:
    lfp8_gerbers.zip          Gerber X2 + Excellon drill (upload to jlcpcb.com)
    lfp8_bom_jlcpcb.csv       Comment, Designator, Footprint, LCSC Part #
    lfp8_cpl_jlcpcb.csv       Designator, Mid X, Mid Y, Layer, Rotation
    lfp8_hand_solder.csv      parts that are not machine assembled (connectors, terminal)
    lfp8_bom_full.csv         every part with value, MPN, LCSC number, class and verification note
All coordinates use the board's lower-left corner as origin (drill/place origin).
"""
import csv
import os
import re
import shutil
import subprocess
import sys
import zipfile
from collections import OrderedDict

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lfp8_circuit       # noqa: E402
import make_pcb as MP     # noqa: E402

FAB = os.path.normpath(os.path.join(HERE, "..", "fab"))

# JLCPCB rotation offsets for KiCad standard footprints (from JLCKicadTools'
# cpl_rotations_db.csv; first matching pattern wins).  Polarised parts must still be
# checked in JLCPCB's placement preview before ordering.
ROT_DB = [
    (r"^SOT-223", 180), (r"^SOT-23", -90), (r"^SOIC-", 270), (r"^TSSOP-", 270),
    (r"^VSSOP-10_", 270), (r"^MSOP-", 270),
]
# footprint origin -> part centroid offsets (footprint coordinates, mm)
CENTROID_DB = {}


def rot_offset(fpname):
    for pat, r in ROT_DB:
        if re.search(pat, fpname):
            return r
    return 0


def main():
    os.makedirs(FAB, exist_ok=True)
    board = pcbnew.LoadBoard(MP.PCB)
    origin = pcbnew.VECTOR2I(pcbnew.FromMM(MP.OX), pcbnew.FromMM(MP.OY + MP.H))
    board.GetDesignSettings().SetAuxOrigin(origin)
    pcbnew.SaveBoard(MP.PCB, board)

    # ---------------------------------------------------------------- gerbers + drill
    gdir = os.path.join(FAB, "gerbers")
    shutil.rmtree(gdir, ignore_errors=True)
    os.makedirs(gdir)
    layers = "F.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts"
    subprocess.run(["kicad-cli", "pcb", "export", "gerbers", "--layers", layers, "--use-drill-file-origin",
                    "--subtract-soldermask", "-o", gdir + "/", MP.PCB], check=True, capture_output=True)
    subprocess.run(["kicad-cli", "pcb", "export", "drill", "--format", "excellon", "--drill-origin", "plot",
                    "--excellon-units", "mm", "--excellon-separate-th", "--generate-map", "--map-format", "gerberx2",
                    "-o", gdir + "/", MP.PCB], check=True, capture_output=True)
    zp = os.path.join(FAB, "lfp8_gerbers.zip")
    with zipfile.ZipFile(zp, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(os.listdir(gdir)):
            z.write(os.path.join(gdir, f), f)

    # ---------------------------------------------------------------- BOM / CPL
    c = lfp8_circuit.build_board()
    inst = {i.ref: i for i in c.insts}
    fps = {fp.GetReference(): fp for fp in board.GetFootprints()}
    groups = OrderedDict()
    hand, cpl, full = [], [], []
    for ref in sorted(inst, key=lambda r: (re.sub(r"\d", "", r), int(re.sub(r"\D", "", r) or 0))):
        i = inst[ref]
        fp = fps[ref]
        fpname = fp.GetFPID().GetLibItemName().wx_str()
        full.append([ref, i.val, i.part.desc, i.part.mfr, i.part.lcsc or "", i.part.jlc, i.part.footprint,
                     i.part.verified])
        if i.part.jlc == "Hand" or not i.part.lcsc:
            if i.part.footprint.startswith("MountingHole"):
                continue
            hand.append([ref, i.val, i.part.desc, i.part.mfr, i.part.footprint])
            continue
        key = i.part.lcsc
        g = groups.setdefault(key, {"comment": i.val, "refs": [], "fp": fpname, "lcsc": key})
        g["refs"].append(ref)
        pos = fp.GetPosition()
        mx = (pos.x - origin.x) / 1e6
        my = (origin.y - pos.y) / 1e6
        rot = (fp.GetOrientationDegrees() + rot_offset(fpname)) % 360
        cpl.append([ref, f"{mx:.4f}mm", f"{my:.4f}mm", "Top" if fp.GetLayer() == pcbnew.F_Cu else "Bottom",
                    f"{rot:.1f}"])
    with open(os.path.join(FAB, "lfp8_bom_jlcpcb.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #"])
        for g in groups.values():
            w.writerow([g["comment"], ",".join(g["refs"]), g["fp"], g["lcsc"]])
    with open(os.path.join(FAB, "lfp8_cpl_jlcpcb.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        w.writerows(cpl)
    with open(os.path.join(FAB, "lfp8_hand_solder.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "Value", "Description", "Part", "Footprint"])
        w.writerows(hand)
    with open(os.path.join(FAB, "lfp8_bom_full.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "Value", "Description", "MPN", "LCSC", "JLC class", "Footprint", "Verified"])
        w.writerows(full)
    n_ext = len({g["lcsc"] for g in groups.values() if inst[g["refs"][0]].part.jlc == "Extended"})
    print(f"gerbers: {zp}")
    print(f"BOM lines: {len(groups)}  placed parts: {len(cpl)}  extended part types: {n_ext}  hand-soldered: {len(hand)}")


if __name__ == "__main__":
    main()
