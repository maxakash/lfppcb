#!/usr/bin/env python3
"""
check_printability.py - sanity checks for the LFP-8 cell holder STLs.

For every STL in mechanical/stl/:
  * mesh is watertight, winding-consistent, a positive volume, one body
  * fits the Ender 3 S1 Pro with margin: X, Y <= 210 mm, Z <= 260 mm, sits on z = 0
  * no unsupported overhang: every downward-facing patch steeper than 45 deg
    must be a bridge whose shorter side is <= 10 mm (reported per part)
  * volume -> filament grams (PETG 1.27 g/cm3, effective fill 45 % of solid)
    and a rough print-time estimate for an Ender 3 S1 Pro
Cradle specific (parameters exported from lfp_holder.scad via echo):
  * cell envelope  D 33.6 x L 141.5 fits every slot with >= 0.3 mm radial
    clearance - checked analytically AND by sampling points of the envelope
    against the real cradle mesh
  * spring pre-load / reserve over the whole cell-length and plate tolerance
  * pogo stroke budget, + pogo lands on the + cap, contact separation
  * stacking: cell top below the cradle above, pin / socket clearance

Exit code 0 = all checks passed, 1 = at least one failure.
Requires: numpy, numpy-stl, trimesh (+ rtree, networkx/scipy for some queries).
"""
import json
import math
import pathlib
import subprocess
import sys
import tempfile

import numpy as np
import trimesh
from stl import mesh as npstl

HERE = pathlib.Path(__file__).resolve().parent
SCAD = HERE / "lfp_holder.scad"
STL_DIR = HERE / "stl"

BED_MAX = (210.0, 210.0, 260.0)   # 220 x 220 x 270 minus margin
PETG_DENSITY = 1.27               # g/cm3
EFFECTIVE_FILL = 0.45             # 30 % infill + 4 walls ~ 45 % of the solid volume
FILAMENT_D = 1.75                 # mm
# rough Ender 3 S1 Pro PETG model: ~4 mm3/s average extrusion (50-60 mm/s,
# 0.2 mm layers, incl. accelerations), +6 s per layer for travel/retracts/
# layer change, +6 min heat-up/levelling per print
FLOW_MM3_S = 4.0
LAYER_H = 0.2
SEC_PER_LAYER = 6.0
SETUP_S = 360.0
OVERHANG_NZ = -0.72               # n_z below this = steeper than ~46 deg from vertical
MAX_BRIDGE = 10.5                 # mm (10 mm rule + tessellation tolerance)
MIN_RADIAL_CLEARANCE = 0.3        # mm

failures = []


def check(cond, msg):
    print(("  [ OK ] " if cond else "  [FAIL] ") + msg)
    if not cond:
        failures.append(msg)
    return cond


# ------------------------------------------------------------------ params
def load_params():
    echo = STL_DIR / "params.echo"
    tmp = None
    if not echo.exists() or echo.stat().st_mtime < SCAD.stat().st_mtime:
        tmp = tempfile.NamedTemporaryFile(suffix=".echo", delete=False)
        tmp.close()
        echo = pathlib.Path(tmp.name)
        subprocess.run(["openscad", "-o", str(echo), "-D", 'part="cradle"', str(SCAD)],
                       check=True, capture_output=True)
    for line in echo.read_text().splitlines():
        if "LFP_PARAMS_JSON=" in line:
            js = line.split("LFP_PARAMS_JSON=", 1)[1].rstrip()
            if js.endswith('"'):
                js = js[:-1]
            try:
                return json.loads(js)
            except json.JSONDecodeError:
                return json.loads(js.replace('\\"', '"'))
    raise RuntimeError("LFP_PARAMS_JSON not found in OpenSCAD echo output")


