#!/usr/bin/python3
"""Tiled routing of the LFP-8 board (the 8 channels are identical).

    /usr/bin/python3 route_tiled.py template     # channel-1 board + stub pads
    /usr/bin/python3 route_tiled.py template-dsn # -> route_work/ch_template.dsn (fresh process: see make_template)
    /usr/bin/python3 route_tiled.py route-template [passes]
    /usr/bin/python3 route_tiled.py replicate    # copy channel-1 routing to channels 1..8 (locked)
    /usr/bin/python3 route_tiled.py route-global [passes]
    /usr/bin/python3 route_tiled.py finish       # zones, fix-ups, fill, save lfp8.kicad_pcb

Routing the whole board in one Freerouting run takes > 2 h; routing one channel
column and replicating it by translation takes minutes and gives 8 identical,
reviewable channel layouts.  Every net that leaves the channel is brought to a
stub pad on the column's top edge; the global pass then only connects the
common section to those stubs.
"""
import os
import sys

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_pcb as MP        # noqa: E402
import route_pcb as RP       # noqa: E402

KDIR = MP.KDIR
WORK = os.path.join(KDIR, "route_work")
PLACED = os.path.join(WORK, "placed.kicad_pcb")          # placed + pre-routed board (input)
TPL = os.path.join(WORK, "ch_template.kicad_pcb")
TPL_DSN = os.path.join(WORK, "ch_template.dsn")
TPL_SES = os.path.join(WORK, "ch_template.ses")
REP = os.path.join(WORK, "replicated.kicad_pcb")
GLB_DSN = os.path.join(WORK, "global.dsn")
GLB_SES = os.path.join(WORK, "global.ses")
X0, Y0, Y1 = MP.LM, MP.Y_SIG, 137.5                       # channel-1 column (board mm)
STUB_Y = MP.Y_SIG + 0.75

mm = pcbnew.FromMM


def bmm(v):
    return v / 1e6


def ext_nets():
    """DSL nets of channel 1 that also connect outside the channel (stub needed)."""
    c = MP.lfp8_circuit.build_board()
    inside = {n for i in c.by_group("ch1") for n in i.conns.values() if n}
    outside = {n for i in c.insts if i.group != "ch1" for n in i.conns.values() if n}
    order = ["MXA_1", "MXB_1", "MXC_1", "MXE_1", "NTC_1", "VREF", "VS", "+3V3", "SAFE_EN",
             "LED_1", "DISEN_1", "CHGEN_1"]
    nets = sorted(inside & outside - {"GND", "+5V"}, key=lambda n: order.index(n) if n in order else 99)
    return nets


