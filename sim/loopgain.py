"""Small-signal loop gain of the CC loop (Middlebrook voltage injection at the
error-amplifier output).  Usage: python3 loopgain.py <soc> [ref=value ...]"""
import sys, re
import numpy as np
from benches import *

def loop_gain(soc=0.5, overrides=None, loop="cc", vin=5.0, iset=4, nocell=False, name=None, r0="8m"):
    vo = dict(overrides or {})
    body = channel_bench(1, chg="DC 5", iset=iset, vin=vin, cell=None if nocell else dict(soc0=soc, r0=r0), value_override=vo,
                         safe="source")
    # DC sources for an AC operating point
    body = re.sub(r"^(V(?:5V|3V3|SAFE|B\d) \S+ 0) PWL\(.*\)$",
                  lambda m: m.group(1) + " DC " + {"V5V": str(vin), "V3V3": "3.3", "VSAFE": "4.6"}.get(m.group(1).split()[0], "X"),
                  body, flags=re.M)
    for i in range(3):
        bit = (iset >> i) & 1
        body = body.replace(f"VB{i} ISETB{i} 0 DC X", f"VB{i} ISETB{i} 0 DC {5*bit}")
    # break only the OUTER loop: op-amp output -> min-select diode -> NB.
    # D102 (CC) / D103 (CV) cathode is moved to inj_b; Vinj drives inj_b from the op-amp output.
    out = "CCO_1" if loop == "cc" else "CVO_1"
    diode = "DD102" if loop == "cc" else "DD103"
    lines = []
    for l in body.splitlines():
        if l.startswith(diode + " "):
            toks = l.split()
            toks[2] = "inj_b"          # D <anode> <cathode> model
            l = " ".join(toks)
        lines.append(l)
    body = "\n".join(lines) + f"\nVinj inj_b {out} DC 0 AC 1\n"
    # the OV latch is bistable: start the DC solution in the healthy (released) state
    body += ".nodeset V(OVO_1)=3.75 V(GEN_1)=3.8\n"
    ana = (f"op\nac dec 40 1 1e6\nlet tl = -v({out.lower()})/v(inj_b)\n"
           "let tmag = db(tl)\nlet tph = 180/pi*cph(tl)")
    w = run(body, ana, ["tmag", "tph"], name=name or f"loopgain_{loop}")
    f, mag, ph = w["x"], w["tmag"], w["tph"]
    # crossover and phase margin
    idx = np.where((mag[:-1] > 0) & (mag[1:] <= 0))[0]
    if len(idx) == 0:
        return dict(f=f, mag=mag, ph=ph, fc=None, pm=None, gm=None)
    i = idx[0]
    fc = float(np.interp(0, [mag[i + 1], mag[i]], [f[i + 1], f[i]]))
    phc = float(np.interp(fc, f, ph))
    pm = 180 + phc
    while pm > 180: pm -= 360
    while pm < -180: pm += 360
    # gain margin: where phase crosses -180 (unwrapped)
    phu = np.unwrap(np.radians(ph)) * 180 / np.pi
    gmi = np.where((phu[:-1] > -180) & (phu[1:] <= -180))[0]
    gmar = float(-mag[gmi[0]]) if len(gmi) else None
    return dict(f=f, mag=mag, ph=ph, fc=fc, pm=pm, gm=gmar)

if __name__ == "__main__":
    soc = float(sys.argv[1]) if len(sys.argv) > 1 else 0.5
    loop = sys.argv[2] if len(sys.argv) > 2 else "cc"
    ov = dict(a.split("=") for a in sys.argv[3:])
    r = loop_gain(soc, ov, loop=loop)
    print(f"{loop} soc={soc}: fc={r['fc']} Hz  PM={r['pm']} deg  GM={r['gm']} dB")
    for fr in [10, 100, 300, 1e3, 3e3, 1e4, 3e4, 1e5]:
        print(f"  f={fr:8.0f}  |T|={np.interp(fr, r['f'], r['mag']):7.2f} dB  ph={np.interp(fr, r['f'], r['ph']):7.1f}")
