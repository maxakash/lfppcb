"""Generate the KiCad 7 schematic for the LFP-8 board from the netlist DSL.

The schematic is *label based*: every symbol pin carries a net label (local label
for nets that live on one sheet, global label for nets shared between sheets,
power symbols for GND/+5V/+3V3).  The connectivity therefore equals the DSL
netlist by construction, and `verify_netlist.py` proves it by exporting the
netlist with kicad-cli and comparing it pin by pin.

Output: hardware/kicad/lfp8.kicad_sch (root) + one sub-sheet per group.
"""
import json
import os
import re
import uuid

import lfp8_circuit
from sexpr import parse, find, find_all, dump, Sym

HERE = os.path.dirname(os.path.abspath(__file__))
KDIR = os.path.normpath(os.path.join(HERE, "..", "kicad"))
SYMDIR = "/usr/share/kicad/symbols"
PROJECT = "lfp8"
NS = uuid.UUID("6b1f0c3e-6f1e-4d55-9c55-1f8f1c6c1a01")

POWER_NETS = {"GND": "power:GND", "+5V": "power:+5V", "+3V3": "power:+3V3"}
SHEETS = [("power", "Power input, 3.3 V LDO, 2.495 V reference, ISET DAC"),
          ("safety", "SAFE_EN chain: watchdog, rail UV/OV, board over-temperature, E-STOP"),
          ("mcu", "ESP32-C3 module, USB, buttons, fan"),
          ("adc", "ADS1115 + CD4051 measurement multiplexers"),
          ("logic", "74HC595 control word, level shifters, SR output enable")] + \
         [(f"ch{k}", f"Cell channel {k}: charger, discharger, protection, measurement") for k in range(1, 9)] + \
         [("mech", "Mounting holes")]

GRID = 2.54


def uid(*parts):
    return str(uuid.uuid5(NS, "/".join(map(str, parts))))


def snap(v, g=GRID):
    return round(round(v / g) * g, 4)


def fmt(v):
    s = f"{v:.4f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


# ---------------------------------------------------------------- symbol library access
_libs = {}


def _lib(name):
    if name not in _libs:
        path = os.path.join(KDIR, "lib", "LFP8.kicad_sym") if name == "LFP8" else os.path.join(SYMDIR, name + ".kicad_sym")
        _libs[name] = {s[1]: s for s in find_all(parse(open(path).read()), "symbol")}
    return _libs[name]


class LibSym:
    """A flattened library symbol (``extends`` resolved) with pin geometry."""

    def __init__(self, lib_id):
        self.lib_id = lib_id
        lib, name = lib_id.split(":")
        raw = _lib(lib)[name]
        ext = find(raw, "extends")
        if ext:
            base = _lib(lib)[ext[1]]
            node = [Sym("symbol"), lib_id]
            # header flags from base, properties from the derived symbol (fallback base)
            props = {p[1]: p for p in find_all(base, "property")}
            for p in find_all(raw, "property"):
                props[p[1]] = p
            for x in base[2:]:
                if isinstance(x, list) and x[0] in ("property", "symbol", "extends"):
                    continue
                node.append(x)
            node += list(props.values())
            for sub in find_all(base, "symbol"):
                sub = list(sub)
                sub[1] = sub[1].replace(ext[1], name, 1)
                node.append(sub)
        else:
            node = [Sym("symbol"), lib_id] + [x for x in raw[2:]]
        self.node = node
        self.name = name
        self.is_power = find(node, "power") is not None
        # pins: number -> dict
        self.pins = {}
        self.units = set()
        self.bbox_by_unit = {}
        for sub in find_all(node, "symbol"):
            m = re.match(r".*_(\d+)_(\d+)$", sub[1])
            unit = int(m.group(1))
            if unit:
                self.units.add(unit)
            for g in sub[2:]:
                if not isinstance(g, list):
                    continue
                for (x, y) in _shape_points(g):
                    self._grow(unit, x, y)
            for p in find_all(sub, "pin"):
                at = find(p, "at")
                ln = float(find(p, "length")[1])
                x, y, a = float(at[1]), float(at[2]), int(float(at[3])) if len(at) > 3 else 0
                self.pins[find(p, "number")[1]] = dict(x=x, y=y, angle=a % 360, unit=unit,
                                                      etype=str(p[1]), name=find(p, "name")[1])
                self._grow(unit, x, y)
        if not self.units:
            self.units = {1}

    def _grow(self, unit, x, y):
        b = self.bbox_by_unit.setdefault(unit, [x, y, x, y])
        b[0], b[1], b[2], b[3] = min(b[0], x), min(b[1], y), max(b[2], x), max(b[3], y)

    def bbox(self, unit):
        bs = [self.bbox_by_unit[u] for u in (0, unit) if u in self.bbox_by_unit]
        return (min(b[0] for b in bs), min(b[1] for b in bs), max(b[2] for b in bs), max(b[3] for b in bs))

    def unit_pins(self, unit):
        return {n: p for n, p in self.pins.items() if p["unit"] in (0, unit)}


