#include <math.h>
#include <string.h>

#include <memory>

#include "../lfp8/coulomb.h"
#include "../lfp8/ir_calc.h"
#include "../lfp8/lfp8_config.h"
#include "rig.h"
#include "test_framework.h"

using namespace lfp8;

TEST(coulomb_1A_for_1h_is_1000mAh) {
  Coulomb c;
  c.reset();
  // 5 Hz sampling, 1.000 A, 3.300 V for exactly one hour
  for (uint32_t t = 0; t <= 3600u * 1000u; t += 200) c.add(3.300f, 1.000f, t);
  CHECK_NEAR(c.chgMah, 1000.0, 1000.0 * 0.005);  // spec: +-0.5 %
  CHECK_NEAR(c.chgMah, 1000.0, 1e-6);            // actually exact
  CHECK_NEAR(c.chgMwh, 3300.0, 1e-3);
  CHECK_NEAR(c.disMah, 0.0, 0);
  // discharge, irregular sample spacing (150..250 ms jitter)
  Coulomb d;
  uint32_t t = 0, seed = 7;
  d.add(3.2f, -1.0f, t);
  while (t < 3600u * 1000u) {
    seed = seed * 1103515245u + 12345u;
    uint32_t dt = 150 + (seed >> 16) % 101;
    if (t + dt > 3600u * 1000u) dt = 3600u * 1000u - t;
    t += dt;
    d.add(3.2f, -1.0f, t);
  }
  CHECK_NEAR(d.disMah, 1000.0, 1e-6);
  CHECK_NEAR(d.disMwh, 3200.0, 1e-3);
  CHECK_NEAR(d.chgMah, 0.0, 0);
}

TEST(coulomb_sign_change_and_gaps) {
  Coulomb c;
  c.add(3.3f, 1.0f, 0);
  c.add(3.3f, -1.0f, 3600);  // half of the 3.6 s interval booked each way
  CHECK_NEAR(c.chgMah, 0.5, 1e-9);
  CHECK_NEAR(c.disMah, 0.5, 1e-9);
  Coulomb g;
  g.add(3.3f, 1.0f, 0);
  g.add(3.3f, 1.0f, 20000);  // 20 s gap: not integrated (fault elsewhere)
  CHECK_EQ(g.gaps, 1);
  CHECK_NEAR(g.chgMah, 0.0, 0);
  // millis() wrap-around
  Coulomb w;
  w.add(3.3f, 1.0f, 0xFFFFFF00u);
  w.add(3.3f, 1.0f, 0x00000100u);  // 512 ms later
  CHECK_NEAR(w.chgMah, 512.0 / 3600.0, 1e-9);
}

TEST(coulomb_full_loop_1A_1h_through_controller) {
  // The real scanner (ADS1115 codes, ~6 Hz per channel) against the simulated
  // charger set to exactly 1.000 A.
  std::unique_ptr<Rig> rp(new Rig);  // ~110 KB: keep off the stack
  Rig &r = *rp;
  SimCell &c = r.sim.cell[0];
  c.present = true;
  c.qAh = 100.0;  // large: stays in CC for the whole hour
  c.soc = 0.30;
  c.isetScale = 1.0 / 1.064;  // ISET 4 at 5.20 V -> exactly 1.000 A (ceiling ~1.03 A)
  r.begin();
  r.run(1.0);
  CHECK(r.start(1, Program::Charge) == nullptr);
  r.run(3600.0);
  const ChStatus *s = nullptr;
  static StatusSnapshot snap;
  r.ctl.snapshot(snap);
  s = &snap.ch[0];
  CHECK(s->st == ChState::ChargingCc);
  double truth = c.ahIn * 1000.0;
  CHECK_NEAR(truth, 1000.0, 2.0);
  CHECK_NEAR(s->stepChgMah, truth, truth * 0.005);  // +-0.5 %
  CHECK_NEAR(s->stepChgMah, 1000.0, 5.0);
  printf("    measured %.2f mAh, true %.2f mAh (%.3f %%)\n", s->stepChgMah, truth,
         (s->stepChgMah - truth) / truth * 100.0);
  CHECK_EQ(r.sim.interlockViolations, 0);
}

// ---- IR ----
namespace {
// OCV 3.30 V, R0 = 8 mOhm, R1 = 4 mOhm, tau = 0.5 s, step of I at t = 0
double cellV(double t, double I) {
  if (t < 0) return 3.30;
  return 3.30 + I * 0.008 + I * 0.004 * (1.0 - exp(-t / 0.5));
}
float quantV(double v) { return (float)(floor(v / 125e-6 + 0.5) * 125e-6); }
float quantI(double i) { return (float)(floor(i * 0.1 / 7.8125e-6 + 0.5) * 7.8125e-6 / 0.1); }
}  // namespace

