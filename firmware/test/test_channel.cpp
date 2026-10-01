// Channel state machine, driven with synthetic samples (no board model).
#include <string.h>

#include "../lfp8/channel.h"
#include "test_framework.h"

using namespace lfp8;

namespace {
struct ChRig {
  Settings cfg;
  Channel ch;
  ChannelCtx c;
  uint32_t t = 1000;
  ChRig() {
    settingsDefaults(cfg);
    ch.init(1);
    c.cfg = &cfg;
    c.isetEff = 4;
    c.vin = 5.2f;
    c.powerOk = true;
    c.boardHot = false;
    c.nowMs = t;
  }
  void sample(float v, float i, float temp = 25.0f, NtcStatus ntc = NtcStatus::Ok) {
    ChSample s;
    s.tMs = t;
    s.valid = true;
    s.v = v;
    s.i = i;
    s.tCell = temp;
    s.ntc = ntc;
    c.nowMs = t;
    ch.onSample(s, c);
    ch.tick(c);
  }
  void tick() {
    c.nowMs = t;
    ch.tick(c);
  }
  // feed a constant sample at 5 Hz for 'sec' seconds
  void hold(float sec, float v, float i, float temp = 25.0f) {
    for (float s = 0; s < sec; s += 0.2f) {
      t += 200;
      sample(v, i, temp);
    }
  }
  const char *start(Program p) {
    c.nowMs = t;
    return ch.start(p, c);
  }
};
}  // namespace

TEST(channel_presence_empty_idle_reversed) {
  ChRig r;
  r.sample(0.02f, 0.0f);
  CHECK(r.ch.state() == ChState::Empty);
  r.t += 200;
  r.sample(3.30f, 0.0f);
  CHECK(r.ch.state() == ChState::Idle);
  r.t += 200;
  r.sample(-2.9f, 0.0f);  // §5: reversed cell reads -1..-3.3 V
  CHECK(r.ch.state() == ChState::Reversed);
  CHECK(r.start(Program::Charge) != nullptr);  // refuse
  CHECK(!r.ch.wantChg());
  r.t += 200;
  r.sample(0.0f, 0.0f);
  CHECK(r.ch.state() == ChState::Empty);
  CHECK(r.start(Program::Discharge) != nullptr);
}

TEST(channel_refuses_to_charge_dead_cell) {
  ChRig r;
  r.sample(1.85f, 0.0f);  // < V_DEAD = 2.0 V
  CHECK(r.ch.state() == ChState::Idle);
  const char *e = r.start(Program::Charge);
  CHECK(e != nullptr);
  CHECK(r.ch.state() == ChState::FaultDeadCell);
  CHECK(!r.ch.wantChg() && !r.ch.wantDis());
  r.c.nowMs = r.t;
  r.ch.reset(r.c);
  CHECK(r.ch.state() == ChState::Idle);
  CHECK(r.start(Program::CapTest) != nullptr);
  CHECK(r.ch.state() == ChState::FaultDeadCell);
}

namespace {
// Run 'ms' milliseconds with 10 ms ticks and a 5 Hz sample (v/i depend on CHG_EN);
// returns on/off durations seen (excluding the first partial phase).
void pulseRun(ChRig &r, int ms, float vOn, float vOff, int &onMs, int &offMs, int &nOn, int &nOff) {
  bool prev = r.ch.wantChg();
  uint32_t lastEdge = r.t;
  bool first = true;
  onMs = offMs = nOn = nOff = 0;
  for (int t = 0; t < ms; t += 10) {
    r.t += 10;
    if (t % 200 == 0) r.sample(r.ch.wantChg() ? vOn : vOff, r.ch.wantChg() ? 0.95f : 0.0f);
    else r.tick();
    bool now = r.ch.wantChg();
    if (now != prev) {
      int d = (int)(r.t - lastEdge);
      if (!first) {
        if (prev) {
          onMs = d;
          nOn++;
        } else {
          offMs = d;
          nOff++;
        }
      }
      first = false;
      lastEdge = r.t;
      prev = now;
    }
  }
}
}  // namespace

