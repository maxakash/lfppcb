// Coulomb / energy counter. Trapezoidal integration of the measured current
// (sign convention: I > 0 charging, I < 0 discharging). Each half of a
// trapezoid is booked into the bucket of its own sign, so a sign change
// inside one interval is handled correctly. Accumulators are double so that
// 15 Ah in ~0.04 mAh steps does not lose precision.
#pragma once
#include <stdint.h>

namespace lfp8 {

class Coulomb {
 public:
  static constexpr uint32_t kMaxGapMs = 10000;  // longer gaps are not integrated

  void reset() {
    chgMah = chgMwh = disMah = disMwh = 0.0;
    havePrev_ = false;
    gaps = 0;
    samples = 0;
  }

  // v in V, i in A, t in ms (wrap-safe).
  void add(float v, float i, uint32_t tMs) {
    samples++;
    if (havePrev_) {
      uint32_t dt = tMs - tPrev_;
      if (dt > kMaxGapMs) {
        gaps++;
      } else if (dt > 0) {
        const double h = (double)dt / 3600000.0 * 0.5;  // hours, half interval
        book(iPrev_, vPrev_, h);
        book(i, v, h);
      }
    }
    havePrev_ = true;
    tPrev_ = tMs;
    iPrev_ = i;
    vPrev_ = v;
  }

  // Forget the previous point (e.g. after a pause with outputs off).
  void breakChain() { havePrev_ = false; }

  double chgMah = 0, chgMwh = 0, disMah = 0, disMwh = 0;
  uint32_t gaps = 0;
  uint32_t samples = 0;

 private:
  void book(float i, float v, double h) {
    double q = (double)i * 1000.0 * h;  // mAh
    double e = q * (double)v;           // mWh
    if (i >= 0) {
      chgMah += q;
      chgMwh += e;
    } else {
      disMah -= q;
      disMwh -= e;
    }
  }
  bool havePrev_ = false;
  uint32_t tPrev_ = 0;
  float iPrev_ = 0, vPrev_ = 0;
};

}  // namespace lfp8
