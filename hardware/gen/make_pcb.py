#!/usr/bin/python3
"""Place the LFP-8 PCB from the netlist DSL (run with the KiCad python: /usr/bin/python3).

Floorplan (216 x 146 mm, 2 layers, y grows downwards):

   y   0 ..  44   common section: ESP32-C3 (antenna on the top edge), 74HC595 chain,
                  CD4051 muxes + ADS1115, SAFE_EN chain, reference / ISET DAC
   y  44 ..  98   eight 25 mm channel columns: small-signal part (LM324 + passives)
   y  98 .. 127   per-channel power stage: P-FET pass device, Schottky, 1.2 ohm
                  ballast, 4 x 11 ohm load bank, discharge FET, isolation FETs, shunt
   y 127 .. 135   JST-XH cell connectors on the bottom edge
   y 138 .. 144.5 +5 V bus (6.5 mm wide on both copper layers), fed by J1 (bottom left)

GND is a solid plane on B.Cu above the +5 V bus.  The power-stage interconnect
(1 A paths) is pre-routed here as locked tracks; everything else is routed by
Freerouting (route_pcb.py).

Outputs hardware/kicad/lfp8.kicad_pcb (placed + pre-routed) and lfp8.dsn.
"""
import json
import math
import os
import re
import subprocess
import sys
import tempfile

import numpy as np
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lfp8_circuit                      # noqa: E402
from make_sch import uid                 # noqa: E402
from sexpr import parse, find, find_all  # noqa: E402

KDIR = os.environ.get("LFP8_KDIR") or os.path.normpath(os.path.join(HERE, "..", "kicad"))
FPDIR = "/usr/share/kicad/footprints"
PCB = os.path.join(KDIR, "lfp8.kicad_pcb")

# ------------------------------------------------------------------ geometry
W, H = 216.0, 146.0
LM, CW = 10.0, 25.0            # left margin, channel column width
Y_SIG = 44.0                   # top of channel area
Y_JST = 131.5                  # JST pin row
BAND_Y0, BAND_Y1 = 138.0, 144.5
BAND_XL_B, BAND_XL_F, BAND_XR = 10.0, 10.0, W - 9.0
J1_XY = (5.5, 134.5)          # 5 V input terminal pad 1 (+5V); fed into the bus on both layers
OX, OY = 30.0, 30.0            # board origin on the drawing sheet
PART_MARGIN = 0.45            # keep-out added around every courtyard (routing room)
HOLES = [(4.5, 4.5), (W - 4.5, 4.5), (4.5, H - 4.5), (W - 4.5, H - 4.5)]   # 4 corners (board stand)

# JLCPCB 2-layer standard capabilities, with margin
CLEARANCE = 0.2
TRACK = 0.25
VIA_D, VIA_DRILL = 0.6, 0.3
NETCLASSES = {
    # name: (track width, clearance, via diameter, via drill)
    "Default": (0.25, 0.2, 0.6, 0.3),
    "Power": (0.8, 0.2, 0.8, 0.4),     # 1 A cell paths (>= 2 A capable at 10 C rise)
    "Supply": (0.5, 0.2, 0.8, 0.4),    # +5V / GND / +3V3 branches to the logic
}


def mm(v):
    return pcbnew.FromMM(float(v))


def pt(x, y):
    return pcbnew.VECTOR2I(mm(x + OX), mm(y + OY))


def unpt(v):
    return v.x / 1e6 - OX, v.y / 1e6 - OY


# ------------------------------------------------------------------ net naming (KiCad names from the schematic)
def kicad_net_names(c):
    """Map DSL net -> net name KiCad gives it (local labels are '/<sheet>/<net>')."""
    out = os.path.join(tempfile.mkdtemp(), "n.net")
    subprocess.run(["kicad-cli", "sch", "export", "netlist", "--format", "kicadsexpr", "-o", out,
                    os.path.join(KDIR, "lfp8.kicad_sch")], check=True, capture_output=True)
    tree = parse(open(out).read())
    names = {}
    for net in find_all(find(tree, "nets"), "net"):
        nm = find(net, "name")[1]
        for n in find_all(net, "node"):
            names[(find(n, "ref")[1], find(n, "pin")[1])] = nm
    m = {}
    for net, pins in c.nets().items():
        ks = {names.get(p) for p in pins}
        ks.discard(None)
        if len(ks) == 1:
            k = ks.pop()
            m[net] = k if not k.startswith("unconnected") else ""
        else:
            m[net] = ""
    return m


