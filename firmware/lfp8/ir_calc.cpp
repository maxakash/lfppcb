#include "ir_calc.h"

#include <math.h>

#include "lfp8_config.h"

namespace lfp8 {

bool irEvalAt(const IrPoint *p, int n, float t, float ta, float tb, float &out) {
  // Least-squares line over samples within [ta, tb].
  double sx = 0, sy = 0, sxx = 0, sxy = 0;
  int cnt = 0;
  for (int k = 0; k < n; k++) {
    if (p[k].t < ta || p[k].t > tb) continue;
    double x = p[k].t - t, y = p[k].y;
    sx += x;
    sy += y;
    sxx += x * x;
    sxy += x * y;
    cnt++;
  }
  if (cnt >= 2) {
    double den = cnt * sxx - sx * sx;
    if (fabs(den) > 1e-15) {
      double b = (cnt * sxy - sx * sy) / den;
      double a = (sy - b * sx) / cnt;  // value at x = 0, i.e. at t
      out = (float)a;
      return true;
    }
  }
  // Fallback: linear interpolation between the samples bracketing t.
  int lo = -1, hi = -1;
  for (int k = 0; k < n; k++) {
    if (p[k].t <= t) lo = k;
    if (p[k].t >= t) {
      hi = k;
      break;
    }
  }
  if (lo < 0 || hi < 0) return false;
  if (lo == hi || p[hi].t - p[lo].t < 1e-9f) {
    out = p[lo].y;
    return true;
  }
  float f = (t - p[lo].t) / (p[hi].t - p[lo].t);
  out = p[lo].y + f * (p[hi].y - p[lo].y);
  return true;
}

bool irCompute(float v0, float i0, const IrPoint *v, int nv, const IrPoint *i, int ni,
               bool chargePulse, IrResult &r) {
  r = IrResult();
  r.v0 = v0;
  r.i0 = i0;
  r.chargePulse = chargePulse;
  // Window for 10 ms: 6..14 ms; window for 1 s: 0.90 s .. end of pulse.
  if (!irEvalAt(v, nv, kIrTOhmic, 0.006f, 0.014f, r.vOhmic) ||
      !irEvalAt(i, ni, kIrTOhmic, 0.006f, 0.014f, r.iOhmic)) {
    r.err = "no samples at 10 ms";
    return false;
  }
  if (!irEvalAt(v, nv, kIrTDc, 0.90f, 1.05f, r.vDc) || !irEvalAt(i, ni, kIrTDc, 0.90f, 1.05f, r.iDc)) {
    r.err = "no samples at 1 s";
    return false;
  }
  float dI10 = r.iOhmic - i0;
  float dI1 = r.iDc - i0;
  r.iPulse = r.iDc;
  if (fabsf(dI10) < kIrMinDeltaI || fabsf(dI1) < kIrMinDeltaI) {
    r.err = "pulse current too small";
    return false;
  }
  // Direction must match the requested pulse.
  if ((chargePulse && dI1 < 0) || (!chargePulse && dI1 > 0)) {
    r.err = "pulse current has wrong sign";
    return false;
  }
  r.rOhmic = (r.vOhmic - v0) / dI10;
  r.rDc = (r.vDc - v0) / dI1;
  if (!(r.rOhmic > 0.0f) || !(r.rDc > 0.0f) || r.rOhmic > 1.0f || r.rDc > 1.0f) {
    r.err = "implausible resistance";
    return false;
  }
  r.ok = true;
  return true;
}

}  // namespace lfp8
