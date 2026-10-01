#include <math.h>
#include <string.h>

#include "../lfp8/history.h"
#include "../lfp8/leds.h"
#include "../lfp8/settings.h"
#include "test_framework.h"

using namespace lfp8;

TEST(settings_defaults_match_policy) {
  Settings s;
  settingsDefaults(s);
  CHECK_NEAR(s.vMaxChg, 3.60, 1e-6);
  CHECK_NEAR(s.vAbsMax, 3.70, 1e-6);
  CHECK_NEAR(s.iTerm, 0.05, 1e-6);
  CHECK_EQ(s.tTermS, 60);
  CHECK_NEAR(s.vMinDis, 2.50, 1e-6);
  CHECK_NEAR(s.vPrecharge, 2.80, 1e-6);
  CHECK_NEAR(s.vDead, 2.00, 1e-6);
  CHECK_NEAR(s.tCellMax, 55, 1e-6);
  CHECK_NEAR(s.tCellResume, 45, 1e-6);
  CHECK_NEAR(s.tBoardFan, 40, 1e-6);
  CHECK_NEAR(s.tBoardMax, 70, 1e-6);
  CHECK_NEAR(s.maxChgH, 30, 1e-6);
  CHECK_NEAR(s.maxDisH, 30, 1e-6);
  CHECK_EQ(s.iset, 4);
  CHECK_EQ(s.restAfterChgMin, 30);
  CHECK_EQ(s.storagePct, 50);
  for (int k = 0; k < kNumCh; k++) {
    CHECK_NEAR(s.calV[k], 1.0, 0);
    CHECK_NEAR(s.calI[k], 1.0, 0);
  }
  Settings t = s;
  CHECK_EQ(settingsSanitize(t), 0);  // defaults are within all ranges
  CHECK_EQ(s.crc, settingsCrc(s));
}

TEST(settings_sanitize_is_conservative) {
  Settings s;
  settingsDefaults(s);
  s.vMaxChg = 4.2f;      // Li-ion value -> clamp to LFP max
  s.vAbsMax = 3.0f;
  s.vMinDis = 1.0f;
  s.vDead = 2.9f;
  s.iset = 12;
  s.calV[3] = 1.5f;
  s.calI[2] = NAN;
  s.irRestS = 1;         // §7.1 requires >= 5 s
  s.tCellMax = 90;
  s.boardId = 0;
  memset(s.wifiSsid, 'x', sizeof(s.wifiSsid));  // unterminated
  int n = settingsSanitize(s);
  CHECK(n >= 9);
  CHECK_NEAR(s.vMaxChg, 3.65, 1e-6);
  CHECK(s.vAbsMax >= s.vMaxChg + 0.05f - 1e-6f);
  CHECK_NEAR(s.vMinDis, 2.30, 1e-6);
  CHECK(s.vDead <= s.vPrecharge - 0.2f + 1e-6f);
  CHECK_EQ(s.iset, 7);
  CHECK_NEAR(s.calV[3], 1.10, 1e-6);
  CHECK_NEAR(s.calI[2], 0.90, 1e-6);
  CHECK_EQ(s.irRestS, 5);
  CHECK_NEAR(s.tCellMax, 60, 1e-6);
  CHECK(s.tCellFault > s.tCellMax);
  CHECK_EQ(s.boardId, 1);
  CHECK_EQ(s.wifiSsid[32], 0);
  uint32_t c = s.crc;
  s.iset = 3;
  CHECK(settingsCrc(s) != c);
  uint8_t mac[6] = {0, 1, 2, 3, 4, 7};
  CHECK_EQ(boardIdFromMac(mac), 3);
}

TEST(history_ring_buffer) {
  static History h;  // 92 KB - keep off the stack
  h.reset(0);
  uint32_t t = 0;
  // interval 1: two samples on ch1, none on ch2
  h.accumulate(0, 3.300f, 1.000f);
  h.accumulate(0, 3.310f, 0.990f);
  t += kHistIntervalMs;
  CHECK(h.tick(t));
  CHECK_EQ(h.count(), 1);
  HistSample s[4];
  CHECK_EQ(h.copy(0, 0, 4, s), 1);
  CHECK_EQ(s[0].mv, 3305);
  CHECK_EQ(s[0].ma, 995);
  CHECK_EQ(h.copy(1, 0, 1, s), 1);
  CHECK_EQ(s[0].mv, kHistNoData);
  CHECK(!h.tick(t + 1000));  // not yet
  // fill past the capacity: oldest is dropped
  for (int j = 0; j < kHistLen + 10; j++) {
    h.accumulate(0, (float)j / 1000.0f, -0.5f);
    t += kHistIntervalMs;
    h.tick(t);
  }
  CHECK_EQ(h.count(), kHistLen);
  CHECK_EQ(h.copy(0, 0, 1, s), 1);
  CHECK_EQ(s[0].mv, 10);  // first 11 pushes (incl. the initial one) dropped -> j=10 is oldest
  CHECK_EQ(s[0].ma, -500);
  CHECK_EQ(h.copy(0, kHistLen - 1, 4, s), 1);
  CHECK_EQ(s[0].mv, kHistLen + 9);
  CHECK_EQ(h.copy(0, kHistLen, 1, s), 0);
}

TEST(led_blink_codes) {
  CHECK(!ledPattern(ChState::Empty, 0));
  CHECK(ledPattern(ChState::Done, 12345));
  CHECK(ledPattern(ChState::ChargingCc, 100) && !ledPattern(ChState::ChargingCc, 600));
  // FAULT_OV: 2 blinks then a pause
  int on = 0;
  bool prev = false;
  for (uint32_t ms = 0; ms < 2500; ms += 10) {
    bool l = ledPattern(ChState::FaultOv, ms);
    if (l && !prev) on++;
    prev = l;
  }
  CHECK_EQ(on, 2);
  on = 0;
  prev = false;
  for (uint32_t ms = 0; ms < 8 * 500 + 1500; ms += 10) {
    bool l = ledPattern(ChState::FaultSafety, ms);
    if (l && !prev) on++;
    prev = l;
  }
  CHECK_EQ(on, 8);
}