# ------------------------------------------------------------------ placement plan
def ch_fixed(k):
    """Fixed placements of the channel power stage, local column coordinates.
    Returns {ref: (x, y, rot)} - x local to the column, y absolute."""
    r = lambda i: f"R{k}{i:02d}"
    q = lambda i: f"Q{k}{i:02d}"
    d = lambda i: f"D{k}{i:02d}"
    f = {
        f"J{k}01": (22.0, Y_JST, 180),      # pin 1 (B+ force) on the right, pin 6 (GND) on the left
        f"F{k}01": (22.0, 122.5, 90),       # PTC: B+ force pad down, BP pad up
        q(1): (2.6, 100.0, 0),              # pass P-FET: D right, G top-left, S bottom-left
        d(1): (8.3, 100.0, 180),            # SS34: A left, K right
        r(6): (16.0, 100.0, 0),             # 1.2 ohm ballast: CH2 left, BP right
        q(4): (15.58, 121.3, 90),           # discharge FET: D up (into load bank), G/S down
        q(5): (11.0, 125.3, 270),           # isolation FET (cell side): D down to JST pin 4
        q(8): (8.1, 121.0, 90),             # isolation FET (shunt side): D up to the shunt
        f"R{k}50": (8.55, 114.5, 90),       # Kelvin shunt: pad1 (SHP) down, pad2 (GND) up, sense pads right
        f"U{k}01": (12.5, 72.0, 0),         # LM324
    }
    for i, y in enumerate((104.8, 109.0, 113.2, 117.4)):
        f[r(24 + i)] = (18.54, y, 180)      # 11 ohm load bank: BP pad right (x 21.5), DLOAD pad left (x 15.58)
    return f


def ch_preroutes(k):
    """Locked 1 A tracks of channel k: list of (net, layer, width, [(x,y) local...]).
    Endpoints are snapped onto pad centres by name "ref.pad"."""
    r = lambda i: f"R{k}{i:02d}"
    q = lambda i: f"Q{k}{i:02d}"
    d = lambda i: f"D{k}{i:02d}"
    J, F, RS = f"J{k}01", f"F{k}01", f"R{k}50"
    return [
        ("BPF", "F", 1.2, [f"{J}.1", f"{F}.1"]),
        ("BP", "F", 1.6, [f"{r(24)}.1", f"{r(27)}.1"]),                 # BP column of the load bank
        ("BP", "F", 1.2, [f"{r(27)}.1", f"{F}.2"]),
        ("BP", "F", 1.2, [f"{r(6)}.2", (21.5, 100.0), f"{r(24)}.1"]),
        ("DLOAD", "F", 1.6, [f"{r(24)}.2", f"{r(27)}.2"]),               # DLOAD column
        ("DLOAD", "F", 1.2, [f"{r(27)}.2", f"{q(4)}.3"]),
        ("CH1", "F", 1.0, [f"{q(1)}.3", f"{d(1)}.2"]),
        ("CH2", "F", 1.2, [f"{d(1)}.1", f"{r(6)}.1"]),
        ("+5V", "F", 1.0, [f"{q(1)}.2", (1.66, 101.6), (1.66, 141.25)]),  # spur into the +5 V bus
        ("SHP", "F", 0.8, [f"{q(8)}.3", f"{RS}.1"]),
        ("MIDISO", "F", 0.8, [f"{q(8)}.2", f"{q(5)}.2"]),
        ("BN", "F", 1.0, [f"{q(5)}.3", (11.0, 128.3), (14.5, 128.3), f"{J}.4"]),
    ]


def ch_gnd_vias(k):
    """Stitching vias (local coords) for the high-current GND returns of channel k."""
    return [
        (f"Q{k}04.2", [(16.53, 123.9), (17.6, 123.9)]),     # discharge FET source
        (f"R{k}50.2", [(7.5, 110.1), (8.7, 110.1)]),         # shunt GND end
    ]