TEST(channel_thermal_policy_pulse_modes) {
  // §4: 2.00..2.80 V -> 20 % duty (1 s on / 4 s off); 2.80..3.05 V -> 50 %
  // duty with 1 s period; continuous CC only from 3.05 V.
  ChRig r;
  r.sample(2.60f, 0.0f);
  CHECK(r.start(Program::Charge) == nullptr);
  CHECK(r.ch.state() == ChState::ChargingPre);
  CHECK_EQ(r.ch.pulseDuty(), 20);
  CHECK(r.ch.wantChg());
  int on, off, nOn, nOff;
  pulseRun(r, 20000, 2.70f, 2.62f, on, off, nOn, nOff);
  CHECK(nOn >= 3 && nOff >= 3);
  CHECK_NEAR(on, 1000, 20);
  CHECK_NEAR(off, 4000, 20);
  CHECK_EQ(r.ch.pulseDuty(), 20);
  // rest voltage reaches 2.80 V -> 50 % duty
  pulseRun(r, 6000, 2.95f, 2.85f, on, off, nOn, nOff);
  CHECK(r.ch.state() == ChState::ChargingPre);
  CHECK_EQ(r.ch.pulseDuty(), 50);
  pulseRun(r, 10000, 3.00f, 2.95f, on, off, nOn, nOff);
  CHECK(nOn >= 5 && nOff >= 5);
  CHECK_NEAR(on, 500, 20);
  CHECK_NEAR(off, 500, 20);
  CHECK_EQ(r.ch.pulseDuty(), 50);
  // rest voltage >= 3.05 V -> continuous CC
  pulseRun(r, 3000, 3.12f, 3.06f, on, off, nOn, nOff);
  CHECK(r.ch.state() == ChState::ChargingCc);
  CHECK(r.ch.wantChg());
  CHECK_EQ(r.ch.pulseDuty(), 0);
  // in CC the voltage under charge falls below 3.05 - 0.08 V -> pulse again
  r.hold(1.0f, 2.95f, 0.95f);
  CHECK(r.ch.state() == ChState::ChargingPre);
  CHECK_EQ(r.ch.pulseDuty(), 50);
  // starting directly at 2.9 V uses 50 % duty, at 3.1 V continuous CC
  ChRig q;
  q.sample(2.90f, 0.0f);
  q.start(Program::Charge);
  CHECK(q.ch.state() == ChState::ChargingPre && q.ch.pulseDuty() == 50);
  ChRig p;
  p.sample(3.10f, 0.0f);
  p.start(Program::Charge);
  CHECK(p.ch.state() == ChState::ChargingCc);
}

TEST(channel_ov_latch_detection) {
  // §6: CHG_EN = 1, current ~0 and V_cell >= 3.75 V -> hardware OV latch
  ChRig r;
  r.cfg.vAbsMax = 3.80f;  // so that only the latch rule can trigger
  r.cfg.vMaxChg = 3.65f;
  r.sample(3.40f, 0.0f);
  CHECK(r.start(Program::Charge) == nullptr);
  r.hold(2, 3.50f, 0.6f);
  r.t += 200;
  r.sample(3.76f, 0.0f);
  r.t += 200;
  r.sample(3.76f, 0.0f);
  CHECK(r.ch.state() == ChState::FaultOv);
  CHECK(strstr(r.ch.msg(), "latch") != nullptr);
  CHECK(!r.ch.wantChg());
}