# --------------------------------------------------------------- mesh checks
def overhang_patches(m):
    """Connected patches of downward faces steeper than 45 deg (not on the bed).
    Returns list of (min_span, max_span, area, z) per patch."""
    n = m.face_normals
    zc = m.triangles_center[:, 2]
    sel = np.where((n[:, 2] < OVERHANG_NZ) & (zc > 0.3))[0]
    if len(sel) == 0:
        return []
    sel_set = np.zeros(len(m.faces), bool)
    sel_set[sel] = True
    adj = m.face_adjacency
    adj = adj[sel_set[adj[:, 0]] & sel_set[adj[:, 1]]]
    groups = trimesh.graph.connected_components(adj, nodes=sel, min_len=1)
    out = []
    for g in groups:
        g = np.asarray(g)
        pts = m.vertices[np.unique(m.faces[g].ravel())][:, :2]
        if len(np.unique(pts.round(4), axis=0)) < 3:
            span = np.ptp(pts, axis=0)
            mn, mx = float(min(span)), float(max(span))
        else:
            _, ext = trimesh.bounds.oriented_bounds_2D(pts)
            mn, mx = float(min(ext)), float(max(ext))
        out.append((mn, mx, float(m.area_faces[g].sum()), float(zc[g].mean())))
    return out


def print_time_h(volume_mm3, height_mm):
    ext = volume_mm3 * EFFECTIVE_FILL
    layers = height_mm / LAYER_H
    return (ext / FLOW_MM3_S + layers * SEC_PER_LAYER + SETUP_S) / 3600.0


def check_part(path):
    print(f"\n== {path.name}")
    m = trimesh.load(path, force="mesh")
    m_np = npstl.Mesh.from_file(str(path))
    vol_np, _, _ = m_np.get_mass_properties()
    lo, hi = m.bounds
    ext = hi - lo
    print(f"  bbox  X {ext[0]:.2f}  Y {ext[1]:.2f}  Z {ext[2]:.2f} mm   "
          f"(min {lo.round(2).tolist()})  faces {len(m.faces)}")
    check(m.is_watertight, "watertight (every edge shared by exactly 2 faces)")
    check(m.is_winding_consistent, "consistent winding / manifold")
    check(m.is_volume and m.volume > 0, "valid positive volume")
    bodies = len(m.split(only_watertight=False))
    check(bodies == 1, f"single body ({bodies})")
    check(abs(vol_np - m.volume) < 0.001 * m.volume + 1, "numpy-stl volume agrees with trimesh")
    fits = (ext[0] <= BED_MAX[0] and ext[1] <= BED_MAX[1]) or \
           (ext[1] <= BED_MAX[0] and ext[0] <= BED_MAX[1])
    check(fits and ext[2] <= BED_MAX[2],
          f"fits {BED_MAX[0]:.0f} x {BED_MAX[1]:.0f} x {BED_MAX[2]:.0f} mm")
    check(abs(lo[2]) < 1e-6, "sits on the bed (z min = 0)")
    bed_area = m.area_faces[(m.face_normals[:, 2] < -0.999) &
                            (m.triangles_center[:, 2] < 1e-3)].sum()
    print(f"  first-layer contact area {bed_area/100:.1f} cm2")
    patches = overhang_patches(m)
    worst = max(patches, key=lambda p: p[0]) if patches else (0, 0, 0, 0)
    tot = sum(p[2] for p in patches)
    check(worst[0] <= MAX_BRIDGE,
          f"overhangs > 45 deg are bridges <= 10 mm: {len(patches)} patches, "
          f"{tot:.0f} mm2, widest span {worst[0]:.1f} mm (z {worst[3]:.1f})")
    vol_cm3 = m.volume / 1000.0
    grams = vol_cm3 * EFFECTIVE_FILL * PETG_DENSITY
    metres = m.volume * EFFECTIVE_FILL / (math.pi * (FILAMENT_D / 2) ** 2) / 1000.0
    hours = print_time_h(m.volume, ext[2])
    print(f"  solid volume {vol_cm3:.1f} cm3 -> {grams:.0f} g PETG "
          f"({metres:.1f} m of 1.75 mm), est. print time {hours:.1f} h")
    return m, dict(name=path.stem, bbox=ext.round(2).tolist(), volume_cm3=round(vol_cm3, 1),
                   grams=round(grams, 1), metres=round(metres, 1), hours=round(hours, 1))


# ---------------------------------------------------------- cradle physics
def circle_rect_overlap(r, x0, x1, y0, y1, n=600):
    """area of circle(r, centre 0,0) intersected with rectangle (numerical)."""
    xs = np.linspace(x0, x1, n)
    dx = (x1 - x0) / (n - 1)
    h = np.sqrt(np.clip(r * r - xs ** 2, 0, None))
    lo = np.clip(-h, y0, y1)
    hi = np.clip(h, y0, y1)
    return float(np.sum(np.clip(hi - lo, 0, None)) * dx)


