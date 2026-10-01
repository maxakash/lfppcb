# LFP-8 firmware: host unit tests and ESP32-C3 build.
#
#   make test    build + run host unit tests, validate JSON output with python3,
#                check the embedded web UI, run the 8-cell capacity-test simulation
#   make build   compile the sketch with arduino-cli (ESP32-C3)
#   make clean
#
# Override ARDUINO_CLI / FQBN / PORT on the command line if needed.

CXX       ?= g++
CXXFLAGS  ?= -std=c++17 -O2 -g -Wall -Wextra -Wshadow -Werror
ARDUINO_CLI ?= arduino-cli
FQBN      ?= esp32:esp32:esp32c3:PartitionScheme=min_spiffs
PORT      ?= /dev/ttyACM0

SKETCH    := lfp8
BUILD     := build/host
JSON_DIR  := $(BUILD)/json

# Pure-C++ core: everything in the sketch folder except the ESP32 glue (*_esp32.cpp).
CORE_SRCS := $(filter-out %_esp32.cpp,$(wildcard $(SKETCH)/*.cpp))
CORE_HDRS := $(wildcard $(SKETCH)/*.h)
CORE_OBJS := $(patsubst $(SKETCH)/%.cpp,$(BUILD)/core/%.o,$(CORE_SRCS))

UNIT_SRCS := test/test_main.cpp test/sim_board.cpp $(filter-out test/test_main.cpp,$(wildcard test/test_*.cpp))
UNIT_OBJS := $(patsubst test/%.cpp,$(BUILD)/test/%.o,$(UNIT_SRCS))
SIM_OBJS  := $(BUILD)/test/sim_cell_test.o $(BUILD)/test/sim_board.o

.PHONY: test unit sim json-check ui-check build upload clean

test: unit json-check ui-check sim

unit: $(BUILD)/unit_tests
	@rm -rf $(JSON_DIR) && mkdir -p $(JSON_DIR)
	$(BUILD)/unit_tests --json-out $(JSON_DIR)

json-check: unit
	@python3 -c 'import json,glob,sys; fs=sorted(glob.glob("$(JSON_DIR)/*.json")); \
	[json.load(open(f)) for f in fs]; \
	print("python3 json: %d JSON documents parsed OK" % len(fs)); sys.exit(0 if len(fs)>=8 else 1)'

ui-check:
	python3 test/check_ui.py

sim: $(BUILD)/sim_cell_test
	$(BUILD)/sim_cell_test

$(BUILD)/unit_tests: $(CORE_OBJS) $(UNIT_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILD)/sim_cell_test: $(CORE_OBJS) $(SIM_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILD)/core/%.o: $(SKETCH)/%.cpp $(CORE_HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/test/%.o: test/%.cpp $(CORE_HDRS) $(wildcard test/*.h)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

build:
	$(ARDUINO_CLI) compile --fqbn $(FQBN) --warnings default $(SKETCH)

upload:
	$(ARDUINO_CLI) compile --fqbn $(FQBN) --upload --port $(PORT) $(SKETCH)

clean:
	rm -rf build
