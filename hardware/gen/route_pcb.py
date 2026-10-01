#!/usr/bin/python3
"""Autoroute the placed LFP-8 board with Freerouting and finish it.

    /usr/bin/python3 route_pcb.py [--passes N] [--reuse]

1. rewrites the net classes in lfp8.dsn (KiCad 7 exports every net as default)
2. runs Freerouting headless (lfp8.dsn -> lfp8.ses)
3. imports the session (tracks + vias) into lfp8.kicad_pcb
4. adds the top GND pour + thermal copper, fills all zones, saves
"""
import os
import re
import subprocess
import sys

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_pcb as MP           # noqa: E402
from sexpr import parse, find, find_all   # noqa: E402

KDIR = MP.KDIR
DSN = os.path.join(KDIR, "lfp8.dsn")
SES = os.path.join(KDIR, "lfp8.ses")
JAR = os.environ.get("FREEROUTING_JAR", "/tmp/freerouting-2.1.0.jar")


def net_classes(board):
    """KiCad net name -> class name, from the DSL classification used by make_pcb."""
    out = {}
    for ni in board.GetNetsByName().values():
        name = ni.GetNetname()
        if not name:
            continue
        dsl = name.split("/")[-1]
        base = dsl.rsplit("_", 1)[0] if dsl[-1].isdigit() and "_" in dsl else dsl
        if base in ("BPF", "BP", "BN", "CH1", "CH2", "DLOAD", "MIDISO", "SHP"):
            out[name] = "Power"
        elif dsl in ("+5V", "GND", "+3V3"):
            out[name] = "Supply"
        else:
            out[name] = "kicad_default"
    return out


def q(n):
    return f'"{n}"' if re.search(r'[\s()"]', n) or n == "" else n


def rewrite_classes(classes, dsn=None):
    dsn = dsn or DSN
    txt = open(dsn).read()
    start = txt.index("    (class kicad_default")
    end = txt.index("  (wiring")
    blocks = []
    for cname, (w, cl, vd, vdr) in (("kicad_default", MP.NETCLASSES["Default"]),
                                    ("Power", MP.NETCLASSES["Power"]),
                                    ("Supply", MP.NETCLASSES["Supply"])):
        nets = sorted(n for n, c in classes.items() if c == cname)
        via = f"Via[0-1]_{int(vd * 1000)}:{int(vdr * 1000)}_um"
        lines = [f"    (class {cname}"]
        row = "     "
        for n in nets:
            if len(row) > 70:
                lines.append(row)
                row = "     "
            row += " " + q(n)
        lines.append(row)
        lines.append(f"      (circuit\n        (use_via {via})\n      )")
        lines.append(f"      (rule\n        (width {w * 1000:.0f})\n        (clearance {cl * 1000 + 0.1:.1f})\n      )\n    )")
        blocks.append("\n".join(lines))
    # make sure every via padstack we reference exists in the library section
    new = txt[:start] + "\n".join(blocks) + "\n  )\n" + txt[end:]
    for (w, cl, vd, vdr) in MP.NETCLASSES.values():
        via = f"Via[0-1]_{int(vd * 1000)}:{int(vdr * 1000)}_um"
        if f"(padstack {via}" not in new and f'(padstack "{via}"' not in new:
            ps = (f"    (padstack \"{via}\"\n      (shape\n        (circle F.Cu {vd * 1000:.0f} 0 0)\n      )\n"
                  f"      (shape\n        (circle B.Cu {vd * 1000:.0f} 0 0)\n      )\n      (attach off)\n    )\n")
            i = new.index("  (network")
            j = new.rindex("  )", 0, i)
            new = new[:j] + ps + new[j:]
    open(dsn, "w").write(new)


def run_freerouting(passes, dsn=None, ses=None, logname="freerouting.log"):
    dsn, ses = dsn or DSN, ses or SES
    if os.path.exists(ses):
        os.remove(ses)
    cmd = ["java", "-Xmx6g", "-jar", JAR, "--gui.enabled=false", "-de", dsn, "-do", ses, "-mp", str(passes),
           "-mt", "1"]
    print(" ".join(cmd), flush=True)
    log = open(os.path.join(os.path.dirname(dsn), logname), "w")
    r = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT)
    log.close()
    if r.returncode != 0 or not os.path.exists(ses):
        raise SystemExit("freerouting failed, see freerouting.log")