TEST(channel_cv_plateau_terminates_without_reaching_3v6) {
  // §4: hardware CV can be as low as 3.51 V; the current may level off above
  // I_TERM. A flat CV current (< 0.2 A) for 10 min ends the charge.
  ChRig r;
  r.sample(3.30f, 0.0f);
  r.start(Program::Charge);
  r.hold(2, 3.505f, 0.5f);
  CHECK(r.ch.state() == ChState::ChargingCv);
  r.hold(9 * 60, 3.508f, 0.11f);
  CHECK(r.ch.state() == ChState::ChargingCv);
  r.hold(3 * 60, 3.508f, 0.108f);
  CHECK(r.ch.state() == ChState::Done);
  CHECK(r.ch.result().chgTerm == Term::Plateau);
  // a current that is still falling does not end the charge
  ChRig q;
  q.sample(3.30f, 0.0f);
  q.start(Program::Charge);
  q.hold(2, 3.51f, 0.5f);
  float i = 0.19f;
  for (int m = 0; m < 14; m++) {
    q.hold(60, 3.51f, i);
    i *= 0.93f;
  }
  CHECK(q.ch.state() == ChState::ChargingCv);
}

TEST(channel_discharge_refused_when_load_cannot_start) {
  ChRig r;
  r.sample(2.60f, 0.0f);  // §6: the load cannot start below 2.59 V
  CHECK(r.start(Program::Discharge) != nullptr);
  ChRig q;
  q.sample(2.70f, 0.0f);
  CHECK(q.start(Program::Discharge) == nullptr);
}

TEST(channel_cc_cv_and_i_term_termination) {
  ChRig r;
  r.sample(3.30f, 0.0f);
  CHECK(r.start(Program::Charge) == nullptr);
  CHECK(r.ch.state() == ChState::ChargingCc);
  r.hold(10, 3.40f, 0.97f);
  CHECK(r.ch.state() == ChState::ChargingCc);
  r.hold(5, 3.53f, 0.60f);  // CV region
  CHECK(r.ch.state() == ChState::ChargingCv);
  r.hold(30, 3.565f, 0.20f);
  CHECK(r.ch.state() == ChState::ChargingCv);
  r.hold(59, 3.566f, 0.04f);  // < I_TERM, but not yet for 60 s
  CHECK(r.ch.state() == ChState::ChargingCv);
  r.hold(1.6f, 3.566f, 0.04f);
  CHECK(r.ch.state() == ChState::Done);
  CHECK(r.ch.result().hasChg);
  CHECK(r.ch.result().chgTerm == Term::ITerm);
  CHECK(!r.ch.wantChg());
  CHECK(r.ch.result().chgMah > 0);
}

TEST(channel_i_term_timer_restarts_when_current_rises) {
  ChRig r;
  r.sample(3.30f, 0.0f);
  r.start(Program::Charge);
  r.hold(2, 3.53f, 0.5f);
  CHECK(r.ch.state() == ChState::ChargingCv);
  r.hold(50, 3.565f, 0.04f);
  r.hold(1, 3.565f, 0.08f);  // current back above I_TERM
  r.hold(50, 3.565f, 0.04f);
  CHECK(r.ch.state() == ChState::ChargingCv);
  r.hold(12, 3.565f, 0.04f);
  CHECK(r.ch.state() == ChState::Done);
}

TEST(channel_vmax_stops_charge) {
  ChRig r;
  r.sample(3.30f, 0.0f);
  r.start(Program::Charge);
  r.hold(1, 3.45f, 0.9f);
  r.t += 200;
  r.sample(3.61f, 0.3f);
  r.t += 200;
  r.sample(3.61f, 0.3f);
  CHECK(r.ch.state() != ChState::Done);  // needs 3 consecutive samples
  r.t += 200;
  r.sample(3.61f, 0.3f);
  CHECK(r.ch.state() == ChState::Done);
  CHECK(r.ch.result().chgTerm == Term::VMax);
}

TEST(channel_ov_fault_while_charging_and_at_rest) {
  ChRig r;
  r.sample(3.30f, 0.0f);
  r.start(Program::Charge);
  r.t += 200;
  r.sample(3.72f, 0.9f);  // > V_ABS_MAX with CHG_EN=1 -> immediate
  CHECK(r.ch.state() == ChState::FaultOv);
  CHECK(!r.ch.wantChg());

  ChRig q;  // idle cell above V_ABS_MAX: fault after 2 s with CHG_EN = 0
  q.sample(3.75f, 0.0f);
  q.hold(1.6f, 3.75f, 0.0f);
  CHECK(q.ch.state() == ChState::Idle);
  q.hold(0.8f, 3.75f, 0.0f);
  CHECK(q.ch.state() == ChState::FaultOv);
  // a short excursion does not latch
  ChRig p;
  p.sample(3.75f, 0.0f);
  p.hold(1.0f, 3.75f, 0.0f);
  p.hold(3.0f, 3.65f, 0.0f);
  CHECK(p.ch.state() == ChState::Idle);
}