def _shape_points(g):
    k = g[0]
    if k == "rectangle":
        s, e = find(g, "start"), find(g, "end")
        return [(float(s[1]), float(s[2])), (float(e[1]), float(e[2]))]
    if k in ("polyline", "bezier"):
        pts = find(g, "pts")
        return [(float(p[1]), float(p[2])) for p in find_all(pts, "xy")]
    if k == "circle":
        c, r = find(g, "center"), float(find(g, "radius")[1])
        cx, cy = float(c[1]), float(c[2])
        return [(cx - r, cy - r), (cx + r, cy + r)]
    if k == "arc":
        return [(float(find(g, t)[1]), float(find(g, t)[2])) for t in ("start", "mid", "end")]
    return []


_symcache = {}


def libsym(lib_id):
    if lib_id not in _symcache:
        _symcache[lib_id] = LibSym(lib_id)
    return _symcache[lib_id]


# ---------------------------------------------------------------- S-expression snippets
def eff(size=1.27, justify=None, hide=False):
    e = [Sym("effects"), [Sym("font"), [Sym("size"), size, size]]]
    if justify:
        e.append([Sym("justify")] + [Sym(j) for j in justify.split()])
    if hide:
        e.append(Sym("hide"))
    return e


def at(x, y, a=0):
    return [Sym("at"), float(x), float(y), int(a)]


def prop(name, value, x, y, a=0, justify=None, hide=False):
    return [Sym("property"), name, value, at(x, y, a), eff(justify=justify, hide=hide)]


def text_w(s):
    return 1.27 * 0.85 * len(s) + 1.0


LABEL_JUST = {0: "left bottom", 90: "left bottom", 180: "right bottom", 270: "right bottom"}
GLABEL_JUST = {0: "left", 90: "left", 180: "right", 270: "right"}