def check_cradle_analytic(p):
    print("\n== cradle: analytic checks from lfp_holder.scad parameters")
    rc = p["cell_d_max"] / 2
    clr = p["r_saddle"] - rc
    check(clr >= MIN_RADIAL_CLEARANCE,
          f"saddle radius {p['r_saddle']:.2f} -> radial clearance {clr:.2f} mm for D{p['cell_d_max']} "
          f"({p['r_saddle'] - p['cell_d_nom']/2:.2f} mm for D{p['cell_d_nom']})")
    # axial: longest cell (pressed on the + plate) must not touch the - wall
    gap_neg = p["W_neg"] - (p["P"] + p["cell_len_max"])
    check(gap_neg >= MIN_RADIAL_CLEARANCE,
          f"longest cell end {gap_neg:.2f} mm in front of the - wall face (spring side)")
    # + wall face must stay behind the cell shoulder (cap raised >= cap_h_min)
    shoulder = p["cap_h_min"] - (p["W_pos"] - p["P"]) - p["fp_t_tol"]
    check(shoulder >= 0.15,
          f"+ wall face {p['W_pos'] - p['P']:.2f} mm proud of the plate, cap >= {p['cap_h_min']} mm "
          f"-> shoulder clearance {shoulder:.2f} mm (thinnest plate)")
    check(p["cap_clear_d"] / 2 >= p["cap_d_max"] / 2 + abs(p["z_cell"] - (p["z_saddle_bottom"] + rc)) + 0.25,
          f"cap recess D{p['cap_clear_d']} clears a D{p['cap_d_max']} cap incl. axis tolerance")

    # spring: L_contact between + plate face and - spring base, plates +-tol
    t = p["fp_t_tol"]
    L_lo = p["L_contact"] - 2 * t        # both plates thick  -> shortest gap
    L_hi = p["L_contact"] + 2 * t        # both plates thin   -> longest gap
    h_long = L_lo - p["cell_len_max"]    # spring height with the longest cell
    h_short = L_hi - p["cell_len_min"]   # spring height with the shortest cell
    pre_short = p["spring_free_min"] - h_short
    check(h_long >= max(2.0, p["spring_solid"]),
          f"141.5 mm cell: spring compressed to {h_long:.2f} mm "
          f"(>= 2 mm and >= solid {p['spring_solid']} mm, reserve {h_long - p['spring_solid']:.2f} mm); "
          f"deflection {p['spring_free_min'] - h_long:.1f}..{p['spring_free_max'] - h_long:.1f} mm")
    check(pre_short >= p["spring_preload_min"] - 1e-9,
          f"139.5 mm cell: spring pre-load {pre_short:.2f} mm with the shortest "
          f"({p['spring_free_min']} mm) spring and thinnest plates (>= {p['spring_preload_min']} mm)")
    ins = (p["L_contact"] - p["spring_solid"]) - p["cell_len_nom"]
    print(f"  info: insertion room for a {p['cell_len_nom']} mm cell (spring solid): {ins:.2f} mm "
          f"vs + pogo protrusion {p['pos_pogo_preload']} mm")

    if p["sense_style"] == "pogo":
        # + pogo: fixed datum, compression = pos_pogo_preload
        check(p["pos_pogo_preload"] <= p["pogo_work"],
              f"+ pogo compression {p['pos_pogo_preload']} mm <= working stroke {p['pogo_work']} mm")
        stop_p = p["x_tip_p"] - p["x_head_stop_p"]
        check(stop_p <= p["pogo_full"],
              f"+ pogo head hard-stop at {stop_p:.1f} mm compression (< full stroke {p['pogo_full']})")
        # - pogo: compression range over the cell length tolerance
        c_min = p["P"] + p["cell_len_min"] - p["x_tip_n"]
        c_max = p["P"] + p["cell_len_max"] - p["x_tip_n"]
        check(c_min > 0 and c_max <= p["pogo_work"],
              f"- pogo compression {c_min:.2f}..{c_max:.2f} mm for {p['cell_len_min']}..{p['cell_len_max']} mm "
              f"cells (working stroke {p['pogo_work']} mm)")
        print(f"  info: with a 2.0 mm-stroke pin set the - pogo per batch with the gauge "
              f"(tip {p['gauge_neg']:.1f} mm in front of the - wall for L={p['cell_len_min']})")
        # + pogo must land on the + cap (smallest cap, largest axis offset)
        dz_axis = (p["z_saddle_bottom"] + p["cell_d_max"] / 2) - p["z_cell"]
        r_land = abs(p["sense_dz"]) + abs(dz_axis)
        check(r_land + 0.3 <= p["cap_d_min"] / 2,
              f"+ pogo lands {r_land:.2f} mm off-axis, inside a D{p['cap_d_min']} cap (0.3 mm margin), "
              f"clear of M5 thread (r 2.5)")
        # separation pogo head vs force plate edge
        plate_bottom = p["force_dz"] - p["fp_h"] / 2
        sep = (plate_bottom - p["sense_dz"]) - p["pogo_head_d"] / 2
        check(sep >= 1.0, f"force plate edge to pogo head clearance {sep:.2f} mm (>= 1.0)")
        cap_r = p["cap_d_min"] / 2
        ov = circle_rect_overlap(cap_r, p["force_dy"] - p["fp_w"] / 2, p["force_dy"] + p["fp_w"] / 2,
                                 p["force_dz"] - p["fp_h"] / 2, p["force_dz"] + p["fp_h"] / 2)
        check(ov >= 40, f"force plate covers {ov:.0f} mm2 of a D{p['cap_d_min']} + cap")
        tail_room = p["x_tail_p"] - p["race_lip_t"]
        check(tail_room >= 2.0, f"+ pogo tail ends {tail_room:.1f} mm before the raceway lip")
        print(f"  info: - pogo tail sticks out {p['x_tail_n'] - p['X_len']:.1f} mm beyond the cradle "
              f"(solder joint, outside the printed part)")
    else:
        cap_r = p["cap_d_min"] / 2
        ov_f = circle_rect_overlap(cap_r, p["force_dy"] - p["fp_w"] / 2, p["force_dy"] + p["fp_w"] / 2,
                                   p["force_dz"] - p["fp_h"] / 2, p["force_dz"] + p["fp_h"] / 2)
        ov_s = circle_rect_overlap(cap_r, p["sense_dy"] - p["sp_w"] / 2, p["sense_dy"] + p["sp_w"] / 2,
                                   p["sense_dz"] - p["sp_h"] / 2, p["sense_dz"] + p["sp_h"] / 2)
        check(ov_f >= 40 and ov_s >= 10,
              f"plate style: force plate {ov_f:.0f} mm2, sense plate {ov_s:.0f} mm2 on a D{p['cap_d_min']} cap")

    # stacking
    cell_top = p["z_saddle_bottom"] + p["cell_d_max"]
    check(p["H"] - cell_top >= 2.0,
          f"stack: max cell top {cell_top:.1f} mm, next cradle floor at {p['H']:.1f} mm "
          f"(gap {p['H'] - cell_top:.1f} mm)")
    check(abs(p["stack_clr"] - 0.2) < 1e-9, f"stacking pin D{p['pin_d']} in socket D{p['pin_d'] + 2*p['stack_clr']:.1f} "
          f"(0.2 mm radial clearance)")