# board-level (common section) fixed placements, absolute coordinates
COMMON_FIXED = {
    "U6": (W - 23.0, 18.4, 0),        # ESP32-C3-WROOM-02, antenna on the top edge
    "U1": (166.0, 9.0, 0),            # AMS1117-3.3
    "J3": (150.0, 3.2, 90),           # USB / boot header (2.54 mm) on the top edge
    "J4": (128.0, 3.2, 90),           # AUX / I2C header
    "J2": (112.0, 3.2, 90),           # fan header
    "SW1": (9.0, 13.0, 0),            # E-STOP (pulls SAFE_EN low)
    "SW2": (173.0, 31.0, 0),          # EN / reset
    "SW3": (173.0, 39.0, 0),          # BOOT
    "U4": (22.0, 22.0, 0),            # LM393 watchdog / rail UV
    "U5": (34.0, 22.0, 0),            # LM393 rail OV / board OT
    "U13": (45.0, 38.0, 90),          # 74HC595 #1 (ch 1-3)
    "U14": (95.0, 38.0, 90),          # 74HC595 #2 (ch 3-6)
    "U15": (145.0, 38.0, 90),         # 74HC595 #3 (ch 6-8)
    "U16": (120.0, 38.0, 90),         # 74HC595 #4 (mux select, ISET, AUX)
    "U8": (62.0, 22.0, 0),            # CD4051 MXA (B+ sense)
    "U9": (74.0, 22.0, 0),            # MXB (B- sense)
    "U10": (86.0, 22.0, 0),           # MXC (shunt +)
    "U12": (98.0, 22.0, 0),           # MXE (shunt -)
    "U11": (110.0, 22.0, 0),          # MXD (NTC)
    "U7": (80.0, 8.0, 0),             # ADS1115
    "U3": (58.0, 8.0, 0),             # LM358 ISET DAC buffer
    "U2": (46.0, 8.0, 0),             # CJ431 reference
    "J1": (J1_XY[0], J1_XY[1], 90),   # 5 V input terminal: pad 1 (+5V) feeds the bus, pad 2 (GND) above
    "D1": (5.0, 122.5, 90),           # TVS right next to the input
    "D2": (5.0, 114.5, 90),           # reverse-polarity crowbar
    "C1": (3.0, 108.0, 90),
    "C2": (7.0, 108.0, 90),
    "C3": (3.0, 102.5, 90),
    "C4": (7.0, 102.5, 90),
}
# placement regions for the greedy placer (absolute rectangles x0, y0, x1, y1)
REGIONS = {
    "power": [(1.0, 1.0, 175.0, 43.0)],
    "safety": [(1.0, 1.0, 60.0, 43.0)],
    "mcu": [(150.0, 1.0, 215.0, 43.0)],
    "adc": [(50.0, 1.0, 140.0, 43.0)],
    "logic": [(30.0, 25.0, 215.0, 43.0), (150.0, 1.0, 215.0, 43.0)],
    "mech": [],
}


# ------------------------------------------------------------------ footprints
def load_fp(fpid):
    lib, name = fpid.split(":")
    path = os.path.join(KDIR, "lib", "LFP8.pretty") if lib == "LFP8" else os.path.join(FPDIR, lib + ".pretty")
    fp = pcbnew.FootprintLoad(path, name)
    if fp is None:
        raise RuntimeError(f"footprint {fpid} not found")
    fp.SetFPID(pcbnew.LIB_ID(lib, name))
    return fp


def courtyard(fp):
    """Absolute courtyard bbox (x0,y0,x1,y1) in board mm (front or back courtyard)."""
    xs, ys = [], []
    for g in fp.GraphicalItems():
        if g.GetLayer() in (pcbnew.F_CrtYd, pcbnew.B_CrtYd):
            bb = g.GetBoundingBox()
            xs += [bb.GetLeft(), bb.GetRight()]
            ys += [bb.GetTop(), bb.GetBottom()]
    if not xs:
        bb = fp.GetBoundingBox(False, False)
        xs, ys = [bb.GetLeft(), bb.GetRight()], [bb.GetTop(), bb.GetBottom()]
    return (min(xs) / 1e6 - OX, min(ys) / 1e6 - OY, max(xs) / 1e6 - OX, max(ys) / 1e6 - OY)


class Occupancy:
    RES = 0.25

    def __init__(self):
        self.g = np.zeros((int(H / self.RES) + 1, int(W / self.RES) + 1), dtype=bool)

    def _idx(self, x0, y0, x1, y1):
        r = self.RES
        return (max(0, int(math.floor(y0 / r))), min(self.g.shape[0], int(math.ceil(y1 / r))),
                max(0, int(math.floor(x0 / r))), min(self.g.shape[1], int(math.ceil(x1 / r))))

    def mark(self, box, m=0.0):
        a, b, c, d = self._idx(box[0] - m, box[1] - m, box[2] + m, box[3] + m)
        self.g[a:b, c:d] = True

    def free(self, box, m=0.0):
        if box[0] - m < 0.3 or box[1] - m < 0.3 or box[2] + m > W - 0.3 or box[3] + m > H - 0.3:
            return False
        a, b, c, d = self._idx(box[0] - m, box[1] - m, box[2] + m, box[3] + m)
        return not self.g[a:b, c:d].any()