def import_ses(board, ses=None):
    tree = parse(open(ses or SES).read())
    routes = find(tree, "routes")
    res = find(routes, "resolution")
    scale = {"um": 1e-3, "mil": 0.0254, "mm": 1.0, "inch": 25.4}[str(res[1])] / float(res[2])   # -> mm
    nets = {ni.GetNetname(): ni for ni in board.GetNetsByName().values()}
    # existing (pre-routed) segments, to avoid duplicates
    have = set()
    for t in board.GetTracks():
        if t.Type() == pcbnew.PCB_TRACE_T:
            a, b = t.GetStart(), t.GetEnd()
            have.add((t.GetLayer(), min((a.x, a.y), (b.x, b.y)), max((a.x, a.y), (b.x, b.y))))
        else:
            have.add(("via", t.GetPosition().x, t.GetPosition().y))
    nt = nv = 0
    for net in find_all(find(routes, "network_out"), "net"):
        name = net[1]
        ni = nets.get(name)
        if ni is None:
            raise KeyError(f"net {name} from session not on board")
        for w in find_all(net, "wire"):
            path = find(w, "path")
            layer = pcbnew.F_Cu if path[1] == "F.Cu" else pcbnew.B_Cu
            width = float(path[2]) * scale
            co = [float(v) * scale for v in path[3:]]
            pts = [(co[i], -co[i + 1]) for i in range(0, len(co), 2)]
            for a, b in zip(pts[:-1], pts[1:]):
                A = pcbnew.VECTOR2I(pcbnew.FromMM(a[0]), pcbnew.FromMM(a[1]))
                B = pcbnew.VECTOR2I(pcbnew.FromMM(b[0]), pcbnew.FromMM(b[1]))
                key = (layer, min((A.x, A.y), (B.x, B.y)), max((A.x, A.y), (B.x, B.y)))
                if key in have or (A.x == B.x and A.y == B.y):
                    continue
                t = pcbnew.PCB_TRACK(board)
                t.SetStart(A)
                t.SetEnd(B)
                t.SetWidth(pcbnew.FromMM(width))
                t.SetLayer(layer)
                t.SetNet(ni)
                board.Add(t)
                nt += 1
        for v in find_all(net, "via"):
            m = re.search(r"_(\d+):(\d+)_um", v[1])
            d, drill = (int(m.group(1)) / 1000, int(m.group(2)) / 1000) if m else (0.6, 0.3)
            x, y = float(v[2]) * scale, -float(v[3]) * scale
            P = pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y))
            if ("via", P.x, P.y) in have:
                continue
            via = pcbnew.PCB_VIA(board)
            via.SetPosition(P)
            via.SetWidth(pcbnew.FromMM(d))
            via.SetDrill(pcbnew.FromMM(drill))
            via.SetViaType(pcbnew.VIATYPE_THROUGH)
            via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
            via.SetNet(ni)
            board.Add(via)
            nv += 1
    print(f"imported {nt} track segments and {nv} vias")


def finish(board):
    """Top GND pour and thermal copper zones, then fill everything."""
    OX, OY = MP.OX, MP.OY

    existing = {z.GetZoneName() for z in board.Zones()}

    def zone(netname, layer, poly, prio, solid=False, name=""):
        if name and name in existing:      # idempotent (route_pcb.py --reuse)
            return
        z = pcbnew.ZONE(board)
        z.SetLayer(layer)
        z.SetNet(board.FindNet(netname))
        ol = z.Outline()
        ol.NewOutline()
        for x, y in poly:
            ol.Append(pcbnew.FromMM(x + OX), pcbnew.FromMM(y + OY))
        z.SetAssignedPriority(prio)
        z.SetMinThickness(pcbnew.FromMM(0.25))
        z.SetLocalClearance(pcbnew.FromMM(0.25))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL if solid else pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetThermalReliefGap(pcbnew.FromMM(0.3))
        z.SetThermalReliefSpokeWidth(pcbnew.FromMM(0.5))
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        if name:
            z.SetZoneName(name)
        board.Add(z)

    W, H = MP.W, MP.H
    zone("GND", pcbnew.F_Cu, [(0.3, 0.3), (W - 0.3, 0.3), (W - 0.3, MP.BAND_Y0 - 0.6), (0.3, MP.BAND_Y0 - 0.6)], 0,
         name="GND_F")
    # load-bank copper (heat spreading): BP pads column and DLOAD pads column of each channel
    for k in range(1, 9):
        x0 = MP.LM + (k - 1) * MP.CW
        zone(f"/ch{k}/BP_{k}", pcbnew.F_Cu, [(x0 + 19.0, 102.9), (x0 + 23.6, 102.9), (x0 + 23.6, 119.3),
                                              (x0 + 19.0, 119.3)], 3, solid=True, name=f"BP_{k}")
        zone(f"/ch{k}/DLOAD_{k}", pcbnew.F_Cu, [(x0 + 13.9, 102.9), (x0 + 18.4, 102.9), (x0 + 18.4, 119.3),
                                                (x0 + 13.9, 119.3)], 3, solid=True, name=f"DLOAD_{k}")
    MP.board_fixups(board)
    filler = pcbnew.ZONE_FILLER(board)
    filler.Fill(board.Zones())


def main():
    passes = 60
    if "--passes" in sys.argv:
        passes = int(sys.argv[sys.argv.index("--passes") + 1])
    board = pcbnew.LoadBoard(MP.PCB)
    if "--reuse" not in sys.argv:
        rewrite_classes(net_classes(board))
        run_freerouting(passes)
    import_ses(board)
    finish(board)
    pcbnew.SaveBoard(MP.PCB, board)
    print("saved", MP.PCB)


if __name__ == "__main__":
    main()