# ---------------------------------------------------------------- sheet builder
class Sheet:
    def __init__(self, name, title, root_uuid, sheet_uuid, page):
        self.name, self.title = name, title
        self.root_uuid, self.uuid, self.page = root_uuid, sheet_uuid, page
        self.items = []
        self.lib_ids = set()
        self.path = f"/{root_uuid}/{sheet_uuid}" if sheet_uuid else f"/{root_uuid}"

    def symbol(self, inst_ref, lib_id, x, y, unit, fields, pins_uuid_key, value, footprint, dnp=False, extra=None):
        ls = libsym(lib_id)
        self.lib_ids.add(lib_id)
        u = uid("sym", inst_ref, unit, self.name)
        bx0, by0, bx1, by1 = ls.bbox(unit)
        node = [Sym("symbol"), [Sym("lib_id"), lib_id], at(x, y, 0), [Sym("unit"), unit],
                [Sym("in_bom"), Sym("no" if ls.is_power or not footprint else "yes")],
                [Sym("on_board"), Sym("no" if ls.is_power else "yes")],
                [Sym("dnp"), Sym("yes" if dnp else "no")],
                [Sym("uuid"), u]]
        ref = inst_ref
        hide_ref = ls.is_power
        node.append(prop("Reference", ref, x + bx1 + 1.27, y - by1 - 1.27, justify="left", hide=hide_ref))
        node.append(prop("Value", value, x + bx1 + 1.27, y - by0 + 1.27 if not ls.is_power else y + 3.0,
                         justify="left" if not ls.is_power else None))
        node.append(prop("Footprint", footprint or "", x, y, hide=True))
        node.append(prop("Datasheet", "~", x, y, hide=True))
        for k, v in (extra or {}).items():
            node.append(prop(k, v, x, y, hide=True))
        for n in ls.unit_pins(unit):
            node.append([Sym("pin"), n, [Sym("uuid"), uid("pin", inst_ref, n, unit)]])
        node.append([Sym("instances"), [Sym("project"), PROJECT,
                                        [Sym("path"), self.path, [Sym("reference"), ref], [Sym("unit"), unit]]]])
        self.items.append(node)
        return u

    def label(self, net, x, y, angle, glob):
        if glob:
            node = [Sym("global_label"), net, [Sym("shape"), Sym("bidirectional")], at(x, y, angle),
                    [Sym("fields_autoplaced")], eff(justify=GLABEL_JUST[angle]), [Sym("uuid"), uid("gl", self.name, net, x, y)],
                    [Sym("property"), "Intersheetrefs", "${INTERSHEET_REFS}", at(x, y, angle),
                     eff(justify=GLABEL_JUST[angle], hide=True)]]
        else:
            node = [Sym("label"), net, at(x, y, angle), [Sym("fields_autoplaced")],
                    eff(justify=LABEL_JUST[angle]), [Sym("uuid"), uid("lb", self.name, net, x, y)]]
        self.items.append(node)

    def no_connect(self, x, y):
        self.items.append([Sym("no_connect"), at(x, y)[:3], [Sym("uuid"), uid("nc", self.name, x, y)]])

    def text(self, s, x, y, size=2.0):
        self.items.append([Sym("text"), s, at(x, y, 0), eff(size=size, justify="left bottom"),
                           [Sym("uuid"), uid("tx", self.name, s[:40], x, y)]])

    def write(self, paper, extra_tail=()):
        lib_nodes = [libsym(l).node for l in sorted(self.lib_ids)]
        tb = [Sym("title_block"), [Sym("title"), f"LFP-8 8-channel LFP cell tester - {self.name}"],
              [Sym("date"), "2026-10-01"], [Sym("rev"), "1.0"],
              [Sym("company"), "github.com/maxakash/lfppcb"],
              [Sym("comment"), 1, self.title],
              [Sym("comment"), 2, "Generated by hardware/gen/make_sch.py from the netlist DSL - do not edit by hand"]]
        doc = [Sym("kicad_sch"), [Sym("version"), 20230121], [Sym("generator"), Sym("eeschema")],
               [Sym("uuid"), self.root_uuid if self.uuid is None else uid("file", self.name)], [Sym("paper"), paper], tb,
               [Sym("lib_symbols")] + lib_nodes] + self.items + list(extra_tail)
        fn = os.path.join(KDIR, f"{PROJECT}.kicad_sch" if self.uuid is None else f"{PROJECT}_{self.name}.kicad_sch")
        open(fn, "w").write(dump(doc) + "\n")
        return fn


PAPERS = [("A4", 297, 210), ("A3", 420, 297), ("A2", 594, 420), ("A1", 841, 594), ("A0", 1189, 841)]


def place_group(sheet, insts, net_sheets, single_nets, flags):
    """Shelf-pack the symbols of one group and label all pins."""
    # one placement item per (instance, unit)
    items = []
    for i in insts:
        ls = libsym(i.part.symbol)
        units = sorted(ls.units) if i.part.units else [1]
        for unit in units:
            items.append((i, unit))
    for net, lib_id in flags:
        items.append((("FLAG", net, lib_id), 1))

    max_w = 560.0
    x0, y0 = 20.0, 30.0
    cx, cy, shelf_h = x0, y0, 0.0
    max_x = 0.0
    for it, unit in items:
        if isinstance(it, tuple):
            _, net, lib_id = it
            ls = libsym(lib_id)
            pins = ls.unit_pins(1)
        else:
            ls = libsym(it.part.symbol)
            pins = ls.unit_pins(unit)
        bx0, by0, bx1, by1 = ls.bbox(unit)
        # label extents on each side
        ext = {0: 0.0, 90: 0.0, 180: 0.0, 270: 0.0}
        for n, p in pins.items():
            net = net if isinstance(it, tuple) else it.conns.get(n, "")
            if not net or net in POWER_NETS:
                lw = 6.0 if net in POWER_NETS else 2.0
            else:
                lw = text_w(net) + (3.0 if net_sheets.get(net, 1) > 1 else 0)
            out = (p["angle"] + 180) % 360
            ext[out] = max(ext[out], lw)
        valw = text_w(it.val if not isinstance(it, tuple) else "PWR_FLAG")
        left = -bx0 + ext[180] + 2.54
        right = bx1 + max(ext[0], valw + 2.0) + 2.54
        top = by1 + ext[90] + 3.0
        bot = -by0 + ext[270] + 3.0
        w, h = left + right, top + bot
        if cx + w > x0 + max_w and cx > x0:
            cx = x0
            cy += shelf_h + 5.08
            shelf_h = 0.0
        sx, sy = snap(cx + left), snap(cy + top)
        if isinstance(it, tuple):
            _, net, lib_id = it
            ref = f"#FLG{len([1 for x in sheet.items if x[0] == 'symbol']) + 1:02d}"
            sheet.symbol(ref, lib_id, sx, sy, 1, {}, None, "PWR_FLAG", "")
            for n, p in pins.items():
                _attach(sheet, net, sx + p["x"], sy - p["y"], p["angle"], net_sheets, single_nets, tag=ref)
        else:
            extra = {}
            if it.part.lcsc:
                extra["LCSC"] = it.part.lcsc
            if it.part.mfr:
                extra["MPN"] = it.part.mfr
            extra["JLC"] = it.part.jlc
            sheet.symbol(it.ref, it.part.symbol, sx, sy, unit, {}, None, it.val, it.part.footprint, dnp=it.dnp, extra=extra)
            for n, p in pins.items():
                _attach(sheet, it.conns.get(n, ""), sx + p["x"], sy - p["y"], p["angle"], net_sheets, single_nets,
                        tag=f"{it.ref}.{n}")
        cx += w
        max_x = max(max_x, cx)
        shelf_h = max(shelf_h, h)
    return max_x + 20, cy + shelf_h + 20


