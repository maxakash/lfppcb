#include "../lfp8/adc_conv.h"
#include "../lfp8/lfp8_config.h"
#include "test_framework.h"

using namespace lfp8;

TEST(adc_cell_voltage_conversion) {
  // V_cell = code * 4.096/32768 * CAL_V  (LSB = 125 uV)
  CHECK_NEAR(adsCodeToCellV(26400, 1.0f), 3.3000, 1e-6);
  CHECK_NEAR(adsCodeToCellV(1, 1.0f), 125e-6, 1e-9);
  CHECK_NEAR(adsCodeToCellV(32767, 1.0f), 4.095875, 1e-6);
  CHECK_NEAR(adsCodeToCellV(26400, 1.01f), 3.333, 1e-5);
  // reversed cell reads negative
  CHECK_NEAR(adsCodeToCellV(-8000, 1.0f), -1.000, 1e-6);
  CHECK_NEAR(adsCodeToCellV(-26400, 1.0f), -3.300, 1e-6);
}

TEST(adc_cell_current_conversion_and_sign) {
  // I_cell = code * 0.256/32768 / 0.100 * CAL_I  (LSB = 78.125 uA)
  CHECK_NEAR(adsCodeToCellI(12800, 1.0f), 1.000, 1e-6);    // charging
  CHECK_NEAR(adsCodeToCellI(-12800, 1.0f), -1.000, 1e-6);  // discharging
  CHECK_NEAR(adsCodeToCellI(1, 1.0f), 78.125e-6, 1e-10);
  CHECK_NEAR(adsCodeToCellI(-13440, 1.0f), -1.050, 1e-6);
  CHECK_NEAR(adsCodeToCellI(12800, 0.98f), 0.980, 1e-6);
  CHECK(adsCodeSaturated(32767));
  CHECK(adsCodeSaturated(-32768));
  CHECK(!adsCodeSaturated(32766));
  CHECK_NEAR(adsCodeToVolts(16384, ads::kFs256), 0.128, 1e-7);
}

TEST(ads1115_config_words) {
  // OS=1 | MUX | PGA | MODE=single | DR | COMP_QUE=11
  CHECK_EQ(ads::configWord(ads::kAin0Ain1, ads::kFs4096, ads::kSps128), 0x8383);  // cell voltage
  CHECK_EQ(ads::configWord(ads::kAin2Ain3, ads::kFs256, ads::kSps128), 0xBB83);   // current
  CHECK_EQ(ads::configWord(ads::kAin1Ain3, ads::kFs256, ads::kSps128), 0xAB83);   // diagnostic
  CHECK_EQ(ads::configWord(ads::kAin0Gnd, ads::kFs4096, ads::kSps128), 0xC383);   // cross-check
  CHECK_EQ(ads::configWord(ads::kAin0Ain1, ads::kFs4096, ads::kSps860), 0x83E3);  // IR pulses
  CHECK_EQ(ads::periodUs(ads::kSps128), 7813);
  CHECK_EQ(ads::periodUs(ads::kSps860), 1163);
  CHECK(ads::waitUs(ads::kSps128) > ads::periodUs(ads::kSps128));
  CHECK_NEAR(ads::fullScale(ads::kFs4096), 4.096, 1e-6);
  CHECK_NEAR(ads::fullScale(ads::kFs256), 0.256, 1e-7);
}

TEST(esp32_adc_conversions) {
  CHECK_NEAR(vchkToBplus(1.650f), 3.300, 1e-6);
  CHECK_NEAR(vinFromAdc(1.5985f), 5.000, 1e-4);
  CHECK_NEAR(vinFromAdc(5.0f * 0.3197f), 5.0, 1e-5);
  NtcStatus st;
  CHECK_NEAR(ntcTempC(1.65f, st), 25.0, 0.01);
  CHECK(st == NtcStatus::Ok);
  // 55 C: R = 10k*exp(3950*(1/328.15-1/298.15)) = 2977.8 ohm -> V = 0.7572
  CHECK_NEAR(ntcTempC(3.3f * 2977.8f / 12977.8f, st), 55.0, 0.05);
  CHECK(st == NtcStatus::Ok);
  float t = ntcTempC(3.25f, st);  // > 3.2 V: no NTC fitted
  CHECK(st == NtcStatus::Open);
  CHECK(t != t);
  ntcTempC(0.05f, st);
  CHECK(st == NtcStatus::Short);
  ntcTempC(3.1f, st);  // -> about -24 C: implausible, treated as not fitted
  CHECK(st == NtcStatus::Open);
  CHECK_NEAR(ntcResistance(1.65f), 10000.0, 0.5);
  // board diode (§8): 0.601 V at 25 C, -1.97 mV/K
  CHECK_NEAR(kTbrdV25Default, 0.601, 1e-6);
  CHECK_NEAR(boardTempC(0.601f, 0.601f), 25.0, 1e-4);
  CHECK_NEAR(boardTempC(0.601f - 0.0197f, 0.601f), 35.0, 1e-3);
  CHECK_NEAR(boardTempC(0.601f - 0.0886f, 0.601f), 70.0, 0.05);
  CHECK(boardDiodePlausible(0.601f));
  CHECK(!boardDiodePlausible(0.05f));
  CHECK(!boardDiodePlausible(3.0f));
}

TEST(iset_lut_scaling_and_passive_ceiling) {
  // §4: set-point at 5.20 V, scales with V_IN/5.20
  const float lut[8] = {0, 0.265f, 0.532f, 0.797f, 1.064f, 1.328f, 1.595f, 1.860f};
  for (int k = 0; k < 8; k++) CHECK_NEAR(isetNominalA((uint8_t)k, 5.20f), lut[k], 1e-6);
  CHECK_NEAR(isetNominalA(4, 5.00f), 1.064 * 5.00 / 5.20, 1e-5);
  CHECK_NEAR(isetNominalA(9, 5.20f), 1.860, 1e-6);  // clamped
  // passive ceiling (V_IN - 0.38 - V_cell) / 1.5 ohm: ~1.0 A at 3.3 V, 5.20 V rail
  CHECK_NEAR(chargeCeilingA(5.20f, 3.30f), 1.0133, 1e-3);
  CHECK_NEAR(chargeCeilingA(5.00f, 3.50f), 0.7467, 1e-3);
  CHECK_NEAR(chargeCeilingA(4.50f, 4.20f), 0.0, 0);  // never negative
  // expected current = min(set-point, ceiling): codes > 4 only help at low V
  CHECK_NEAR(chargeExpectedA(4, 5.20f, 3.30f), 1.0133, 1e-3);
  CHECK_NEAR(chargeExpectedA(7, 5.20f, 3.30f), 1.0133, 1e-3);
  CHECK_NEAR(chargeExpectedA(7, 5.20f, 2.50f), 1.5467, 1e-3);
  CHECK_NEAR(chargeExpectedA(2, 5.20f, 3.30f), 0.532, 1e-6);
}
