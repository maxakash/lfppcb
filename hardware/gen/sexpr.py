"""Minimal S-expression reader/writer for KiCad files."""
import re

_TOKEN = re.compile(r'\s*(?:(\()|(\))|("(?:[^"\\]|\\.)*")|([^\s()"]+))', re.S)


class Sym(str):
    """Unquoted atom."""


def parse(text):
    stack = [[]]
    pos = 0
    n = len(text)
    while pos < n:
        m = _TOKEN.match(text, pos)
        if not m:
            if text[pos:].strip() == "":
                break
            raise ValueError(f"parse error at {pos}: {text[pos:pos + 40]!r}")
        pos = m.end()
        lp, rp, s, a = m.groups()
        if lp:
            stack.append([])
        elif rp:
            node = stack.pop()
            stack[-1].append(node)
        elif s is not None:
            stack[-1].append(s[1:-1].replace('\\"', '"').replace("\\\\", "\\"))
        elif a is not None:
            stack[-1].append(Sym(a))
    assert len(stack) == 1
    return stack[0][0] if len(stack[0]) == 1 else stack[0]


def q(s):
    return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"') + '"'


def dump(node, indent=0, inline_depth=0):
    """Serialise; small lists are written on one line."""
    if isinstance(node, Sym):
        return str(node)
    if isinstance(node, str):
        return q(node)
    if isinstance(node, (int,)):
        return str(node)
    if isinstance(node, float):
        s = f"{node:.6f}".rstrip("0").rstrip(".")
        return s if s not in ("-0", "") else "0"
    parts = [dump(x, indent + 1) for x in node]
    one = "(" + " ".join(parts) + ")"
    if len(one) < 100 and "\n" not in one:
        return one
    pad = "  " * (indent + 1)
    return "(" + parts[0] + "".join("\n" + pad + p for p in parts[1:]) + ")"


def find(node, key):
    """First child list whose head is key."""
    for x in node:
        if isinstance(x, list) and x and x[0] == key:
            return x
    return None


def find_all(node, key):
    return [x for x in node if isinstance(x, list) and x and x[0] == key]


def S(*items):
    """Build a list with the first element as an unquoted symbol."""
    return [Sym(items[0])] + list(items[1:])