TEST(ir_compute_simulated_cell) {
  static IrPoint v[600], i[600];
  for (int pass = 0; pass < 2; pass++) {
    bool chg = pass == 1;
    double I = chg ? 0.968 : -1.05;
    int nv = 0, ni = 0;
    // alternate V/I conversions at 860 SPS (1.163 ms each), timestamp = mid conversion
    double t = 0.000582;
    for (int k = 0; t < 1.02; k++, t += 0.001163 + 0.0002) {
      if (k % 2 == 0) v[nv++] = IrPoint{(float)t, quantV(cellV(t, I))};
      else i[ni++] = IrPoint{(float)t, quantI(I)};
    }
    IrResult r;
    bool ok = irCompute(quantV(3.30), quantI(0.0), v, nv, i, ni, chg, r);
    CHECK(ok);
    CHECK_NEAR(r.rOhmic * 1000.0, 8.0, 1.0);   // spec: ~8 mOhm +-1
    CHECK_NEAR(r.rDc * 1000.0, 12.0, 1.0);     // spec: ~12 mOhm +-1
    // exact model values: 8 + 4(1-e^-0.02) = 8.08, 8 + 4(1-e^-2) = 11.46
    CHECK_NEAR(r.rOhmic * 1000.0, 8.079, 0.25);
    CHECK_NEAR(r.rDc * 1000.0, 11.459, 0.25);
    CHECK_NEAR(r.iPulse, I, 0.001);
    CHECK(r.chargePulse == chg);
  }
  // load did not switch on -> error, no result
  int n = 0;
  for (double t = 0.0005; t < 1.02; t += 0.0024) v[n] = IrPoint{(float)t, 3.30f}, i[n] = IrPoint{(float)t, 0.0f}, n++;
  IrResult r;
  CHECK(!irCompute(3.30f, 0.0f, v, n, i, n, false, r));
  CHECK(!r.ok);
  CHECK(strlen(r.err) > 0);
}

TEST(ir_full_loop_discharge_pulse) {
  std::unique_ptr<Rig> rp(new Rig);  // ~110 KB: keep off the stack
  Rig &r = *rp;
  SimCell &c = r.sim.cell[2];
  c.present = true;
  c.soc = 0.50;  // OCV 3.30 V
  c.r0 = 0.008;
  c.r1 = 0.004;
  c.tau = 0.5;
  r.begin();
  r.run(1.0);
  CHECK(r.start(3, Program::Ir) == nullptr);
  CHECK(r.st(3) == ChState::IrMeasure);
  bool done = r.runUntil([&] { return r.st(3) == ChState::Done; }, 20.0);
  CHECK(done);
  const ChResult &res = r.ctl.channel(3).result();
  CHECK(res.hasIr);
  CHECK(!res.irChargePulse);
  CHECK_NEAR(res.irOhmicMohm, 8.0, 1.0);
  CHECK_NEAR(res.irDcMohm, 12.0, 1.0);
  CHECK_NEAR(res.irI, -1.07, 0.03);  // §8: ~1.07 A at 3.29 V
  CHECK_NEAR(res.irV0, 3.30, 0.002);
  printf("    R_ohmic = %.3f mOhm, R_dc = %.3f mOhm at %.3f A\n", res.irOhmicMohm, res.irDcMohm, res.irI);
  CHECK(!r.sim.disOn(3));  // load off again
  CHECK_EQ(r.sim.interlockViolations, 0);
  // heartbeat kept toggling during the IR engine (fast loop)
  CHECK(r.sim.maxToggleGapUs < 5000);
}

TEST(ir_full_loop_charge_pulse_fallback_below_2v65) {
  std::unique_ptr<Rig> rp(new Rig);  // ~110 KB: keep off the stack
  Rig &r = *rp;
  r.cfg.iset = 2;  // user ISET; the IR pulse must use ISET 4 (§7.1)
  SimCell &c = r.sim.cell[0];
  c.present = true;
  c.soc = 0.0016;  // OCV 2.58 V: the UV backstop would block the load
  r.begin();
  r.run(1.0);
  CHECK(r.start(1, Program::Ir) == nullptr);
  bool sawIset4 = false, sawDis = false;
  bool done = r.runUntil([&] {
    if (r.sim.chgOn(1) && ((r.sim.word >> 27) & 7u) == 4u) sawIset4 = true;
    if (r.sim.disOn(1)) sawDis = true;
    return r.st(1) == ChState::Done;
  }, 20.0);
  CHECK(done);
  CHECK(sawIset4);
  CHECK(!sawDis);
  CHECK(((r.sim.word >> 27) & 7u) == 2u);  // restored
  const ChResult &res = r.ctl.channel(1).result();
  CHECK(res.hasIr);
  CHECK(res.irChargePulse);
  CHECK(res.irI > 0.9f);
  CHECK_NEAR(res.irOhmicMohm, 8.0, 1.0);
  // the OCV slope at 0.2 % SoC adds a little to the 1 s value
  CHECK_NEAR(res.irDcMohm, 12.0, 1.5);
  printf("    charge pulse: R_ohmic = %.3f mOhm, R_dc = %.3f mOhm at %.3f A\n", res.irOhmicMohm, res.irDcMohm,
         res.irI);
}