_pwr_count = [0]


def _attach(sheet, net, x, y, pin_angle, net_sheets, single_nets, tag=""):
    x, y = round(x, 4), round(y, 4)
    out = (pin_angle + 180) % 360
    if not net or net in single_nets:
        sheet.no_connect(x, y)
        return
    if net in POWER_NETS and out in (0, 180):
        # sideways power pins (connector rows, IC power pins): a global label of the same
        # name joins the power net and does not collide with the neighbouring pins
        sheet.label(net, x, y, out, True)
        return
    if net in POWER_NETS:
        _pwr_count[0] += 1
        ref = f"#PWR{_pwr_count[0]:04d}"
        lib_id = POWER_NETS[net]
        ls = libsym(lib_id)
        # power symbol pin is at its origin; orient GND downwards, rails upwards
        sheet.lib_ids.add(lib_id)
        rot = {"GND": {270: 0, 90: 180, 0: 90, 180: 270}, }.get(net, {90: 0, 270: 180, 0: 270, 180: 90})[out] \
            if net == "GND" else {90: 0, 270: 180, 0: 270, 180: 90}[out]
        u = uid("pwr", sheet.name, tag)
        node = [Sym("symbol"), [Sym("lib_id"), lib_id], at(x, y, rot), [Sym("unit"), 1],
                [Sym("in_bom"), Sym("yes")], [Sym("on_board"), Sym("yes")], [Sym("dnp"), Sym("no")],
                [Sym("uuid"), u],
                prop("Reference", ref, x, y, hide=True),
                prop("Value", net, *_beyond(x, y, out, 5.0 if out in (0, 180) else 4.5),
                     justify={0: "left", 180: "right"}.get(out)),
                prop("Footprint", "", x, y, hide=True),
                prop("Datasheet", "", x, y, hide=True),
                [Sym("pin"), "1", [Sym("uuid"), uid("pwrpin", sheet.name, tag)]],
                [Sym("instances"), [Sym("project"), PROJECT,
                                    [Sym("path"), sheet.path, [Sym("reference"), ref], [Sym("unit"), 1]]]]]
        sheet.items.append(node)
        return
    sheet.label(net, x, y, out, net_sheets.get(net, 1) > 1)


def _beyond(x, y, out, d):
    dx, dy = {0: (1, 0), 90: (0, -1), 180: (-1, 0), 270: (0, 1)}[out]
    return round(x + dx * d, 4), round(y + dy * d, 4)


