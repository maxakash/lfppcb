#!/usr/bin/env bash
# Rebuild all STLs and preview images from lfp_holder.scad, then run the checks.
# Needs: openscad (2021.01), xvfb-run (for PNGs on a headless box), python3 with
# numpy-stl + trimesh (+ rtree, networkx) for check_printability.py.
# Any OpenSCAD WARNING/ERROR aborts the build.
set -euo pipefail
cd "$(dirname "$0")"
SCAD=lfp_holder.scad
mkdir -p stl images

run() {   # run <output> [openscad args...]
  local out=$1; shift
  echo ">> $out"
  local log
  log=$(openscad -o "$out" "$@" "$SCAD" 2>&1) || { echo "$log"; exit 1; }
  if echo "$log" | grep -E "WARNING|ERROR" >/dev/null; then
    echo "$log" | grep -E "WARNING|ERROR"; exit 1
  fi
}
png() {   # png <output> <camera> [openscad args...]   (preview mode keeps colours)
  local out=$1 cam=$2; shift 2
  echo ">> $out"
  xvfb-run -a openscad -o "$out" --imgsize=1200,900 --camera="$cam" \
    --colorscheme=Tomorrow "$@" "$SCAD" 2>&1 | grep -E "WARNING|ERROR" && exit 1 || true
}

# ---- STLs (print orientation = as modelled, no supports) -------------------
run stl/cradle_4cell_labels_1-4.stl -D 'part="cradle"' -D label_offset=0
run stl/cradle_4cell_labels_5-8.stl -D 'part="cradle"' -D label_offset=4
run stl/board_stand.stl             -D 'part="board_stand"'
run stl/board_tray.stl              -D 'part="board_tray"'
run stl/pogo_gauge.stl              -D 'part="pogo_gauge"'

# ---- parameters for the checker -------------------------------------------
openscad -o stl/params.echo -D 'part="cradle"' "$SCAD" 2>/dev/null

# ---- previews ----------------------------------------------------------------
# 1) cradle, full CGAL render
png images/cradle_4cell.png "-120,-230,250,102,96,12" --render -D 'part="cradle"'
# 2) section through one cell: contacts, pogo pins, spring, NTC (orthographic)
png images/section_contacts.png "102,72,24,90,0,0,420" --projection=o -D 'part="section"'
# 3) 8-cell module (2 stacked cradles) with the LFP-8 board stand
png images/module_assembly.png "-260,-420,330,60,96,70" -D 'part="assembly"'
# 4) board stand
png images/board_stand.png "-360,430,330,0,0,80" --render -D 'part="board_stand"'

# ---- checks -------------------------------------------------------------------
python3 check_printability.py
