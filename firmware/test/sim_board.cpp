#include "sim_board.h"

#include <math.h>

#include "../lfp8/adc_conv.h"
#include "../lfp8/shiftreg.h"

using namespace lfp8;

// Typical LiFePO4 open-circuit voltage curve (rest, 25 C).
double lfpOcv(double soc) {
  static const double t[][2] = {
      {-0.030, 1.50}, {-0.010, 2.00}, {0.000, 2.50}, {0.005, 2.75}, {0.010, 2.90}, {0.020, 3.00},
      {0.050, 3.12},  {0.100, 3.20},  {0.200, 3.25}, {0.300, 3.275}, {0.400, 3.29}, {0.500, 3.30},
      {0.600, 3.31},  {0.700, 3.325}, {0.800, 3.33}, {0.900, 3.34}, {0.950, 3.36}, {0.980, 3.40},
      {0.995, 3.45},  {1.000, 3.50},  {1.003, 3.60}, {1.010, 3.80}, {1.030, 4.20},
  };
  const int n = sizeof(t) / sizeof(t[0]);
  if (soc <= t[0][0]) return t[0][1];
  if (soc >= t[n - 1][0]) return t[n - 1][1];
  for (int k = 1; k < n; k++) {
    if (soc <= t[k][0]) {
      double f = (soc - t[k - 1][0]) / (t[k][0] - t[k - 1][0]);
      return t[k - 1][1] + f * (t[k][1] - t[k - 1][1]);
    }
  }
  return t[n - 1][1];
}

double SimCell::ocv() const { return lfpOcv(soc); }

SimBoard::SimBoard() {}

// ------------------------------------------------------------------ SAFE ----
bool SimBoard::safeEn() const {
  // §6 watchdog: square wave >= 100 Hz (edges <= 5 ms apart; a 50 Hz wave with
  // 10 ms between edges is rejected), trips 23 ms after the last edge, needs
  // ~20 ms of good edges to recover; SAFE_EN low for 50 ms after power-up.
  bool hbAlive = hbToggles > 1 && nowUs - lastToggleUs < 23000 && emaIntervalUs <= 7500.0 &&
                 nowUs - hbRunningSinceUs >= 20000;
  return nowUs >= 50000 && hbAlive && vin > kRailUvTrip && vin < kRailOvTrip && tBoard < 80.0 && !estop;
}

void SimBoard::setHeartbeat(bool level) {
  if (level != hbLevel) {
    if (hbToggles > 0 && nowUs - lastToggleUs > maxToggleGapUs) maxToggleGapUs = nowUs - lastToggleUs;
    uint64_t iv = hbToggles ? nowUs - lastToggleUs : 1000000;
    // The pump integrates the edge rate: a smoothed interval > 7.5 ms (<= ~66 Hz,
    // e.g. 50 Hz) is rejected; after a trip (>= 23 ms without an edge) it needs
    // ~20 ms of good edges to recover.
    if (iv >= 23000) {
      hbRunningSinceUs = nowUs;
      emaIntervalUs = 1000.0;
    } else {
      emaIntervalUs = 0.8 * emaIntervalUs + 0.2 * (double)iv;
      if (emaIntervalUs > 7500.0) hbRunningSinceUs = nowUs;
    }
    lastIntervalUs = iv;
    hbLevel = level;
    hbToggles++;
    lastToggleUs = nowUs;
  }
}

void SimBoard::shiftWrite(uint32_t w) {
  shiftWrites++;
  for (int k = 1; k <= kNumCh; k++)
    if ((w & srBitChg(k)) && (w & srBitDis(k))) interlockViolations++;
  if (w & 0x80000000u) interlockViolations++;  // bit 31 must be 0
  word = w;
}

bool SimBoard::chgOn(int k) const { return safeEn() && (word & srBitChg(k)) && !(word & srBitDis(k)); }
bool SimBoard::disOn(int k) const { return safeEn() && (word & srBitDis(k)) != 0; }
uint8_t SimBoard::muxCh() const { return safeEn() ? (uint8_t)(((word >> 24) & 7u) + 1) : 1; }