TEST(channel_discharge_cutoff_at_2v5) {
  ChRig r;
  r.sample(3.25f, 0.0f);
  CHECK(r.start(Program::Discharge) == nullptr);
  CHECK(r.ch.state() == ChState::Discharging);
  CHECK(r.ch.wantDis() && !r.ch.wantChg());
  r.hold(60, 3.10f, -1.05f);
  r.hold(10, 2.60f, -1.0f);
  CHECK(r.ch.state() == ChState::Discharging);
  r.t += 200;
  r.sample(2.495f, -0.95f);
  r.t += 200;
  r.sample(2.49f, -0.95f);
  CHECK(r.ch.state() == ChState::Discharging);  // debounced (3 samples)
  r.t += 200;
  r.sample(2.49f, -0.95f);
  CHECK(r.ch.state() == ChState::Done);
  CHECK(!r.ch.wantDis());
  CHECK(r.ch.result().hasCap);
  CHECK(r.ch.result().disTerm == Term::Cutoff);
  // ~70.6 s at ~1.04 A
  CHECK_NEAR(r.ch.result().capMah, 60.0 * 1.05 / 3.6 + 10.6 * 1.0 / 3.6, 0.6);
  // immediate cut-off far below V_MIN_DIS
  ChRig q;
  q.sample(3.2f, 0.0f);
  q.start(Program::Discharge);
  q.t += 200;
  q.sample(2.30f, -0.9f);
  CHECK(q.ch.state() == ChState::Done);
}

TEST(channel_charge_timeout) {
  ChRig r;
  r.cfg.maxChgH = 1.0f;
  r.sample(3.30f, 0.0f);
  r.start(Program::Charge);
  r.hold(3590, 3.35f, 0.97f);
  CHECK(r.ch.state() == ChState::ChargingCc);
  r.hold(15, 3.35f, 0.97f);
  CHECK(r.ch.state() == ChState::FaultTimeout);
  CHECK(!r.ch.wantChg());
}

TEST(channel_discharge_timeout) {
  ChRig r;
  r.cfg.maxDisH = 1.0f;
  r.sample(3.30f, 0.0f);
  r.start(Program::Discharge);
  r.hold(3610, 3.20f, -1.0f);
  CHECK(r.ch.state() == ChState::FaultTimeout);
  CHECK(!r.ch.wantDis());
}

TEST(channel_cell_temperature_pause_resume) {
  ChRig r;
  r.sample(3.30f, 0.0f);
  r.start(Program::Charge);
  r.hold(2, 3.40f, 0.97f, 30.0f);
  CHECK(r.ch.state() == ChState::ChargingCc);
  r.hold(0.4f, 3.40f, 0.97f, 56.0f);  // > T_CELL_MAX 55 C
  CHECK(r.ch.state() == ChState::Paused);
  CHECK(r.ch.pause() == PauseReason::CellHot);
  CHECK(!r.ch.wantChg());
  r.hold(5, 3.36f, 0.0f, 50.0f);  // still above resume threshold (45 C)
  CHECK(r.ch.state() == ChState::Paused);
  r.hold(1, 3.36f, 0.0f, 44.0f);
  CHECK(r.ch.state() == ChState::ChargingCc);
  CHECK(r.ch.wantChg());
  r.hold(0.4f, 3.40f, 0.97f, 66.0f);  // > T_CELL_FAULT -> latched
  CHECK(r.ch.state() == ChState::FaultOvertemp);
  CHECK(!r.ch.wantChg());
}

