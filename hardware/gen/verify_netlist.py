"""Prove that the KiCad schematic (and, optionally, the PCB) implements exactly the
DSL netlist that the SPICE simulations used.

    python3 verify_netlist.py            # schematic: kicad-cli netlist export vs DSL
    python3 verify_netlist.py --pcb      # also compare the routed board's pad nets

Exit code 0 = identical connectivity.
"""
import os
import subprocess
import sys
import tempfile

import lfp8_circuit
from sexpr import parse, find, find_all

HERE = os.path.dirname(os.path.abspath(__file__))
KDIR = os.path.normpath(os.path.join(HERE, "..", "kicad"))


def dsl_nets():
    c = lfp8_circuit.build_board()
    nets = {}
    for n, pins in c.nets().items():
        if len(pins) == 1:          # deliberately unconnected (no-connect flag in the schematic)
            continue
        nets[n] = frozenset(pins)
    return c, nets


def sch_nets():
    out = os.path.join(tempfile.mkdtemp(), "lfp8.net")
    subprocess.run(["kicad-cli", "sch", "export", "netlist", "--format", "kicadsexpr", "-o", out,
                    os.path.join(KDIR, "lfp8.kicad_sch")], check=True, capture_output=True)
    tree = parse(open(out).read())
    comps = {}
    for comp in find_all(find(tree, "components"), "comp"):
        ref = find(comp, "ref")[1]
        fields = {f[1][1]: (f[2] if len(f) > 2 else "") for f in find_all(find(comp, "fields") or [], "field")}
        comps[ref] = dict(value=find(comp, "value")[1], footprint=(find(comp, "footprint") or [None, ""])[1],
                          fields=fields)
    nets = {}
    for net in find_all(find(tree, "nets"), "net"):
        name = find(net, "name")[1].split("/")[-1]   # local labels are named /<sheet>/<net>
        pins = frozenset((find(n, "ref")[1], find(n, "pin")[1]) for n in find_all(net, "node")
                         if not find(n, "ref")[1].startswith("#"))
        if len(pins) > 1:
            nets[name] = pins
    return comps, nets


def pcb_nets():
    import json
    code = r'''
import pcbnew, json, sys
b = pcbnew.LoadBoard(sys.argv[1])
nets = {}
fps = {}
for fp in b.GetFootprints():
    fps[fp.GetReference()] = fp.GetFPIDAsString()
    for p in fp.Pads():
        n = p.GetNetname()
        if n and p.GetNumber():
            nets.setdefault(n, []).append((fp.GetReference(), p.GetNumber()))
print(json.dumps({"nets": nets, "fps": fps}))
'''
    r = subprocess.run(["/usr/bin/python3", "-c", code, os.path.join(KDIR, "lfp8.kicad_pcb")],
                       check=True, capture_output=True, text=True)
    d = json.loads(r.stdout.strip().splitlines()[-1])
    nets = {k.split("/")[-1]: frozenset(map(tuple, v)) for k, v in d["nets"].items() if len(v) > 1}
    return d["fps"], nets


def compare(label, ref_nets, got_nets):
    errors = []
    by_pins = {v: k for k, v in got_nets.items()}
    for name, pins in ref_nets.items():
        if name in got_nets:
            if got_nets[name] != pins:
                errors.append(f"{label}: net {name}: missing {sorted(pins - got_nets[name])} extra {sorted(got_nets[name] - pins)}")
        elif pins in by_pins:
            errors.append(f"{label}: net {name} present but named {by_pins[pins]}")
        else:
            errors.append(f"{label}: net {name} missing")
    for name in got_nets:
        if name not in ref_nets and got_nets[name] not in set(ref_nets.values()):
            errors.append(f"{label}: unexpected net {name} {sorted(got_nets[name])}")
    return errors


def main():
    c, ref = dsl_nets()
    comps, sn = sch_nets()
    errors = compare("schematic", ref, sn)
    refs = {i.ref for i in c.insts}
    if set(comps) != refs:
        errors.append(f"schematic components differ: missing {sorted(refs - set(comps))} extra {sorted(set(comps) - refs)}")
    for i in c.insts:
        if i.ref in comps:
            if comps[i.ref]["footprint"] != i.part.footprint:
                errors.append(f"{i.ref}: footprint {comps[i.ref]['footprint']} != {i.part.footprint}")
            if i.part.lcsc and comps[i.ref]["fields"].get("LCSC") != i.part.lcsc:
                errors.append(f"{i.ref}: LCSC field {comps[i.ref]['fields'].get('LCSC')} != {i.part.lcsc}")
    print(f"schematic: {len(comps)} components, {len(sn)} nets (DSL {len(refs)} parts, {len(ref)} multi-pin nets)")
    if "--pcb" in sys.argv:
        fps, pn = pcb_nets()
        errors += compare("pcb", ref, pn)
        if set(fps) != refs:
            errors.append(f"pcb footprints differ: missing {sorted(refs - set(fps))} extra {sorted(set(fps) - refs)}")
        print(f"pcb: {len(fps)} footprints, {len(pn)} nets")
    for e in errors[:50]:
        print("ERROR", e)
    print("RESULT:", "PASS" if not errors else f"FAIL ({len(errors)} errors)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