# ------------------------------------------------------------------ board
class BoardBuilder:
    def __init__(self):
        self.c = lfp8_circuit.build_board()
        self.netname = kicad_net_names(self.c)
        self.b = pcbnew.BOARD()
        self.b.SetCopperLayerCount(2)
        self.nets = {}
        self.fps = {}
        self.occ = Occupancy()
        self.pre = []        # pre-routed tracks (for the record)
        self._design_rules()

    # -------------------------------------------------- rules / classes
    def _design_rules(self):
        ds = self.b.GetDesignSettings()
        ds.m_MinClearance = mm(0.15)
        ds.m_TrackMinWidth = mm(0.15)
        ds.m_ViasMinSize = mm(0.5)
        ds.m_MinThroughDrill = mm(0.3)
        ds.m_ViasMinAnnularWidth = mm(0.13)
        ds.m_HoleToHoleMin = mm(0.5)
        ds.m_HoleClearance = mm(0.25)
        ds.m_CopperEdgeClearance = mm(0.3)
        ds.m_SilkClearance = mm(0.0)
        ds.m_MinSilkTextHeight = mm(0.8)
        ns = ds.m_NetSettings
        self.classes = {}
        for name, (w, cl, vd, vdr) in NETCLASSES.items():
            nc = ns.m_DefaultNetClass if name == "Default" else pcbnew.NETCLASS(name)
            nc.SetTrackWidth(mm(w))
            nc.SetClearance(mm(cl))
            nc.SetViaDiameter(mm(vd))
            nc.SetViaDrill(mm(vdr))
            if name != "Default":
                ns.m_NetClasses[name] = nc
            self.classes[name] = nc

    def net_class_of(self, dsl_net):
        base = dsl_net.rsplit("_", 1)[0] if dsl_net[-1].isdigit() and "_" in dsl_net else dsl_net
        if base in ("BPF", "BP", "BN", "CH1", "CH2", "DLOAD", "MIDISO", "SHP"):
            return "Power"
        if dsl_net in ("+5V", "GND", "+3V3"):
            return "Supply"
        return "Default"

    def net(self, dsl_net):
        if not dsl_net:
            return None
        kn = self.netname.get(dsl_net) or ""
        if not kn:
            return None
        if kn not in self.nets:
            ni = pcbnew.NETINFO_ITEM(self.b, kn)
            self.b.Add(ni)
            ni.SetNetClass(self.classes[self.net_class_of(dsl_net)])
            self.nets[kn] = ni
        return self.nets[kn]

    # -------------------------------------------------- outline / holes
    def outline(self):
        r = 3.0
        pts = [(r, 0), (W - r, 0), (W, r), (W, H - r), (W - r, H), (r, H), (0, H - r), (0, r)]
        for i in range(0, 8, 2):
            s = pcbnew.PCB_SHAPE(self.b)
            s.SetShape(pcbnew.SHAPE_T_SEGMENT)
            s.SetLayer(pcbnew.Edge_Cuts)
            s.SetWidth(mm(0.1))
            s.SetStart(pt(*pts[i]))
            s.SetEnd(pt(*pts[i + 1]))
            self.b.Add(s)
        for cx, cy, a0 in ((W - r, r, 270), (W - r, H - r, 0), (r, H - r, 90), (r, r, 180)):
            s = pcbnew.PCB_SHAPE(self.b)
            s.SetShape(pcbnew.SHAPE_T_ARC)
            s.SetLayer(pcbnew.Edge_Cuts)
            s.SetWidth(mm(0.1))
            s.SetCenter(pt(cx, cy))
            s.SetStart(pt(cx + r * math.cos(math.radians(a0)), cy + r * math.sin(math.radians(a0))))
            s.SetArcAngleAndEnd(pcbnew.EDA_ANGLE(90, pcbnew.DEGREES_T), True)
            self.b.Add(s)

    # -------------------------------------------------- footprints
    def add_fp(self, inst, x, y, rot):
        fp = load_fp(inst.part.footprint)
        fp.SetReference(inst.ref)
        fp.SetValue(inst.val)
        self.b.Add(fp)
        fp.SetOrientationDegrees(rot)
        fp.SetPosition(pt(x, y))
        sheet = uid("sheet", inst.group)
        sym = uid("sym", inst.ref, 1, inst.group)
        fp.SetPath(pcbnew.KIID_PATH(f"/{sheet}/{sym}"))
        props = {"Sheetname": inst.group, "Sheetfile": f"lfp8_{inst.group}.kicad_sch"}
        if inst.part.lcsc:
            props["LCSC"] = inst.part.lcsc
        if inst.part.mfr:
            props["MPN"] = inst.part.mfr
        props["JLC"] = inst.part.jlc
        for kk, vv in props.items():
            fp.SetProperty(kk, vv)
        attrs = fp.GetAttributes()
        if inst.part.jlc == "Hand":
            attrs |= pcbnew.FP_EXCLUDE_FROM_POS_FILES
        fp.SetAttributes(attrs)
        for p in fp.Pads():
            n = inst.conns.get(p.GetNumber())
            ni = self.net(n) if n else None
            if ni is not None:
                p.SetNet(ni)
        # reference text: small, on silk
        ref = fp.Reference()
        ref.SetTextSize(pcbnew.VECTOR2I(mm(0.8), mm(0.8)))
        ref.SetTextThickness(mm(0.12))
        fp.Value().SetVisible(False)
        self.fps[inst.ref] = fp
        return fp

    def pad_xy(self, spec):
        ref, num = spec.rsplit(".", 1)
        for p in self.fps[ref].Pads():
            if p.GetNumber() == num:
                return unpt(p.GetPosition())
        raise KeyError(spec)

    # -------------------------------------------------- tracks
    def track(self, dsl_net, layer, width, pts, locked=True):
        ni = self.net(dsl_net)
        lay = pcbnew.F_Cu if layer == "F" else pcbnew.B_Cu
        for a, b in zip(pts[:-1], pts[1:]):
            if abs(a[0] - b[0]) < 1e-6 and abs(a[1] - b[1]) < 1e-6:
                continue
            t = pcbnew.PCB_TRACK(self.b)
            t.SetStart(pt(*a))
            t.SetEnd(pt(*b))
            t.SetWidth(mm(width))
            t.SetLayer(lay)
            t.SetNet(ni)
            t.SetLocked(locked)
            self.b.Add(t)
            self.pre.append((dsl_net, layer, width, a, b))
            # keep the placer away from pre-routed copper
            hw = width / 2 + 0.3
            self.occ.mark((min(a[0], b[0]) - hw, min(a[1], b[1]) - hw, max(a[0], b[0]) + hw, max(a[1], b[1]) + hw))

    def via(self, dsl_net, x, y, d=VIA_D, drill=VIA_DRILL, locked=True):
        v = pcbnew.PCB_VIA(self.b)
        v.SetPosition(pt(x, y))
        v.SetWidth(mm(d))
        v.SetDrill(mm(drill))
        v.SetViaType(pcbnew.VIATYPE_THROUGH)
        v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
        v.SetNet(self.net(dsl_net))
        v.SetLocked(locked)
        self.b.Add(v)
        self.occ.mark((x - d / 2 - 0.3, y - d / 2 - 0.3, x + d / 2 + 0.3, y + d / 2 + 0.3))

    # -------------------------------------------------- zones
    def zone(self, dsl_net, layer, poly, priority=0, solid=False, name="", min_w=0.25, clearance=0.25):
        z = pcbnew.ZONE(self.b)
        z.SetLayer(layer)
        ni = self.net(dsl_net)
        if ni is not None:
            z.SetNet(ni)
        ol = z.Outline()
        ol.NewOutline()
        for x, y in poly:
            ol.Append(mm(x + OX), mm(y + OY))
        z.SetAssignedPriority(priority)
        z.SetMinThickness(mm(min_w))
        z.SetLocalClearance(mm(clearance))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL if solid else pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetThermalReliefGap(mm(0.3))
        z.SetThermalReliefSpokeWidth(mm(0.5))
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        if name:
            z.SetZoneName(name)
        self.b.Add(z)
        return z

    def keepout(self, layers, poly, tracks=True, vias=True, pour=True, fps=False):
        z = pcbnew.ZONE(self.b)
        z.SetIsRuleArea(True)
        ls = pcbnew.LSET()
        for l in layers:
            ls.AddLayer(l)
        z.SetLayerSet(ls)
        ol = z.Outline()
        ol.NewOutline()
        for x, y in poly:
            ol.Append(mm(x + OX), mm(y + OY))
        z.SetDoNotAllowTracks(tracks)
        z.SetDoNotAllowVias(vias)
        z.SetDoNotAllowCopperPour(pour)
        z.SetDoNotAllowPads(False)
        z.SetDoNotAllowFootprints(fps)
        self.b.Add(z)

    # -------------------------------------------------- greedy placement
    def greedy(self, insts, regions, margin, extra_pull=None, step=0.5):
        """Place insts one by one at the free spot that minimises the distance from
        each pad to the nearest already-placed pad of the same net (GND excluded)."""
        placed_pads = {}
        for ref, fp in self.fps.items():
            for p in fp.Pads():
                n = self.dsl_net_of(ref, p.GetNumber())
                if n and n != "GND":
                    placed_pads.setdefault(n, []).append(unpt(p.GetPosition()))
        for (n, layer, w, a, b) in self.pre:
            if n != "GND":
                placed_pads.setdefault(n, []).extend([a, b])
        todo = list(insts)
        while todo:
            def score(i):
                s = sum(1 for n in set(i.conns.values()) if n in placed_pads and n not in ("GND", "+5V"))
                return (s, len(i.conns))
            todo.sort(key=score, reverse=True)
            inst = todo.pop(0)
            best = None
            for rot in (0, 90, 180, 270) if len(inst.part.pins) > 1 else (0,):
                fp = load_fp(inst.part.footprint)
                fp.SetOrientationDegrees(rot)
                fp.SetPosition(pt(0, 0))
                cx0, cy0, cx1, cy1 = courtyard(fp)
                pads = [(unpt(p.GetPosition()), inst.conns.get(p.GetNumber())) for p in fp.Pads()]
                for (rx0, ry0, rx1, ry1) in regions:
                    xs = np.arange(rx0 - cx0, rx1 - cx1 + 1e-9, step)
                    ys = np.arange(ry0 - cy0, ry1 - cy1 + 1e-9, step)
                    if len(xs) == 0 or len(ys) == 0:
                        continue
                    X, Y = np.meshgrid(xs, ys)
                    X, Y = X.ravel(), Y.ravel()
                    cost = np.zeros_like(X)
                    for (px, py), n in pads:
                        if not n or n == "GND":
                            continue
                        tg = placed_pads.get(n)
                        if tg:
                            T = np.array(tg)
                            dx = (X + px)[:, None] - T[None, :, 0]
                            dy = (Y + py)[:, None] - T[None, :, 1]
                            dmin = np.sqrt(dx * dx + dy * dy).min(axis=1)
                            cost += dmin * (0.3 if n in ("+5V", "+3V3") else 1.0)
                        elif extra_pull and n in extra_pull:
                            cost += 0.5 * np.abs((Y + py) - extra_pull[n][1]) + 0.2 * np.abs((X + px) - extra_pull[n][0])
                    rcx, rcy = (rx0 + rx1) / 2, (ry0 + ry1) / 2
                    cost += 0.01 * (np.abs(X - rcx) + np.abs(Y - rcy))
                    order = np.argsort(cost, kind="stable")
                    for j in order[:4000]:
                        box = (X[j] + cx0, Y[j] + cy0, X[j] + cx1, Y[j] + cy1)
                        if self.occ.free(box, margin):
                            if best is None or cost[j] < best[0] - 1e-9:
                                best = (cost[j], X[j], Y[j], rot)
                            break
            if best is None:
                raise RuntimeError(f"no room for {inst.ref}")
            _, x, y, rot = best
            fp = self.add_fp(inst, x, y, rot)
            self.occ.mark(courtyard(fp), PART_MARGIN)
            for p in fp.Pads():
                n = inst.conns.get(p.GetNumber())
                if n and n != "GND":
                    placed_pads.setdefault(n, []).append(unpt(p.GetPosition()))

    def dsl_net_of(self, ref, pad):
        return self._inst[ref].conns.get(pad)

    # -------------------------------------------------- build
    def build(self):
        c = self.c
        self._inst = {i.ref: i for i in c.insts}
        self.outline()
        # holes and their keep-outs
        for idx, (hx, hy) in enumerate(HOLES):
            inst = self._inst[f"H{idx + 1}"]
            fp = self.add_fp(inst, hx, hy, 0)
            self.occ.mark((hx - 3.6, hy - 3.6, hx + 3.6, hy + 3.6))
        # +5 V bus (both layers) and its stitching
        yb = (BAND_Y0 + BAND_Y1) / 2
        wb = BAND_Y1 - BAND_Y0
        self.track("+5V", "B", wb, [(BAND_XL_B + wb / 2, yb), (BAND_XR - wb / 2, yb)])
        self.track("+5V", "F", wb, [(BAND_XL_F + wb / 2, yb), (BAND_XR - wb / 2, yb)])
        # feed from the input terminal (pad 1) into the bus, both layers, 5 mm wide
        fx = BAND_XL_B + 2.5
        for lay in ("B", "F"):
            self.track("+5V", lay, 5.0, [J1_XY, (fx, J1_XY[1]), (fx, yb)])
        x = BAND_XL_F + 3.0
        while x < BAND_XR - 3:
            for dy in (-1.8, 1.8):
                self.via("+5V", x, yb + dy, 0.8, 0.4)
            x += 6.0
        self.occ.mark((0, BAND_Y0 - 0.5, W, H))
        # channel power stages
        for k in range(1, 9):
            x0 = LM + (k - 1) * CW
            for ref, (lx, y, rot) in ch_fixed(k).items():
                fp = self.add_fp(self._inst[ref], x0 + lx, y, rot)
                self.occ.mark(courtyard(fp), PART_MARGIN)
        for k in range(1, 9):
            x0 = LM + (k - 1) * CW
            for net, layer, w, pts in ch_preroutes(k):
                P = [self.pad_xy(p) if isinstance(p, str) else (x0 + p[0], p[1]) for p in pts]
                self.track(f"{net}_{k}" if not net.startswith("+") else net, layer, w, P)
            for padspec, vias in ch_gnd_vias(k):
                pxy = self.pad_xy(padspec)
                for (vx, vy) in vias:
                    self.via("GND", x0 + vx, vy)
                    self.track("GND", "F", 0.6, [pxy, (x0 + vx, vy)])
        # common section fixed parts
        for ref, (x, y, rot) in COMMON_FIXED.items():
            fp = self.add_fp(self._inst[ref], x, y, rot)
            self.occ.mark(courtyard(fp), PART_MARGIN)
        # left-margin bulk parts sit on the bus: +5V / GND vias next to them
        # channel 1 small parts (greedy), then replicate to channels 2..8
        x0 = LM
        region = [(x0 + 0.4, Y_SIG + 0.5, x0 + CW - 0.4, 127.0)]
        outside = {n for i in c.insts if i.group != "ch1" for n in i.conns.values()}
        pull = {n: (x0 + CW / 2, Y_SIG) for i in c.by_group("ch1") for n in i.conns.values()
                if n and n in outside and n not in ("GND", "+5V")}
        ch1_small = [i for i in c.by_group("ch1") if i.ref not in self.fps]
        self.greedy(ch1_small, region, margin=0.0, extra_pull=pull)
        for k in range(2, 9):
            dx = (k - 1) * CW
            for i1 in ch1_small:
                refk = i1.ref[0] + str(k) + i1.ref[2:] if i1.ref[1] == "1" else None
                fp1 = self.fps[i1.ref]
                x, y = unpt(fp1.GetPosition())
                rot = fp1.GetOrientationDegrees()
                fp = self.add_fp(self._inst[refk], x + dx, y, rot)
                self.occ.mark(courtyard(fp), PART_MARGIN)
        # remaining common parts
        for g in ("power", "safety", "adc", "logic", "mcu"):
            rest = [i for i in c.by_group(g) if i.ref not in self.fps]
            self.greedy(rest, REGIONS[g], margin=0.0)
        missing = [i.ref for i in c.insts if i.ref not in self.fps]
        assert not missing, missing

    def zones(self):
        # GND plane on B.Cu (everything above the +5 V bus)
        self.zone("GND", pcbnew.B_Cu, [(0.3, 0.3), (W - 0.3, 0.3), (W - 0.3, BAND_Y0 - 0.6), (0.3, BAND_Y0 - 0.6)],
                  priority=0, name="GND_B")
        # +5 V bus zones (same outline as the bus tracks, a bit wider)
        for lay, xl in ((pcbnew.B_Cu, BAND_XL_B), (pcbnew.F_Cu, BAND_XL_F)):
            self.zone("+5V", lay, [(xl - 1.0, BAND_Y0), (BAND_XR, BAND_Y0), (BAND_XR, BAND_Y1), (xl - 1.0, BAND_Y1)],
                      priority=2, solid=True, name="+5V_BUS")

    def save(self):
        pcbnew.SaveBoard(PCB, self.b)
        return PCB