// ----------------------------------------------------------------- physics ---
void SimBoard::advance(uint64_t dtUs) {
  while (dtUs > 0) {
    uint64_t d = dtUs > 1000 ? 1000 : dtUs;
    // Latch an ADS conversion at the middle of its conversion window.
    const uint64_t mid = convStartUs_ + convPeriodUs_ / 2;
    if (convPending_ && !convLatched_) {
      if (mid <= nowUs) {
        latchConversion();
      } else if (mid < nowUs + d) {
        d = mid - nowUs;  // stop exactly at the mid-point, latch on the next pass
      }
    }
    const double dt = (double)d / 1e6;
    const uint8_t iset = (uint8_t)((word >> 27) & 7u);
    const bool safe = safeEn();
    for (int k = 1; k <= kNumCh; k++) {
      SimCell &c = cell[k - 1];
      double I = 0;
      // §6 OV latch releases when SAFE_EN goes low or the cell is removed (< 2.74 V)
      if (c.ovLatched && (!safe || !c.present || c.ocv() < kOvRelease)) c.ovLatched = false;
      bool dis = c.present && !c.reversed && safe && disOn(k);
      if (c.present && !c.reversed && safe && !c.ovLatched) {
        double vOpen = c.ocv() + c.vrc;
        if (chgOn(k) && iset > 0) {
          // §4: set-point scales with V_IN/5.20; passive ceiling (V_IN-0.38-V)/1.5;
          // hardware CV at the Kelvin sense.
          double iSet = kIsetLutA[iset] * vin / kIsetRefRail * c.isetScale;
          double iCeil = (vin - kChgCeilDrop - vOpen) / (kChgCeilR + c.r0);
          double iCv = (c.cvSet - vOpen) / c.r0;
          I = iSet;
          if (iCeil < I) I = iCeil;
          if (iCv < I) I = iCv;
          if (I < 0) I = 0;
          if (vOpen + I * c.r0 >= kOvTrip) {  // latch: isolate + kill the charger
            c.ovLatched = true;
            I = 0;
          }
        } else if (dis) {
          // the load cannot start below 2.59 V; backstop at 2.22 V under load
          if (!c.disWasOn) c.uvTripped = vOpen < kUvRearm;
          if (c.uvTripped && vOpen > kUvRearm) c.uvTripped = false;
          if (!c.uvTripped) {
            // 2.75 ohm load + shunt + 2 x 28 mOhm + B- path + wiring: ~1.07 A at 3.29 V
            I = -vOpen / (kRLoad + kRShunt + 0.056 + c.contactR + 0.131 + c.r0);
            if (vOpen + I * c.r0 < kUvTrip) {
              c.uvTripped = true;
              I = 0;
            }
          }
        }
        I += strayA[k - 1];
      }
      c.disWasOn = dis;
      c.i = I;
      if (chgOn(k) && c.present && c.ocv() < 3.00) {
        c.contOnLowS += dt;
        if (c.contOnLowS > c.maxContOnLowS) c.maxContOnLowS = c.contOnLowS;
      } else {
        c.contOnLowS = 0;
      }
      double v = c.vTerm();
      c.soc += I * dt / 3600.0 / c.qAh;
      if (d != c.expDtUs || c.tau != c.expTau) {  // cache the RC decay factor
        c.expDtUs = d;
        c.expTau = c.tau;
        c.expFac = 1.0 - exp(-dt / c.tau);
      }
      c.vrc += (I * c.r1 - c.vrc) * c.expFac;
      if (I > 0) {
        c.ahIn += I * dt / 3600.0;
        c.whIn += I * v * dt / 3600.0;
      } else {
        c.ahOut -= I * dt / 3600.0;
        c.whOut -= I * v * dt / 3600.0;
      }
    }
    {  // IO1: mux A output / 2 through 1M/1M with 10 nF (tau 5 ms)
      double target = bPlusAbs(muxCh()) / 2.0;
      vchkNode += (target - vchkNode) * (1.0 - exp(-(double)d / 5000.0));
    }
    nowUs += d;
    dtUs -= d;
  }
}

// --------------------------------------------------------- analog values ----
double SimBoard::cellV(int k) const {
  const SimCell &c = cell[k - 1];
  if (!c.present) return 0.03;  // §5: an empty slot reads ~0.03 V
  if (c.reversed) return -c.ocv();
  return c.vTerm();
}

