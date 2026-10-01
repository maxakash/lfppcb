"""Tiny netlist DSL shared by the SPICE netlister and the KiCad generators.

    c = Circuit("LFP-8")
    c.add("R101", resistor("10k"), {"1": "NB_1", "2": "GND"}, group="ch1")

Connections are keyed by *pad number* (see parts.PartDef.pins).
"""
from dataclasses import dataclass, field
from typing import Dict, List, Optional
import re

from parts import PartDef


@dataclass
class Inst:
    ref: str
    part: PartDef
    conns: Dict[str, str]          # pad number -> net
    group: str = "board"           # sheet / placement group ("ch1".."ch8", "power", ...)
    value: Optional[str] = None
    params: Dict[str, str] = field(default_factory=dict)   # SPICE params (e.g. vos1)
    dnp: bool = False
    note: str = ""

    @property
    def val(self) -> str:
        return self.value or self.part.value or self.part.key

    def net(self, pin_name: str) -> str:
        for num, name in self.part.pins.items():
            if name == pin_name:
                return self.conns.get(num, "")
        raise KeyError(f"{self.ref}: no pin named {pin_name}")


class Circuit:
    def __init__(self, name: str):
        self.name = name
        self.insts: List[Inst] = []
        self._refs = set()

    def add(self, ref: str, part: PartDef, conns: Dict[str, str], group="board", **kw) -> Inst:
        if ref in self._refs:
            raise ValueError(f"duplicate reference {ref}")
        # allow connecting by pin *name* as well as pad number
        fixed = {}
        for k, v in conns.items():
            if k in part.pins:
                fixed[k] = v
            else:
                nums = [n for n, nm in part.pins.items() if nm == k]
                if len(nums) != 1:
                    raise KeyError(f"{ref}: unknown/ambiguous pin {k!r} for {part.key}")
                fixed[nums[0]] = v
        unknown = set(fixed) - set(part.pins)
        if unknown:
            raise KeyError(f"{ref}: pads {unknown} not in {part.key}")
        self._refs.add(ref)
        inst = Inst(ref, part, fixed, group, **kw)
        self.insts.append(inst)
        return inst

    # ------------------------------------------------------------ queries
    def nets(self) -> Dict[str, List[tuple]]:
        out: Dict[str, List[tuple]] = {}
        for i in self.insts:
            for pad, net in i.conns.items():
                if net:
                    out.setdefault(net, []).append((i.ref, pad))
        return out

    def by_group(self, group: str) -> List[Inst]:
        return [i for i in self.insts if i.group == group]

    def get(self, ref: str) -> Inst:
        for i in self.insts:
            if i.ref == ref:
                return i
        raise KeyError(ref)

    def check(self, allow_single=()):
        """Structural lint: every pad connected, no single-pin nets."""
        problems = []
        for i in self.insts:
            for pad in i.part.pins:
                if pad not in i.conns:
                    problems.append(f"{i.ref} pad {pad} ({i.part.pins[pad]}) unconnected")
        for net, pins in self.nets().items():
            if len(pins) < 2 and net not in allow_single and not net.startswith("NC_"):
                problems.append(f"net {net} has a single connection {pins}")
        return problems

    # ------------------------------------------------------------ SPICE
    def spice(self, refs=None, extra_params=None) -> str:
        lines = []
        for i in self.insts:
            if refs is not None and i.ref not in refs:
                continue
            if not i.part.spice or i.dnp:
                continue
            m = {"ref": i.ref}
            for pad, name in i.part.pins.items():
                m[name] = spice_net(i.conns.get(pad, f"NC_{i.ref}_{pad}"))
                m[pad] = m[name]
            for k in ("vos1", "vos2", "vos3", "vos4"):
                m[k] = "0"
            m["vref0"] = "2.495"
            m.update(i.params)
            if extra_params and i.ref in extra_params:
                m.update(extra_params[i.ref])
            txt = i.part.spice
            # resistor/capacitor value override
            if i.part.symbol in ("Device:R", "Device:C") and i.value:
                txt = re.sub(r"\S+$", spice_value(i.value), txt.split("\n")[0])
            lines.append(re.sub(r"\{([^{}]+)\}", lambda mm: str(m[mm.group(1)]), txt))
        return "\n".join(lines)


def spice_net(n: str) -> str:
    if n == "GND":
        return "0"
    return re.sub(r"[^A-Za-z0-9_]", lambda m: {"+": "P", "-": "N", ".": "_"}.get(m.group(0), "_"), n)


def spice_value(v: str) -> str:
    v = v.split()[0].replace("µ", "u").replace("Ω", "")
    if re.fullmatch(r"[\d.]+R[\d]*", v):          # 0R75 / 11R style
        a, _, b = v.partition("R")
        return f"{a}.{b}" if b else a
    if v.endswith("M"):
        return v[:-1] + "MEG"
    return v