def make_template():
    os.makedirs(WORK, exist_ok=True)
    b = pcbnew.LoadBoard(PLACED)
    c = MP.lfp8_circuit.build_board()
    ch1 = {i.ref for i in c.by_group("ch1")}
    # Collect everything first: after any BOARD.Remove() the SWIG container accessors
    # (GetTracks/Zones/GetFootprints) of KiCad 7 stop working in this process.
    tracks, zones = list(b.GetTracks()), list(b.Zones())
    drawings, fps = list(b.GetDrawings()), list(b.GetFootprints())
    xl, xr = X0 + MP.OX, X0 + MP.CW + MP.OX
    yt, yb = Y0 + MP.OY, Y1 + MP.OY
    drop = []
    for t in tracks:
        if t.Type() == pcbnew.PCB_VIA_T:
            p = t.GetPosition()
            inside = xl < bmm(p.x) < xr and yt < bmm(p.y) < yb
        else:
            a, e = t.GetStart(), t.GetEnd()
            inside = all(xl < bmm(q.x) < xr for q in (a, e)) and all(yt < bmm(q.y) < yb + 5 for q in (a, e))
            inside = inside and t.GetWidth() <= mm(2.0)       # not the 5 mm J1 feed / 6.5 mm bus
            if inside and bmm(e.y) > yb - 0.5:          # +5 V spur into the bus: clip at the column bottom
                t.SetEnd(pcbnew.VECTOR2I(e.x, mm(yb - 0.5)))
            if inside and bmm(a.y) > yb - 0.5:
                t.SetStart(pcbnew.VECTOR2I(a.x, mm(yb - 0.5)))
        if not inside:
            drop.append(t)
    drop += zones + [d for d in drawings if d.GetLayer() == pcbnew.Edge_Cuts]
    keep_fps = [f for f in fps if f.GetReference() in ch1]
    drop += [f for f in fps if f.GetReference() not in ch1]
    pads = [(bmm(p.GetPosition().x) - MP.OX, bmm(p.GetPosition().y) - MP.OY,
             max(bmm(p.GetSize().x), bmm(p.GetSize().y))) for f in keep_fps for p in f.Pads()]
    netnames = MP.kicad_net_names(c)
    nets = {n: b.FindNet(netnames[n]) for n in ext_nets()}
    gnd = b.FindNet("GND")
    for item in drop:
        b.Remove(item)
    pts = [(X0, Y0), (X0 + MP.CW, Y0), (X0 + MP.CW, Y1), (X0, Y1)]
    for i in range(4):
        s = pcbnew.PCB_SHAPE(b)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(mm(0.1))
        s.SetStart(MP.pt(*pts[i]))
        s.SetEnd(MP.pt(*pts[(i + 1) % 4]))
        b.Add(s)
    # GND plane on B.Cu
    z = pcbnew.ZONE(b)
    z.SetLayer(pcbnew.B_Cu)
    z.SetNet(gnd)
    ol = z.Outline()
    ol.NewOutline()
    for x, y in pts:
        ol.Append(mm(x + MP.OX), mm(y + MP.OY))
    b.Add(z)
    # stub pads: one per external net, along the top edge, clear of existing pads
    x = X0 + 1.0
    for i, n in enumerate(ext_nets()):
        while any(abs(px - x) < 0.6 + ps / 2 + 0.35 and abs(py - STUB_Y) < 0.6 + ps / 2 + 0.35 for px, py, ps in pads):
            x += 0.25
        if x > X0 + MP.CW - 1.0:
            raise RuntimeError("no room for stub pads")
        fp = pcbnew.FOOTPRINT(b)
        fp.SetReference(f"STUB{i + 1}")
        fp.SetValue(n)
        b.Add(fp)
        fp.SetPosition(MP.pt(x, STUB_Y))
        pad = pcbnew.PAD(fp)
        pad.SetAttribute(pcbnew.PAD_ATTRIB_SMD)
        pad.SetShape(pcbnew.PAD_SHAPE_RECT)
        pad.SetLayerSet(pcbnew.PAD.SMDMask())
        pad.SetSize(pcbnew.VECTOR2I(mm(0.6), mm(0.6)))
        pad.SetNumber("1")
        fp.Add(pad)
        pad.SetPosition(MP.pt(x, STUB_Y))
        pad.SetNet(nets[n])
        x += 1.6
    pcbnew.SaveBoard(TPL, b)
    print("template saved; stubs for", ext_nets())


def export_template():
    b = pcbnew.LoadBoard(TPL)
    ok = pcbnew.ExportSpecctraDSN(b, TPL_DSN)
    RP.rewrite_classes(RP.net_classes(b), TPL_DSN)
    print("template DSN" if ok else "DSN FAILED", sum(1 for _ in b.GetFootprints()), "footprints")


def route(dsn, ses, passes, log):
    RP.run_freerouting(passes, dsn=dsn, ses=ses, logname=log)
    print("routed ->", ses)


def chan_netmap(c, names):
    """(KiCad net name in channel 1) -> function k -> KiCad net name in channel k."""
    dsl_of = {v: k for k, v in names.items() if v}
    allnets = set(names)

    def f(kname, k):
        dsl = dsl_of.get(kname)
        if dsl is None:
            raise KeyError(kname)
        if dsl.endswith("_1") and f"{dsl[:-2]}_{k}" in allnets and f"{dsl[:-2]}_2" in allnets:
            dsl = f"{dsl[:-2]}_{k}"
        return names[dsl]
    return f


