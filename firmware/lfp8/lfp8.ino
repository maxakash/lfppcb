// LFP-8: 8-channel LiFePO4 (33140, 15 Ah) charger / discharger / tester.
// ESP32-C3-WROOM-02, Arduino-ESP32 core 3.x.
//
// Build:  arduino-cli compile --fqbn esp32:esp32:esp32c3 firmware/lfp8
// Contract with the hardware: docs/INTERFACE.md. See firmware/README.md.
//
// Architecture
//   hal.h            hardware abstraction (shift register, ADS1115, ADC, GPIO, time)
//   hal_esp32.*      ESP32-C3 implementation
//   controller.*     scanner, IR engine, outputs + interlock, heartbeat, fan, LEDs
//   channel.*        per-channel state machine and test programs
//   supervisor.*     safety supervisor (SAFE_RB, VIN, temperatures, ADC faults)
//   app_esp32.*      control task (heartbeat source), NVS, glue
//   net_esp32.cpp    WiFi STA/AP, mDNS, UDP discovery, ArduinoOTA
//   web_esp32.cpp    HTTP dashboard + JSON API
// Everything except *_esp32.* and this file is plain C++ and unit-tested on
// the host (make -C firmware test).
#include "app_esp32.h"

void setup() { lfp8::app::setup(); }

void loop() { lfp8::app::loop(); }
