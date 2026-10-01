// Persistent settings (NVS on the ESP32). Plain-old-data so it can be stored
// as a single blob with a CRC.
#pragma once
#include <stdint.h>

#include "lfp8_config.h"

namespace lfp8 {

constexpr uint32_t kSettingsMagic = 0x4C465038u;  // "LFP8"
constexpr uint16_t kSettingsVersion = 4;

struct Settings {
  uint32_t magic;
  uint16_t version;
  uint8_t boardId;         // 1..32 (5 boards -> 1..5)
  uint8_t iset;            // 0..7 board charge-current DAC
  char wifiSsid[33];
  char wifiPass[65];
  char adminPass[33];      // optional: protects config/OTA (empty = open)
  float calV[kNumCh];      // gain, default 1.000
  float calI[kNumCh];
  float v25;               // board diode voltage at 25 C
  uint8_t v25Valid;        // 0 -> calibrate at first boot
  // ---- §7 policy ----
  float vMaxChg;           // 3.60
  float vAbsMax;           // 3.70
  float iTerm;             // 0.05 A
  uint16_t tTermS;         // 60 s
  float vMinDis;           // 2.50
  float vPrecharge;        // 2.80 (below: 20 % duty precharge)
  float vCcMin;            // 3.05 (below: 50 % duty pulses; §4 thermal policy)
  float vDead;             // 2.00
  float tCellMax;          // 55 C (pause)
  float tCellResume;       // 45 C
  float tCellFault;        // 65 C (latched fault)
  float tCellMinChg;       // 0 C (pause charging below)
  float tBoardFan;         // 40 C
  float tBoardMax;         // 70 C (derate: no new discharges, pause discharges)
  float maxChgH;           // 30 h
  float maxDisH;           // 30 h
  uint16_t restAfterChgMin;// 30
  uint16_t restAfterDisMin;// 30
  uint16_t restBeforeIrS;  // 300 s (cap-test: after storage charge)
  uint16_t irRestS;        // 5 s (>= 5 s, §7.1)
  uint8_t storagePct;      // 50 % of measured capacity (by Ah)
  uint8_t capTestIr;       // 1 = IR at 50 % SoC at the end of a capacity test
  float nominalMah;        // 15000
  float xchkTolV;          // 0.060 V
  float vinMin;            // 4.80 V (rail set to 5.20 V, hardware UV 4.48 V)
  float vinMax;            // 5.40 V (hardware OV 5.50 V)
  uint32_t crc;            // must stay last
};

void settingsDefaults(Settings &s);
// Clamp every field into a safe range. Returns the number of corrected fields.
int settingsSanitize(Settings &s);
uint32_t settingsCrc(const Settings &s);
// Default board id derived from the MAC (1..5).
uint8_t boardIdFromMac(const uint8_t mac[6]);

}  // namespace lfp8
