#include "adc_conv.h"

#include <math.h>

#include "lfp8_config.h"

namespace lfp8 {

namespace ads {
uint16_t configWord(Mux m, Pga p, Rate r) {
  return (uint16_t)(kOsBit | ((uint16_t)(m & 7) << 12) | ((uint16_t)(p & 7) << 9) |
                    (1u << 8) /* single-shot */ | ((uint16_t)(r & 7) << 5) | 0x0003u /* comp off */);
}

float fullScale(Pga p) {
  switch (p) {
    case kFs6144: return 6.144f;
    case kFs4096: return 4.096f;
    case kFs2048: return 2.048f;
    case kFs1024: return 1.024f;
    case kFs512: return 0.512f;
    default: return 0.256f;  // 101, 110, 111 are all +-0.256 V
  }
}

uint32_t periodUs(Rate r) {
  static const uint16_t sps[8] = {8, 16, 32, 64, 128, 250, 475, 860};
  return (uint32_t)(1000000u + sps[r & 7] - 1) / sps[r & 7];
}

// Poll shortly after the nominal period; a slow part (oscillator -10 %) just
// reports OS=0 and is polled again.
uint32_t waitUs(Rate r) { return periodUs(r) + periodUs(r) / 50 + 20; }
}  // namespace ads

float adsCodeToVolts(int16_t code, ads::Pga pga) { return (float)code * ads::fullScale(pga) / 32768.0f; }

float adsCodeToCellV(int16_t code, float calV) { return (float)code * (4.096f / 32768.0f) * calV; }

float adsCodeToCellI(int16_t code, float calI) {
  return (float)code * (0.256f / 32768.0f) / kRShunt * calI;
}

bool adsCodeSaturated(int16_t code) { return code >= 32767 || code <= -32768; }

float vchkToBplus(float adcVolts) { return adcVolts / kDivVchk; }

float vinFromAdc(float adcVolts) { return adcVolts / kDivVin; }

float ntcResistance(float v) {
  if (v <= 0.0f || v >= kNtcSupply) return -1.0f;
  return kNtcPullup * v / (kNtcSupply - v);
}

float ntcTempC(float v, NtcStatus &status) {
  if (!(v == v)) {  // NaN
    status = NtcStatus::Invalid;
    return NAN;
  }
  if (v > 3.2f) {
    status = NtcStatus::Open;
    return NAN;
  }
  if (v < 0.10f) {
    status = NtcStatus::Short;
    return NAN;
  }
  float r = ntcResistance(v);
  if (r <= 0.0f) {
    status = NtcStatus::Invalid;
    return NAN;
  }
  const float t25 = 298.15f;
  float invT = 1.0f / t25 + logf(r / kNtcR25) / kNtcBeta;
  float t = 1.0f / invT - 273.15f;
  if (t < -20.0f) {  // below the plausible lab range: treat as "not fitted"
    status = NtcStatus::Open;
    return NAN;
  }
  status = NtcStatus::Ok;
  return t;
}

float boardTempC(float v, float v25) { return 25.0f + (v - v25) / kTbrdSlope; }

bool boardDiodePlausible(float v) { return v > 0.35f && v < 0.85f; }

float isetNominalA(uint8_t iset, float vin) {
  if (iset > 7) iset = 7;
  float scale = (vin > 3.0f && vin < 6.0f) ? vin / kIsetRefRail : 1.0f;
  return kIsetLutA[iset] * scale;
}

float chargeCeilingA(float vin, float vcell) {
  if (!(vin > 3.0f && vin < 6.0f)) vin = kRailNominal;
  float i = (vin - kChgCeilDrop - vcell) / kChgCeilR;
  return i > 0.0f ? i : 0.0f;
}

float chargeExpectedA(uint8_t iset, float vin, float vcell) {
  float a = isetNominalA(iset, vin), b = chargeCeilingA(vin, vcell);
  return a < b ? a : b;
}

}  // namespace lfp8
