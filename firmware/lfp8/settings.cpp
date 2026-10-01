#include "settings.h"

#include <stddef.h>
#include <string.h>

namespace lfp8 {

void settingsDefaults(Settings &s) {
  memset(&s, 0, sizeof(s));
  s.magic = kSettingsMagic;
  s.version = kSettingsVersion;
  s.boardId = 1;
  s.iset = kIsetDefault;
  for (int i = 0; i < kNumCh; i++) {
    s.calV[i] = 1.0f;
    s.calI[i] = 1.0f;
  }
  s.v25 = kTbrdV25Default;
  s.v25Valid = 0;
  s.vMaxChg = 3.60f;
  s.vAbsMax = 3.70f;
  s.iTerm = 0.05f;
  s.tTermS = 60;
  s.vMinDis = 2.50f;
  s.vPrecharge = 2.80f;
  s.vCcMin = 3.05f;
  s.vDead = 2.00f;
  s.tCellMax = 55.0f;
  s.tCellResume = 45.0f;
  s.tCellFault = 65.0f;
  s.tCellMinChg = 0.0f;
  s.tBoardFan = 40.0f;
  s.tBoardMax = 70.0f;
  s.maxChgH = 30.0f;
  s.maxDisH = 30.0f;
  s.restAfterChgMin = 30;
  s.restAfterDisMin = 30;
  s.restBeforeIrS = 300;
  s.irRestS = 5;
  s.storagePct = 50;
  s.capTestIr = 1;
  s.nominalMah = 15000.0f;
  s.xchkTolV = 0.060f;
  s.vinMin = 4.80f;
  s.vinMax = 5.40f;
  s.crc = settingsCrc(s);
}

namespace {
template <typename T>
int clampField(T &v, T lo, T hi) {
  if (!(v >= lo)) {  // also catches NaN for floats
    v = lo;
    return 1;
  }
  if (v > hi) {
    v = hi;
    return 1;
  }
  return 0;
}
int terminate(char *s, size_t n) {
  if (memchr(s, 0, n) == nullptr) {
    s[n - 1] = 0;
    return 1;
  }
  return 0;
}
}  // namespace

int settingsSanitize(Settings &s) {
  int n = 0;
  s.magic = kSettingsMagic;
  s.version = kSettingsVersion;
  n += clampField<uint8_t>(s.boardId, 1, 32);
  n += clampField<uint8_t>(s.iset, 0, 7);
  n += terminate(s.wifiSsid, sizeof(s.wifiSsid));
  n += terminate(s.wifiPass, sizeof(s.wifiPass));
  n += terminate(s.adminPass, sizeof(s.adminPass));
  for (int i = 0; i < kNumCh; i++) {
    n += clampField(s.calV[i], 0.90f, 1.10f);
    n += clampField(s.calI[i], 0.90f, 1.10f);
  }
  n += clampField(s.v25, 0.45f, 0.80f);
  n += clampField<uint8_t>(s.v25Valid, 0, 1);
  // Voltage limits: never allow settings beyond what LiFePO4 tolerates.
  n += clampField(s.vMaxChg, 3.40f, 3.65f);
  n += clampField(s.vAbsMax, 3.60f, 3.80f);
  if (s.vAbsMax < s.vMaxChg + 0.05f) {
    s.vAbsMax = s.vMaxChg + 0.05f;
    n++;
  }
  n += clampField(s.iTerm, 0.01f, 0.50f);
  n += clampField<uint16_t>(s.tTermS, 10, 600);
  n += clampField(s.vMinDis, 2.30f, 3.00f);
  n += clampField(s.vPrecharge, 2.50f, 3.00f);
  n += clampField(s.vCcMin, 3.05f, 3.20f);  // §4: no continuous CC below 3.05 V (SOT-23 pass FET)
  n += clampField(s.vDead, 1.50f, 2.50f);
  if (s.vDead > s.vPrecharge - 0.2f) {
    s.vDead = s.vPrecharge - 0.2f;
    n++;
  }
  n += clampField(s.tCellMax, 35.0f, 60.0f);
  n += clampField(s.tCellResume, 20.0f, s.tCellMax - 5.0f);
  n += clampField(s.tCellFault, s.tCellMax + 3.0f, 70.0f);
  n += clampField(s.tCellMinChg, -5.0f, 10.0f);
  n += clampField(s.tBoardFan, 25.0f, 60.0f);
  n += clampField(s.tBoardMax, 50.0f, 75.0f);
  n += clampField(s.maxChgH, 1.0f, 48.0f);
  n += clampField(s.maxDisH, 1.0f, 48.0f);
  n += clampField<uint16_t>(s.restAfterChgMin, 0, 600);
  n += clampField<uint16_t>(s.restAfterDisMin, 0, 600);
  n += clampField<uint16_t>(s.restBeforeIrS, 5, 7200);
  n += clampField<uint16_t>(s.irRestS, 5, 600);
  n += clampField<uint8_t>(s.storagePct, 0, 100);
  n += clampField<uint8_t>(s.capTestIr, 0, 1);
  n += clampField(s.nominalMah, 500.0f, 100000.0f);
  n += clampField(s.xchkTolV, 0.030f, 0.200f);
  n += clampField(s.vinMin, 4.55f, 4.95f);
  n += clampField(s.vinMax, 5.25f, 5.50f);
  s.crc = settingsCrc(s);
  return n;
}

uint32_t settingsCrc(const Settings &s) {
  // CRC-32 (IEEE) over everything but the trailing crc field.
  const uint8_t *p = reinterpret_cast<const uint8_t *>(&s);
  size_t len = offsetof(Settings, crc);
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    c ^= p[i];
    for (int b = 0; b < 8; b++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

uint8_t boardIdFromMac(const uint8_t mac[6]) { return (uint8_t)(mac[5] % 5u + 1u); }

}  // namespace lfp8
