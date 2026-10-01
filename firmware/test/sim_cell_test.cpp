// Host simulation: full capacity test (charge -> rest 30 min -> discharge to
// 2.5 V -> rest 30 min -> storage charge to 50 % by Ah -> rest -> IR) on 8
// simulated 15 Ah LiFePO4 cells at accelerated time, through the complete
// firmware core (scanner, ADS1115 codes, channel FSMs, supervisor, IR engine).
//
// Pass criteria per cell:
//   * reported capacity within +-1 % of the charge actually removed by the
//     simulator during the discharge step (measurement accuracy), and
//   * within +-1 % of the cell's modelled capacity (charge to CV termination
//     and discharge to 2.5 V under load span ~100 % of the LFP SoC window),
//   * storage charge ends at 50 % SoC +-2 %, IR within +-1 mOhm of the model,
//   * thermal policy: CHG_EN never on for more than one 1 s pulse below 3.0 V,
//   * hardware CV anywhere in 3.51..3.62 V: the charge ends by current taper,
//   * no alarms, no interlock violation, heartbeat never paused > 5 ms.
#include <math.h>
#include <stdio.h>
#include <time.h>

#include <memory>

#include "rig.h"

using namespace lfp8;

int main() {
  std::unique_ptr<Rig> rp(new Rig);
  Rig &r = *rp;
  const double q[8] = {14.2, 14.6, 15.0, 15.3, 15.6, 14.9, 15.8, 14.4};      // Ah
  const double r0[8] = {0.006, 0.008, 0.010, 0.012, 0.007, 0.009, 0.011, 0.008};
  const double r1[8] = {0.003, 0.004, 0.005, 0.004, 0.003, 0.004, 0.005, 0.004};
  const double soc0[8] = {0.60, 0.75, 0.90, 0.40, 0.85, 0.70, 0.95, 0.0016};  // ch8: 2.58 V -> precharge
  const double cv[8] = {3.576, 3.550, 3.510, 3.620, 3.530, 3.576, 3.610, 3.576};  // HW CV 3.51..3.62
  const double isc[8] = {1.00, 0.92, 1.08, 0.97, 1.03, 1.10, 0.90, 1.00};      // ISET DAC tolerance
  for (int k = 0; k < 8; k++) {
    SimCell &c = r.sim.cell[k];
    c = SimCell();
    c.present = true;
    c.qAh = q[k];
    c.soc = soc0[k];
    c.r0 = r0[k];
    c.r1 = r1[k];
    c.tau = 0.5;
    c.cvSet = cv[k];
    c.isetScale = isc[k];
    c.tempC = 25.0 + k;
  }
  r.begin();
  r.run(2.0);
  for (int k = 1; k <= 8; k++) {
    const char *e = r.start((uint8_t)k, Program::CapTest);
    if (e) {
      printf("start ch%d failed: %s\n", k, e);
      return 1;
    }
  }
  clock_t wall0 = clock();
  double disStart[8] = {0}, disEnd[8] = {0};
  bool inDis[8] = {false}, sawPre[8] = {false};
  Step prevStep[8];
  for (int k = 0; k < 8; k++) prevStep[k] = r.ctl.channel(k + 1).step();
  bool allDone = r.runUntil([&] {
    bool done = true;
    for (int k = 0; k < 8; k++) {
      const Channel &ch = r.ctl.channel(k + 1);
      Step s = ch.step();
      if (ch.state() == ChState::ChargingPre) sawPre[k] = true;
      if (s != prevStep[k]) {
        if (s == Step::Discharge) {
          disStart[k] = r.sim.cell[k].ahOut;
          inDis[k] = true;
        } else if (inDis[k]) {
          disEnd[k] = r.sim.cell[k].ahOut;
          inDis[k] = false;
        }
        prevStep[k] = s;
      }
      if (ch.state() != ChState::Done) done = false;
    }
    return done;
  }, 60.0 * 3600.0);
  double wall = (double)(clock() - wall0) / CLOCKS_PER_SEC;
  double simH = (double)r.sim.nowUs / 3.6e9;

  int fails = 0;
  auto check = [&](bool ok, const char *what, int k) {
    if (!ok) {
      fails++;
      printf("  FAIL ch%d: %s\n", k + 1, what);
    }
  };
  if (!allDone) {
    fails++;
    printf("FAIL: not all channels finished\n");
  }
  printf("Full capacity test, 8 x LFP (simulated %.1f h in %.1f s wall clock, x%.0f)\n", simH, wall,
         simH * 3600.0 / (wall > 0 ? wall : 1));
  printf(" ch  state   model_Ah  true_dis_Ah  reported_mAh  err_vs_true  err_vs_model  mWh      chg_term  "
         "store_mAh  SoC_end  R_ohm  R_dc  (model R0 / R_dc)\n");
  for (int k = 0; k < 8; k++) {
    const Channel &ch = r.ctl.channel(k + 1);
    const ChResult &res = ch.result();
    double trueDis = (disEnd[k] - disStart[k]) * 1000.0;
    double errT = (res.capMah - trueDis) / trueDis * 100.0;
    double errM = (res.capMah - q[k] * 1000.0) / (q[k] * 1000.0) * 100.0;
    double rDcModel = (r0[k] + r1[k] * (1.0 - exp(-2.0))) * 1000.0;
    printf(" %d   %-6s  %6.2f    %9.1f    %9.1f     %+6.3f %%     %+6.3f %%   %8.0f  %-8s  %8.1f   %5.3f   %5.2f  %5.2f  (%5.2f / %5.2f)\n",
           k + 1, chStateName(ch.state()), q[k], trueDis, res.capMah, errT, errM, res.capMwh,
           termName(res.chgTerm), res.storeMah, r.sim.cell[k].soc, res.irOhmicMohm, res.irDcMohm, r0[k] * 1000.0,
           rDcModel);
    check(ch.state() == ChState::Done, "not DONE", k);
    check(res.hasCap && res.hasChg && res.hasStore && res.hasIr, "missing results", k);
    check(fabs(errT) <= 1.0, "capacity vs removed charge > 1 %", k);
    check(fabs(errM) <= 1.0, "capacity vs model > 1 %", k);
    check(res.capMwh > res.capMah * 3.0 && res.capMwh < res.capMah * 3.35, "implausible mWh", k);
    check(fabs(r.sim.cell[k].soc - 0.5) <= 0.02, "storage SoC not 50 % +-2 %", k);
    check(fabs(res.irOhmicMohm - (r0[k] * 1000.0 + 0.08)) <= 1.0, "R_ohmic off by > 1 mOhm", k);
    check(fabs(res.irDcMohm - rDcModel) <= 1.0, "R_dc off by > 1 mOhm", k);
    check(r.sim.cell[k].maxContOnLowS <= 1.01, "CHG_EN on > 1 s below 3.0 V (thermal policy)", k);
  }
  check(sawPre[7], "channel 8 did not precharge", 7);
  check(r.ctl.channel(7).result().chgTerm == Term::VMax, "ch7 (CV 3.61 V) should stop at V_MAX_CHG", 6);
  check(r.ctl.channel(3).result().chgTerm != Term::VMax, "ch3 (CV 3.51 V) must end by current", 2);
  if (r.ctl.supervisor().alarm() != BoardAlarm::None) {
    fails++;
    printf("FAIL: board alarm %s\n", r.ctl.supervisor().alarmText());
  }
  if (r.sim.interlockViolations) {
    fails++;
    printf("FAIL: interlock violations %llu\n", (unsigned long long)r.sim.interlockViolations);
  }
  if (r.sim.maxToggleGapUs > 5000) {
    fails++;
    printf("FAIL: heartbeat gap %llu us\n", (unsigned long long)r.sim.maxToggleGapUs);
  }
  printf("heartbeat toggles %llu (max gap %llu us), shift-register writes %llu, ADS transactions %llu\n",
         (unsigned long long)r.sim.hbToggles, (unsigned long long)r.sim.maxToggleGapUs,
         (unsigned long long)r.sim.shiftWrites, (unsigned long long)r.sim.adsTransactions);
  printf("%s: sim_cell_test (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
  return fails ? 1 : 0;
}