double SimBoard::bMinusAbs(int k) const {
  const SimCell &c = cell[k - 1];
  if (!c.present || c.reversed) return 0.0;
  // §5: AIN1-AIN3 = I * (B- wire/contact + 2 x 28 mOhm + 0.1 ohm shunt)
  return c.i * (kRbmFixed + c.contactR);
}

double SimBoard::bPlusAbs(int k) const {
  const SimCell &c = cell[k - 1];
  if (!c.present) return 0.03;
  if (c.reversed) return 0.0;  // isolated by the protection, B+ sense sits near GND
  return cellV(k) + bMinusAbs(k);
}

int16_t SimBoard::toCode(double volts, uint8_t pga) {
  double fs = ads::fullScale((ads::Pga)pga);
  double c = volts / (fs / 32768.0);
  if (noiseLsb) {
    rng_ = rng_ * 1103515245u + 12345u;
    int n = (int)((rng_ >> 16) % (2 * noiseLsb + 1)) - (int)noiseLsb;
    c += n;
  }
  c = floor(c + 0.5);
  if (c > 32767) c = 32767;
  if (c < -32768) c = -32768;
  return (int16_t)c;
}

void SimBoard::latchConversion() {
  convLatched_ = true;
  uint8_t mux = (adsCfg_ >> 12) & 7u;
  uint8_t pga = (adsCfg_ >> 9) & 7u;
  int k = muxCh();
  const SimCell &c = cell[k - 1];
  double v = 0;
  switch (mux) {
    case 0: v = cellV(k); break;                               // AIN0-AIN1
    case 2: v = bMinusAbs(k); break;                           // AIN1-AIN3
    case 3: v = (c.present && !c.reversed) ? c.i * kRShunt : 0; break;  // AIN2-AIN3
    case 4: v = bPlusAbs(k); break;                            // AIN0-GND
    default: v = 0; break;
  }
  convResult_ = toCode(v, pga);
}

// ------------------------------------------------------------------ ADS -----
bool SimBoard::i2cOk() {
  adsTransactions++;
  if (!adsPresent || i2cFailAll) return false;
  if (i2cFailNext > 0) {
    i2cFailNext--;
    return false;
  }
  return true;
}

bool SimBoard::adsWrite(uint8_t reg, uint16_t value) {
  if (!i2cOk()) return false;
  if (reg != ads::kRegConfig) return true;
  adsCfg_ = value & 0x7FFFu;
  if (value & ads::kOsBit) {
    convPending_ = true;
    convLatched_ = false;
    convStartUs_ = nowUs;
    convPeriodUs_ = ads::periodUs((ads::Rate)((value >> 5) & 7u));
  }
  return true;
}

bool SimBoard::adsRead(uint8_t reg, uint16_t &value) {
  if (!i2cOk()) return false;
  bool busy = convPending_ && nowUs < convStartUs_ + convPeriodUs_;
  if (reg == ads::kRegConfig) {
    value = (uint16_t)(adsCfg_ | (busy ? 0 : ads::kOsBit));
    return true;
  }
  if (reg == ads::kRegConv) {
    if (convPending_ && !busy) {
      if (!convLatched_) latchConversion();
      convPending_ = false;
    }
    value = (uint16_t)convResult_;
    return true;
  }
  value = 0;
  return true;
}

int32_t SimBoard::adcReadMv(AdcPin pin) {
  int k = muxCh();
  const SimCell &c = cell[k - 1];
  switch (pin) {
    case AdcPin::Ntc: {
      if (!c.present || !c.ntcFitted) return 3250;
      if (c.ntcShort) return 20;
      double t = c.tempC + 273.15;
      double r = kNtcR25 * exp(kNtcBeta * (1.0 / t - 1.0 / 298.15));
      return (int32_t)lround(3300.0 * r / (kNtcPullup + r));
    }
    case AdcPin::Vchk: {
      double v = vchkNode + vchkErrV[k - 1] / 2.0;
      if (v < 0) v = 0;
      return (int32_t)lround(v * 1000.0);
    }
    case AdcPin::Vin: return (int32_t)lround(vin * kDivVin * 1000.0);
    case AdcPin::Tbrd: return (int32_t)lround((kTbrdV25Default + kTbrdSlope * (tBoard - 25.0)) * 1000.0);
  }
  return -1;
}