def build():
    c = lfp8_circuit.build_board()
    problems = c.check(allow_single=lfp8_circuit.SINGLE_OK)
    assert not problems, problems
    nets = c.nets()
    single = {n for n, p in nets.items() if len(p) == 1}
    groups = {}
    for i in c.insts:
        groups.setdefault(i.group, []).append(i)
    net_groups = {}
    for i in c.insts:
        for n in i.conns.values():
            if n:
                net_groups.setdefault(n, set()).add(i.group)
    net_sheets = {n: len(g) for n, g in net_groups.items()}
    # every net that is fed only through passive pins but loads power_in pins gets a PWR_FLAG
    flags = []
    for n, pins in nets.items():
        types = []
        for ref, pad in pins:
            inst = c.get(ref)
            types.append(libsym(inst.part.symbol).pins[pad]["etype"])
        if "power_in" in types and "power_out" not in types:
            flags.append((n, "power:PWR_FLAG"))
    if not any(n == "GND" for n, _ in flags):
        flags.append(("GND", "power:PWR_FLAG"))
    flag_nets = [n for n, _ in flags]
    for n in flag_nets:
        net_sheets[n] = max(net_sheets.get(n, 1), 2) if n not in POWER_NETS else 2

    root_uuid = uid("root")
    os.makedirs(KDIR, exist_ok=True)
    root = Sheet("root", "Top level: sheet index", root_uuid, None, "1")
    written = []
    sheet_nodes = []
    sx, sy = 25.4, 50.8
    for idx, (g, title) in enumerate(SHEETS):
        if g not in groups:
            continue
        page = str(idx + 2)
        su = uid("sheet", g)
        sh = Sheet(g, title, root_uuid, su, page)
        _pwr_count[0] = _pwr_count[0]  # keep running power ref counter unique across sheets
        w, h = place_group(sh, groups[g], net_sheets, single, flags if g == "power" else [])
        paper = next(p for p, pw, ph in PAPERS if w <= pw and h <= ph)
        sh.text(title, 20, 18, size=3.0)
        fn = sh.write(paper)
        written.append(fn)
        # sheet symbol on the root page
        bw, bh = 76.2, 15.24
        node = [Sym("sheet"), [Sym("at"), sx, sy], [Sym("size"), bw, bh], [Sym("fields_autoplaced")],
                [Sym("stroke"), [Sym("width"), 0.1524], [Sym("type"), Sym("solid")]],
                [Sym("fill"), [Sym("color"), 0, 0, 0, 0.0]],
                [Sym("uuid"), su],
                [Sym("property"), "Sheetname", g, at(sx, sy - 0.7112, 0), eff(justify="left bottom")],
                [Sym("property"), "Sheetfile", f"{PROJECT}_{g}.kicad_sch", at(sx, sy + bh + 0.5842, 0),
                 eff(justify="left top")],
                [Sym("instances"), [Sym("project"), PROJECT, [Sym("path"), f"/{root_uuid}", [Sym("page"), page]]]]]
        sheet_nodes.append(node)
        root.text(title, sx + bw + 5.08, sy + 8.0, size=1.8)
        sy += 22.86
        if sy > 260:
            sy = 50.8
            sx += 200
    root.items += sheet_nodes
    root.text("LFP-8: 8-channel LiFePO4 32140/33140 charger / discharger / IR tester (5 boards = 40 cells)", 25.4, 30, 3.5)
    root.text("Single source of truth: hardware/gen/lfp8_circuit.py.  All nets are connected by labels; "
              "power nets use power symbols.", 25.4, 38, 1.8)
    root.items.append([Sym("sheet_instances"), [Sym("path"), "/", [Sym("page"), "1"]]])
    written.insert(0, root.write("A3"))
    # minimal project file
    pro = os.path.join(KDIR, f"{PROJECT}.kicad_pro")
    if not os.path.exists(pro):
        json.dump({"meta": {"filename": f"{PROJECT}.kicad_pro", "version": 1},
                   "board": {}, "schematic": {}, "libraries": {}, "sheets": [], "text_variables": {}},
                  open(pro, "w"), indent=2)
    # project symbol / footprint library tables
    open(os.path.join(KDIR, "sym-lib-table"), "w").write(
        '(sym_lib_table\n  (lib (name "LFP8")(type "KiCad")(uri "${KIPRJMOD}/lib/LFP8.kicad_sym")(options "")(descr "LFP-8 project symbols"))\n)\n')
    open(os.path.join(KDIR, "fp-lib-table"), "w").write(
        '(fp_lib_table\n  (lib (name "LFP8")(type "KiCad")(uri "${KIPRJMOD}/lib/LFP8.pretty")(options "")(descr "LFP-8 project footprints"))\n)\n')
    return c, written, root_uuid


if __name__ == "__main__":
    c, files, _ = build()
    for f in files:
        print("wrote", os.path.relpath(f, KDIR))
