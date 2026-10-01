#!/usr/bin/env bash
# Runs every check in the repository. Exit code 0 = all passed.
#   ./run_all_tests.sh            full run (Monte Carlo 100 runs, ~5 min on 4 cores)
#   QUICK=1 ./run_all_tests.sh    Monte Carlo 20 runs
# Needs: ngspice, python3 (numpy, pytest, pytest-xdist, matplotlib),
#        KiCad 7 (kicad-cli + its python at /usr/bin/python3), g++/make,
#        optional: arduino-cli with the esp32 core, openscad + trimesh for the holder.
set -u
cd "$(dirname "$0")"
declare -A RESULT
run() {  # name, command...
  local name=$1; shift
  echo "=================== $name"
  if "$@"; then RESULT[$name]=PASS; else RESULT[$name]=FAIL; fi
}
MC=${QUICK:+20}
run "ngspice circuit + adversarial tests" bash -c "cd sim && MC_RUNS=${MC:-100} python3 -m pytest -q -n 4 tests/"
run "schematic == DSL netlist"            bash -c "cd hardware/gen && python3 verify_netlist.py"
run "PCB: DRC, netlist, current, BOM"     bash -c "cd hardware/gen && /usr/bin/python3 check_pcb.py"
run "firmware host tests"                 make -C firmware test
if command -v arduino-cli >/dev/null; then
  run "firmware ESP32-C3 build"           make -C firmware build
fi
if command -v openscad >/dev/null; then
  run "holder printability checks"        bash -c "cd mechanical && python3 check_printability.py"
fi
echo "=================== summary"
rc=0
for k in "${!RESULT[@]}"; do printf '%-40s %s\n' "$k" "${RESULT[$k]}"; [ "${RESULT[$k]}" = PASS ] || rc=1; done
exit $rc
