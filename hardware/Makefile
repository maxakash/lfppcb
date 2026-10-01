# Regenerate the KiCad project and the production files from the netlist DSL.
#   make            schematic + placed board + routed board + checks + fab files
#   make sch        schematic only (+ netlist verification)
#   make route      re-run Freerouting on the current placement (slow, ~1 h)
KPY ?= /usr/bin/python3        # python that can import pcbnew (KiCad 7)
GEN = gen

all: sch place route check fab

lib:
	cd $(GEN) && python3 make_lib.py
sch: lib
	cd $(GEN) && python3 make_sch.py && python3 verify_netlist.py
place: sch
	cd $(GEN) && $(KPY) make_pcb.py
route:
	cd $(GEN) && $(KPY) route_pcb.py --passes 40
check:
	cd $(GEN) && $(KPY) check_pcb.py
fab:
	cd $(GEN) && $(KPY) make_fab.py
pdf:
	kicad-cli sch export pdf -o fab/lfp8_schematic.pdf kicad/lfp8.kicad_sch

.PHONY: all lib sch place route check fab pdf