def board_fixups(board):
    """Post-placement fixes, idempotent (also run after routing):
    * ESP32-C3 module footprint: its 12 thermal vias have 0.2 mm drills (JLCPCB 2-layer
      standard minimum is 0.3 mm) - keep 6 of them (1.1 mm pitch) at 0.3 mm.
    * silkscreen: references of 2/3-pad passives, diodes and transistors move to F.Fab
      (they would overlap on a board this dense); ICs, connectors, switches keep theirs.
    """
    for fp in board.GetFootprints():
        ref = fp.GetReference()
        if "ESP32-C3-WROOM" in fp.GetFPIDAsString():
            org = fp.GetPosition()
            for p in fp.Pads():
                if p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH and p.GetDrillSize().x < pcbnew.FromMM(0.3):
                    # position relative to the footprint (the module is never rotated here)
                    rx = round((p.GetPosition().x - org.x) / 1e6, 2)
                    if abs(rx - 0.41) < 0.05 or abs(rx - 1.51) < 0.05:
                        p.SetDrillSize(pcbnew.VECTOR2I(pcbnew.FromMM(0.3), pcbnew.FromMM(0.3)))
                    else:
                        # lies inside the module's central GND pad anyway: make it a plain SMD pad
                        p.SetAttribute(pcbnew.PAD_ATTRIB_SMD)
                        p.SetDrillSize(pcbnew.VECTOR2I(0, 0))
                        p.SetLayerSet(pcbnew.PAD.SMDMask())
        prefix = re.sub(r"\d.*", "", ref)
        if prefix in ("R", "C", "D", "Q", "F") and len(fp.Pads()) <= 4:
            fp.Reference().SetLayer(pcbnew.F_Fab)
        if prefix == "H":
            fp.Reference().SetVisible(False)