TEST(channel_cold_cell_pauses_charging) {
  ChRig r;
  r.sample(3.30f, 0.0f, 10.0f);
  r.start(Program::Charge);
  r.hold(1, 3.40f, 0.97f, 10.0f);
  r.hold(0.4f, 3.40f, 0.97f, -2.0f);
  CHECK(r.ch.state() == ChState::Paused);
  CHECK(r.ch.pause() == PauseReason::CellCold);
  r.hold(1, 3.33f, 0.0f, 4.0f);
  CHECK(r.ch.state() == ChState::ChargingCc);
}

TEST(channel_board_hot_pauses_discharge_and_iset0_pauses_charge) {
  ChRig r;
  r.sample(3.30f, 0.0f);
  r.start(Program::Discharge);
  r.hold(1, 3.2f, -1.0f);
  r.c.boardHot = true;
  r.tick();
  CHECK(r.ch.state() == ChState::Paused);
  CHECK(r.ch.pause() == PauseReason::BoardHot);
  CHECK(!r.ch.wantDis());
  r.c.boardHot = false;
  r.t += 10;
  r.tick();
  CHECK(r.ch.state() == ChState::Discharging);

  ChRig q;
  q.sample(3.30f, 0.0f);
  q.c.isetEff = 0;
  CHECK(q.start(Program::Charge) != nullptr);  // ISET=0 blocks the charger
  q.c.isetEff = 4;
  CHECK(q.start(Program::Charge) == nullptr);
  q.hold(1, 3.4f, 0.9f);
  q.c.isetEff = 0;
  q.tick();
  CHECK(q.ch.state() == ChState::Paused);
  CHECK(q.ch.pause() == PauseReason::ChargerBlocked);
  q.hold(120, 3.36f, 0.0f);  // no false "charged" while blocked
  CHECK(q.ch.state() == ChState::Paused);
  q.c.isetEff = 4;
  q.tick();
  CHECK(q.ch.state() == ChState::ChargingCc);
}

TEST(channel_current_plausibility_faults) {
  ChRig r;  // charger enabled but no current for 10 s
  r.sample(3.30f, 0.0f);
  r.start(Program::Charge);
  r.hold(12, 3.30f, 0.0f);
  CHECK(r.ch.state() == ChState::FaultCurrent);
  ChRig q;  // discharge current while charging
  q.sample(3.30f, 0.0f);
  q.start(Program::Charge);
  q.t += 200;
  q.sample(3.30f, -0.5f);
  CHECK(q.ch.state() == ChState::FaultCurrent);
  ChRig p;  // charge current far above ISET
  p.sample(3.30f, 0.0f);
  p.start(Program::Charge);
  p.t += 200;
  p.sample(3.30f, 1.9f);
  CHECK(p.ch.state() == ChState::FaultCurrent);
  ChRig d;  // no discharge current at a healthy voltage
  d.sample(3.30f, 0.0f);
  d.start(Program::Discharge);
  d.hold(7, 3.30f, 0.0f);
  CHECK(d.ch.state() == ChState::FaultCurrent);
  ChRig u;  // no discharge current at low voltage = hardware UV backstop tripped
  u.sample(3.00f, 0.0f);
  u.start(Program::Discharge);
  u.hold(7, 2.70f, 0.0f);
  CHECK(u.ch.state() == ChState::Done);
  CHECK(u.ch.result().disTerm == Term::UvBackstop);
}

TEST(channel_stale_measurement_and_sample_error_stop_job) {
  ChRig r;
  r.sample(3.30f, 0.0f);
  r.start(Program::Charge);
  r.hold(1, 3.35f, 0.97f);
  for (int ms = 0; ms < 3200; ms += 10) {
    r.t += 10;
    r.tick();
  }
  CHECK(r.ch.state() == ChState::FaultAdc);
  CHECK(!r.ch.wantChg());
  ChRig q;
  q.sample(3.30f, 0.0f);
  q.start(Program::Discharge);
  q.hold(1, 3.2f, -1.0f);
  q.c.nowMs = q.t;
  q.ch.onSampleError(q.c, "I2C error");
  CHECK(q.ch.state() == ChState::FaultAdc);
  CHECK(!q.ch.wantDis());
  // an idle channel is not latched by a transient error
  ChRig p;
  p.sample(3.30f, 0.0f);
  p.ch.onSampleError(p.c, "I2C error");
  CHECK(p.ch.state() == ChState::Idle);
}