def envelope_points(p, i, radial=MIN_RADIAL_CLEARANCE):
    """Points on the cell envelope surfaces that must lie OUTSIDE the cradle."""
    c = p["cell_y"][i]
    P = p["P"]
    pts = []
    th = np.linspace(0, 2 * np.pi, 48, endpoint=False)
    # (a) concentric: D_max + 2 x 0.3 mm around the saddle axis, body only
    R = p["cell_d_max"] / 2 + radial
    for x in np.linspace(P + 1.0, P + p["cell_len_max"] - 1.0, 71):
        pts.append(np.c_[np.full_like(th, x), c + R * np.cos(th), p["z_saddle_axis"] + R * np.sin(th)])
    # (b) resting cells (min and max diameter, lying in the saddle, pressed on the + plate)
    for d in (p["cell_d_nom"], p["cell_d_max"]):
        r = d / 2 - 0.05
        zc = p["z_saddle_bottom"] + d / 2
        for x in np.linspace(P + p["cap_h_min"], P + p["cell_len_max"], 57):
            pts.append(np.c_[np.full_like(th, x), c + r * np.cos(th), zc + r * np.sin(th)])
        rr = np.linspace(0, 1, 9)
        for rad in rr * (p["cap_d_max"] / 2):                      # + cap face
            pts.append(np.c_[np.full_like(th, P + 0.02), c + rad * np.cos(th), zc + rad * np.sin(th)])
        for rad in p["cap_d_max"] / 2 + rr * (r - p["cap_d_max"] / 2):   # shoulder ring
            pts.append(np.c_[np.full_like(th, P + p["cap_h_min"] - p["fp_t_tol"]),
                             c + rad * np.cos(th), zc + rad * np.sin(th)])
        for rad in rr * r:                                          # - end (+0.3 mm)
            pts.append(np.c_[np.full_like(th, P + p["cell_len_max"] + MIN_RADIAL_CLEARANCE),
                             c + rad * np.cos(th), zc + rad * np.sin(th)])
    return np.vstack(pts)