def write_project():
    """KiCad 7 project file with net classes and JLCPCB-friendly design rules."""
    pro = os.path.join(KDIR, "lfp8.kicad_pro")
    classes = []
    for name, (w, cl, vd, vdr) in NETCLASSES.items():
        classes.append({"name": name, "bus_width": 12, "clearance": cl, "diff_pair_gap": 0.25,
                        "diff_pair_via_gap": 0.25, "diff_pair_width": 0.2, "line_style": 0,
                        "microvia_diameter": 0.3, "microvia_drill": 0.1, "pcb_color": "rgba(0, 0, 0, 0.000)",
                        "schematic_color": "rgba(0, 0, 0, 0.000)", "track_width": w, "via_diameter": vd,
                        "via_drill": vdr, "wire_width": 6})
    patterns = [{"netclass": "Supply", "pattern": p} for p in ("+5V", "GND", "+3V3")]
    for base in ("BPF", "BP", "BN", "CH1", "CH2", "DLOAD", "MIDISO", "SHP"):
        patterns.append({"netclass": "Power", "pattern": f"/ch*/{base}_*"})
    data = {
        "board": {"design_settings": {
            "defaults": {"board_outline_line_width": 0.1, "copper_line_width": 0.2, "copper_text_size_h": 1.5,
                         "copper_text_size_v": 1.5, "copper_text_thickness": 0.3, "other_line_width": 0.1,
                         "silk_line_width": 0.15, "silk_text_size_h": 1.0, "silk_text_size_v": 1.0,
                         "silk_text_thickness": 0.15},
            "rules": {"min_clearance": 0.15, "min_track_width": 0.15, "min_via_diameter": 0.5,
                      "min_through_hole_diameter": 0.3, "min_via_annular_width": 0.13,
                      "min_hole_to_hole": 0.5, "min_hole_clearance": 0.25, "min_copper_edge_clearance": 0.3,
                      "min_silk_clearance": 0.0, "min_text_height": 0.8, "min_text_thickness": 0.12,
                      "solder_mask_clearance": 0.05, "solder_mask_min_width": 0.0},
            "track_widths": [0.0, 0.25, 0.5, 0.8, 1.2], "via_dimensions": [{"diameter": 0.0, "drill": 0.0},
                                                                         {"diameter": 0.6, "drill": 0.3},
                                                                         {"diameter": 0.8, "drill": 0.4}]}},
        "meta": {"filename": "lfp8.kicad_pro", "version": 1},
        "net_settings": {"classes": classes, "meta": {"version": 3}, "net_colors": None,
                         "netclass_assignments": None, "netclass_patterns": patterns},
        "pcbnew": {"last_paths": {"gencad": "", "idf": "", "netlist": "", "specctra_dsn": "", "step": "",
                                  "vrml": ""}, "page_layout_descr_file": ""},
        "schematic": {"legacy_lib_dir": "", "legacy_lib_list": []},
        "sheets": [], "text_variables": {}, "libraries": {"pinned_footprint_libs": [], "pinned_symbol_libs": []},
    }
    json.dump(data, open(pro, "w"), indent=2)


def main():
    bb = BoardBuilder()
    bb.build()
    bb.zones()
    board_fixups(bb.b)
    path = bb.save()
    write_project()
    dsn = os.path.join(KDIR, "lfp8.dsn")
    ok = pcbnew.ExportSpecctraDSN(bb.b, dsn)
    print("placed", len(bb.fps), "footprints;", "DSN" if ok else "DSN FAILED", dsn)
    print("board", path)


if __name__ == "__main__":
    main()
