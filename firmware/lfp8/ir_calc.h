// Internal-resistance computation (INTERFACE.md §7.1).
//
//   R_ohmic = (V(10 ms) - V0) / (I(10 ms) - I0)
//   R_dc    = (V(1 s)   - V0) / (I(1 s)   - I0)
//
// With the firmware sign convention (I < 0 = discharge) both a discharge
// pulse (dV < 0, dI < 0) and the charge-pulse fallback (dV > 0, dI > 0) give a
// positive resistance; this equals the §7.1 formula written with the
// discharge current as a positive magnitude ("sign flipped" for the charge
// pulse). V(t) and I(t) are evaluated with a least-squares line through the
// samples in a short window around t (falls back to the nearest two samples).
#pragma once
#include <stdint.h>

namespace lfp8 {

struct IrPoint {
  float t;  // s since the load/charger was switched on
  float y;  // V or A
};

struct IrResult {
  bool ok = false;
  bool chargePulse = false;  // true: charge-pulse fallback was used (cell < 2.65 V)
  float v0 = 0, i0 = 0;      // rest values (16-sample averages)
  float vOhmic = 0, iOhmic = 0;  // values at 10 ms
  float vDc = 0, iDc = 0;        // values at 1 s
  float rOhmic = 0;          // ohm
  float rDc = 0;             // ohm
  float iPulse = 0;          // A (signed) - measurement current at 1 s
  const char *err = "";
};

// Evaluate y(t) from samples (sorted by t) with a line fit over [ta, tb].
// Returns false when fewer than 2 usable samples exist.
bool irEvalAt(const IrPoint *p, int n, float t, float ta, float tb, float &out);

// Compute both resistances. v/i arrays must be sorted by time.
bool irCompute(float v0, float i0, const IrPoint *v, int nv, const IrPoint *i, int ni,
               bool chargePulse, IrResult &r);

}  // namespace lfp8