def replicate():
    tpl = pcbnew.LoadBoard(TPL)
    RP.import_ses(tpl, TPL_SES)
    pcbnew.SaveBoard(os.path.join(WORK, "ch_template_routed.kicad_pcb"), tpl)
    b = pcbnew.LoadBoard(PLACED)
    c = MP.lfp8_circuit.build_board()
    names = MP.kicad_net_names(c)
    fmap = chan_netmap(c, names)
    have = set()
    for t in b.GetTracks():
        if t.Type() == pcbnew.PCB_TRACE_T:
            a, e = t.GetStart(), t.GetEnd()
            have.add((t.GetLayer(), min((a.x, a.y), (e.x, e.y)), max((a.x, a.y), (e.x, e.y))))
        else:
            have.add(("via", t.GetPosition().x, t.GetPosition().y))
    nt = nv = 0
    yclip = mm(Y1 - 0.5 + MP.OY)
    for k in range(1, MP.lfp8_circuit.N_CH + 1):
        dx = mm((k - 1) * MP.CW)
        for t in tpl.GetTracks():
            net = b.FindNet(fmap(t.GetNetname(), k))
            if t.Type() == pcbnew.PCB_VIA_T:
                p = t.GetPosition()
                P = pcbnew.VECTOR2I(p.x + dx, p.y)
                if ("via", P.x, P.y) in have:
                    continue
                v = pcbnew.PCB_VIA(b)
                v.SetPosition(P)
                v.SetWidth(t.GetWidth())
                v.SetDrill(t.GetDrillValue())
                v.SetViaType(pcbnew.VIATYPE_THROUGH)
                v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
                v.SetNet(net)
                v.SetLocked(True)
                b.Add(v)
                nv += 1
                continue
            a, e = t.GetStart(), t.GetEnd()
            if a.y == yclip or e.y == yclip:      # the clipped +5 V spur already exists in full
                continue
            A = pcbnew.VECTOR2I(a.x + dx, a.y)
            E = pcbnew.VECTOR2I(e.x + dx, e.y)
            key = (t.GetLayer(), min((A.x, A.y), (E.x, E.y)), max((A.x, A.y), (E.x, E.y)))
            if key in have:
                continue
            n = pcbnew.PCB_TRACK(b)
            n.SetStart(A)
            n.SetEnd(E)
            n.SetWidth(t.GetWidth())
            n.SetLayer(t.GetLayer())
            n.SetNet(net)
            n.SetLocked(True)
            b.Add(n)
            nt += 1
    pcbnew.SaveBoard(REP, b)
    ok = pcbnew.ExportSpecctraDSN(b, GLB_DSN)
    RP.rewrite_classes(RP.net_classes(b), GLB_DSN)
    print(f"replicated {nt} segments + {nv} vias into 8 channels;", "global DSN" if ok else "DSN FAILED")


def finish():
    b = pcbnew.LoadBoard(REP)
    RP.import_ses(b, GLB_SES)
    RP.finish(b)
    pcbnew.SaveBoard(MP.PCB, b)
    print("saved", MP.PCB)


if __name__ == "__main__":
    cmd = sys.argv[1]
    passes = int(sys.argv[2]) if len(sys.argv) > 2 else 30
    if cmd == "template":
        make_template()
    elif cmd == "template-dsn":
        export_template()
    elif cmd == "route-template":
        route(TPL_DSN, TPL_SES, passes, "freerouting_template.log")
    elif cmd == "replicate":
        replicate()
    elif cmd == "route-global":
        route(GLB_DSN, GLB_SES, passes, "freerouting_global.log")
    elif cmd == "finish":
        finish()
    else:
        raise SystemExit(__doc__)