TEST(channel_power_loss_aborts_job_with_safety_fault) {
  ChRig r;
  r.sample(3.30f, 0.0f);
  r.start(Program::Charge);
  r.hold(1, 3.35f, 0.97f);
  r.c.powerOk = false;
  r.tick();
  CHECK(r.ch.state() == ChState::FaultSafety);
  CHECK(!r.ch.wantChg());
}

TEST(channel_cell_removed_during_discharge) {
  ChRig r;
  r.sample(3.30f, 0.0f);
  r.start(Program::Discharge);
  r.hold(1, 3.2f, -1.0f);
  r.t += 200;
  r.sample(0.05f, 0.0f);
  CHECK(r.ch.state() == ChState::Empty);
  CHECK(!r.ch.jobActive());
}

TEST(channel_capacity_test_sequence) {
  ChRig r;
  r.cfg.restAfterChgMin = 1;
  r.cfg.restAfterDisMin = 1;
  r.cfg.restBeforeIrS = 10;
  r.sample(3.30f, 0.0f);
  CHECK(r.start(Program::CapTest) == nullptr);
  CHECK(r.ch.step() == Step::Charge);
  r.hold(3, 3.53f, 0.5f);
  r.hold(62, 3.566f, 0.04f);
  CHECK(r.ch.state() == ChState::Resting);
  CHECK(r.ch.step() == Step::RestAfterCharge);
  CHECK(r.ch.result().hasChg);
  r.hold(61, 3.40f, 0.0f);
  CHECK(r.ch.state() == ChState::Discharging);
  r.hold(36, 3.1f, -1.0f);  // 10 mAh
  r.hold(0.6f, 2.45f, -0.9f);
  CHECK(r.ch.state() == ChState::Resting);
  CHECK(r.ch.step() == Step::RestAfterDischarge);
  CHECK_NEAR(r.ch.result().capMah, 10.0 + 0.6 * 0.9 / 3.6, 0.3);
  r.hold(61, 3.10f, 0.0f);
  CHECK(r.ch.step() == Step::StorageCharge);
  CHECK(r.ch.state() == ChState::ChargingCc);
  CHECK_NEAR(r.ch.storageTargetMah(), r.ch.result().capMah * 0.5, 0.01);
  for (int n = 0; n < 150 && r.ch.state() == ChState::ChargingCc; n++) {  // 1 A until the target
    r.t += 200;
    r.sample(3.30f, 1.0f);
  }
  CHECK(r.ch.state() == ChState::Resting);
  CHECK_NEAR(r.ch.result().storeMah, r.ch.storageTargetMah(), 0.06);
  CHECK(r.ch.step() == Step::RestBeforeIr);
  CHECK(r.ch.result().hasStore);
  r.hold(11, 3.29f, 0.0f);
  CHECK(r.ch.state() == ChState::IrMeasure);
  CHECK(!r.ch.irReady());
  r.hold(5.2f, 3.29f, 0.0f);  // §7.1: rest >= 5 s before the pulse
  CHECK(r.ch.irReady());
  r.ch.irBegin();
  IrResult ir;
  ir.ok = true;
  ir.rOhmic = 0.008f;
  ir.rDc = 0.012f;
  ir.iPulse = -1.05f;
  ir.v0 = 3.29f;
  r.c.nowMs = r.t;
  r.ch.irDone(ir, r.c);
  CHECK(r.ch.state() == ChState::Done);
  CHECK(r.ch.result().hasIr);
  CHECK_NEAR(r.ch.result().irDcMohm, 12.0, 1e-4);
  CHECK_NEAR(r.ch.result().irOhmicMohm, 8.0, 1e-4);
}