def check_cradle_mesh(m, p, name):
    pts = np.vstack([envelope_points(p, i) for i in range(p["n_cells"])])
    # only points near the part matter; test in chunks to keep memory low
    inside = np.zeros(len(pts), bool)
    for k in range(0, len(pts), 2000):
        inside[k:k + 2000] = m.contains(pts[k:k + 2000])
    n_in = int(inside.sum())
    check(n_in == 0,
          f"{name}: cell envelope D{p['cell_d_max']} x {p['cell_len_max']} (+0.3 mm radial) "
          f"fits all {p['n_cells']} slots ({len(pts)} sample points, {n_in} inside the part)")
    if n_in:
        bad = pts[inside]
        print("    first offending points:", bad[:5].round(2).tolist())
    # negative control: an envelope 0.05 mm larger than the saddle MUST be detected,
    # otherwise the point-in-mesh test is not working and the result above is void
    ctrl = envelope_points(p, 0, radial=p["r_saddle"] - p["cell_d_max"] / 2 + 0.05)
    n_ctrl = int(sum(m.contains(ctrl[k:k + 2000]).sum() for k in range(0, len(ctrl), 2000)))
    check(n_ctrl > 0, f"{name}: negative control (envelope 0.05 mm larger than the saddle) "
                      f"is detected ({n_ctrl} points inside)")


# --------------------------------------------------------------------- main
def main():
    p = load_params()
    print(f"LFP-8 holder check  (sense_style={p['sense_style']}, bed limit "
          f"{BED_MAX[0]:.0f} x {BED_MAX[1]:.0f} x {BED_MAX[2]:.0f} mm)")
    stls = sorted(STL_DIR.glob("*.stl"))
    required = ["cradle_4cell_labels_1-4", "cradle_4cell_labels_5-8", "board_stand"]
    for r in required:
        check((STL_DIR / f"{r}.stl").exists(), f"stl/{r}.stl present")
    results = {}
    meshes = {}
    for s in stls:
        m, info = check_part(s)
        results[s.stem] = info
        meshes[s.stem] = m

    check_cradle_analytic(p)
    print("\n== cradle: envelope vs. mesh")
    for k, m in meshes.items():
        if k.startswith("cradle"):
            check_cradle_mesh(m, p, k)

    # ------------------------------------------------------------ the set
    print("\n== full set for 40 cells (5 x LFP-8)")
    qty = {"cradle_4cell_labels_1-4": 5, "cradle_4cell_labels_5-8": 5, "board_stand": 5}
    opt = {"pogo_gauge": 1, "board_tray": 0}
    tg = th = tm = 0.0
    for k, n in list(qty.items()) + list(opt.items()):
        if k not in results or n == 0:
            continue
        r = results[k]
        print(f"  {n:2d} x {k:26s} {r['grams']:6.0f} g  {r['hours']:5.1f} h   "
              f"-> {n*r['grams']:6.0f} g  {n*r['hours']:6.1f} h")
        tg += n * r["grams"]; th += n * r["hours"]; tm += n * r["metres"]
    print(f"  TOTAL  {tg/1000:.2f} kg PETG ({tm:.0f} m, ~{math.ceil(tg/1000)} x 1 kg spools), "
          f"~{th:.0f} h printing")
    if "board_tray" in results:
        print(f"  (optional board_tray instead of a stand: {results['board_tray']['grams']:.0f} g, "
              f"{results['board_tray']['hours']:.1f} h each)")

    print()
    if failures:
        print(f"FAILED: {len(failures)} check(s)")
        for f in failures:
            print("   -", f)
        return 1
    print("ALL CHECKS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
