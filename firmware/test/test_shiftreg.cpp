#include "../lfp8/shiftreg.h"
#include "test_framework.h"

using namespace lfp8;

TEST(shiftreg_channel_bit_positions) {
  for (int k = 1; k <= kNumCh; k++) {
    SrOutputs o;
    srClear(o);
    o.chg[k - 1] = true;
    CHECK_EQ(srBuild(o, nullptr), 1u << (3 * (k - 1)));  // CHG_k at 3(k-1)
    srClear(o);
    o.dis[k - 1] = true;
    CHECK_EQ(srBuild(o, nullptr), 1u << (3 * (k - 1) + 1));  // DIS_k at 3(k-1)+1
    srClear(o);
    o.led[k - 1] = true;
    CHECK_EQ(srBuild(o, nullptr), 1u << (3 * (k - 1) + 2));  // LED_k at 3(k-1)+2
  }
  CHECK_EQ(srBitChg(8), 1u << 21);
  CHECK_EQ(srBitDis(8), 1u << 22);
  CHECK_EQ(srBitLed(8), 1u << 23);
}

TEST(shiftreg_mux_and_iset_bits) {
  for (int k = 1; k <= kNumCh; k++) {
    SrOutputs o;
    srClear(o);
    o.muxCh = (uint8_t)k;  // n = S2*4+S1*2+S0 selects channel n+1
    uint32_t w = srBuild(o, nullptr);
    CHECK_EQ(w, (uint32_t)(k - 1) << 24);
    CHECK_EQ((w >> 24) & 1u, (uint32_t)((k - 1) & 1));        // S0 = bit 24
    CHECK_EQ((w >> 25) & 1u, (uint32_t)(((k - 1) >> 1) & 1));  // S1 = bit 25
    CHECK_EQ((w >> 26) & 1u, (uint32_t)(((k - 1) >> 2) & 1));  // S2 = bit 26
  }
  for (int is = 0; is <= 7; is++) {
    SrOutputs o;
    srClear(o);
    o.iset = (uint8_t)is;
    uint32_t w = srBuild(o, nullptr);
    CHECK_EQ(w, (uint32_t)is << 27);
    CHECK_EQ((w >> 27) & 1u, (uint32_t)(is & 1));         // B0 = bit 27
    CHECK_EQ((w >> 29) & 1u, (uint32_t)((is >> 2) & 1));  // B2 = bit 29
  }
  SrOutputs o;
  srClear(o);
  o.iset = 9;  // clamped to 7
  CHECK_EQ(srBuild(o, nullptr), 7u << 27);
  srClear(o);
  o.aux = true;
  CHECK_EQ(srBuild(o, nullptr), 1u << 30);
}

TEST(shiftreg_bit31_never_set_and_roundtrip) {
  SrOutputs o;
  srClear(o);
  for (int k = 0; k < kNumCh; k++) o.led[k] = true;
  for (int k = 0; k < kNumCh; k += 2) o.chg[k] = true;
  for (int k = 1; k < kNumCh; k += 2) o.dis[k] = true;
  o.muxCh = 8;
  o.iset = 7;
  o.aux = true;
  uint32_t w = srBuild(o, nullptr);
  CHECK_EQ(w & 0x80000000u, 0);
  SrOutputs d;
  srDecode(w, d);
  for (int k = 0; k < kNumCh; k++) {
    CHECK(d.chg[k] == o.chg[k]);
    CHECK(d.dis[k] == o.dis[k]);
    CHECK(d.led[k] == o.led[k]);
  }
  CHECK_EQ(d.muxCh, 8);
  CHECK_EQ(d.iset, 7);
  CHECK(d.aux);
  // every single bit i decodes to exactly the named output at position i
  for (int i = 0; i < 31; i++) {
    srDecode(1u << i, d);
    CHECK_EQ(srBuild(d, nullptr), 1u << i);
  }
  CHECK_EQ(srPowerBits(), 0x006DB6DBu);
}

TEST(shiftreg_interlock_chg_and_dis_never_together) {
  for (int k = 1; k <= kNumCh; k++) {
    SrOutputs o;
    srClear(o);
    o.chg[k - 1] = true;
    o.dis[k - 1] = true;
    o.led[k - 1] = true;
    uint8_t il = 0;
    uint32_t w = srBuild(o, &il);
    CHECK_EQ(il, 1u << (k - 1));
    CHECK_EQ(w & (srBitChg(k) | srBitDis(k)), 0);  // both forced off
    CHECK(w & srBitLed(k));
  }
  // other channels are unaffected
  SrOutputs o;
  srClear(o);
  o.chg[0] = o.dis[0] = true;
  o.chg[1] = true;
  o.dis[2] = true;
  uint8_t il = 0;
  uint32_t w = srBuild(o, &il);
  CHECK_EQ(il, 1);
  CHECK_EQ(w, srBitChg(2) | srBitDis(3));
}
