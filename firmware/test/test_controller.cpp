// Controller + supervisor against the simulated board (full measurement chain).
#include <math.h>
#include <string.h>

#include <memory>

#include "rig.h"
#include "test_framework.h"

using namespace lfp8;

namespace {
std::unique_ptr<Rig> newRig() { return std::unique_ptr<Rig>(new Rig); }
void insert(Rig &r, int k, double soc, double qAh = 15.0) {
  SimCell &c = r.sim.cell[k - 1];
  c = SimCell();
  c.present = true;
  c.soc = soc;
  c.qAh = qAh;
}
}  // namespace

TEST(boot_heartbeat_and_safe_enable) {
  auto rp = newRig();
  Rig &r = *rp;
  insert(r, 1, 0.5);
  r.begin();
  CHECK_EQ(r.sim.word, 0);  // all-zero written first
  r.run(1.0);
  CHECK(r.sim.safeEn());
  CHECK(r.ctl.supervisor().armed());
  CHECK(r.ctl.supervisor().powerAllowed());
  CHECK(r.sim.hbToggles > 400);           // >= 100 Hz required; loop runs at ~1 kHz
  CHECK(r.sim.maxToggleGapUs <= 2000);    // never close to the 70 ms trip
  CHECK(r.ctl.supervisor().alarm() == BoardAlarm::None);
  // fan off when idle and cool
  CHECK_EQ(r.sim.fanPct, 0);
}

TEST(scan_rate_at_least_5hz_per_channel) {
  auto rp = newRig();
  Rig &r = *rp;
  for (int k = 1; k <= 8; k++) insert(r, k, 0.5);
  r.begin();
  r.run(2.0);
  static StatusSnapshot s;
  r.ctl.snapshot(s);
  // §5: full scan ~150 ms; coulomb counting needs >= 5 Hz (<= 200 ms)
  CHECK(s.b.scanCycleMs > 100);
  CHECK(s.b.scanCycleMs <= 200);
  printf("    scan cycle %u ms\n", (unsigned)s.b.scanCycleMs);
}

TEST(detects_empty_reversed_and_present_cells) {
  auto rp = newRig();
  Rig &r = *rp;
  insert(r, 1, 0.5);
  insert(r, 3, 0.5);
  r.sim.cell[2].reversed = true;
  insert(r, 4, 0.98);
  r.begin();
  r.run(1.0);
  CHECK(r.st(1) == ChState::Idle);
  CHECK(r.st(2) == ChState::Empty);
  CHECK(r.st(3) == ChState::Reversed);
  CHECK(r.st(4) == ChState::Idle);
  CHECK_NEAR(r.ctl.channel(1).last().v, 3.300, 0.002);
  CHECK_NEAR(r.ctl.channel(3).last().v, -3.300, 0.002);
  CHECK(r.start(3, Program::Charge) != nullptr);
  CHECK(r.start(2, Program::Discharge) != nullptr);
  // removing / inserting
  r.sim.cell[0].present = false;
  r.run(0.5);
  CHECK(r.st(1) == ChState::Empty);
  r.sim.cell[0].present = true;
  r.run(0.5);
  CHECK(r.st(1) == ChState::Idle);
}

TEST(full_charge_cc_cv_termination) {
  auto rp = newRig();
  Rig &r = *rp;
  insert(r, 2, 0.80, 1.0);  // 1 Ah cell for speed
  r.begin();
  r.run(1.0);
  CHECK(r.start(2, Program::Charge) == nullptr);
  r.run(1.0);
  CHECK(r.st(2) == ChState::ChargingCc);
  CHECK(r.sim.chgOn(2));
  double vmax = 0;
  bool sawCv = false;
  bool done = r.runUntil([&] {
    vmax = fmax(vmax, r.sim.cell[1].vTerm());
    if (r.st(2) == ChState::ChargingCv) sawCv = true;
    return r.st(2) == ChState::Done;
  }, 4 * 3600.0);
  CHECK(done);
  CHECK(sawCv);
  CHECK(vmax <= 3.600);  // never above V_MAX_CHG
  const ChResult &res = r.ctl.channel(2).result();
  CHECK(res.chgTerm == Term::ITerm);
  CHECK(r.sim.cell[1].i < 0.05);   // terminated below I_TERM
  CHECK(!r.sim.chgOn(2));
  CHECK_NEAR(res.chgMah, r.sim.cell[1].ahIn * 1000.0, r.sim.cell[1].ahIn * 1000.0 * 0.01);
  CHECK_EQ(r.sim.interlockViolations, 0);
}

TEST(discharge_cutoff_2v5_under_load) {
  auto rp = newRig();
  Rig &r = *rp;
  insert(r, 5, 0.30, 1.0);
  r.begin();
  r.run(1.0);
  CHECK(r.start(5, Program::Discharge) == nullptr);
  double vmin = 9;
  bool done = r.runUntil([&] {
    if (r.sim.disOn(5)) vmin = fmin(vmin, r.sim.cell[4].vTerm());
    return r.st(5) == ChState::Done;
  }, 4 * 3600.0);
  CHECK(done);
  const ChResult &res = r.ctl.channel(5).result();
  CHECK(res.hasCap);
  CHECK(res.disTerm == Term::Cutoff);
  CHECK(vmin > 2.40);     // stopped at ~2.5 V (3 samples debounce)
  CHECK(vmin <= 2.50);
  CHECK(!r.sim.disOn(5));
  CHECK_NEAR(res.capMah, r.sim.cell[4].ahOut * 1000.0, r.sim.cell[4].ahOut * 1000.0 * 0.01);
}

TEST(precharge_pulsing_then_cc_thermal_policy) {
  auto rp = newRig();
  Rig &r = *rp;
  insert(r, 1, 0.0016, 0.5);  // OCV 2.58 V, small cell so recovery is quick
  r.begin();
  r.run(1.0);
  CHECK(r.start(1, Program::Charge) == nullptr);
  CHECK(r.st(1) == ChState::ChargingPre);
  // measure pulse timing at the hardware (CHG_EN as seen by the simulator)
  bool prev = r.sim.chgOn(1);
  uint64_t edge = r.sim.nowUs;
  uint8_t dutyAtEdge = r.ctl.channel(1).pulseDuty();
  int on20 = 0, off20 = 0, on50 = 0, off50 = 0;
  r.runUntil([&] {
    bool now = r.sim.chgOn(1);
    uint8_t duty = r.ctl.channel(1).pulseDuty();
    if (now != prev && r.st(1) == ChState::ChargingPre) {
      double d = (double)(r.sim.nowUs - edge) / 1000.0;
      if (duty == dutyAtEdge) {  // skip the edge where the duty changed
        if (duty == 20) {
          CHECK_NEAR(d, prev ? 1000.0 : 4000.0, 5.0);
          (prev ? on20 : off20)++;
        } else {
          CHECK_NEAR(d, 500.0, 5.0);
          (prev ? on50 : off50)++;
        }
      }
      edge = r.sim.nowUs;
      dutyAtEdge = duty;
    }
    prev = now;
    return r.st(1) != ChState::ChargingPre;
  }, 3600.0);
  CHECK(on20 >= 2 && off20 >= 2);
  CHECK(on50 >= 2 && off50 >= 2);
  CHECK(r.st(1) == ChState::ChargingCc);
  CHECK(r.sim.cell[0].ocv() >= 3.03);
  // §4 thermal policy: never more than one pulse (1 s) of CHG_EN while the
  // cell is below 3.0 V
  CHECK(r.sim.cell[0].maxContOnLowS <= 1.01);
  printf("    %d x 20 %% and %d x 50 %% pulses before CC, longest CHG_EN below 3.0 V: %.2f s\n", on20, on50,
         r.sim.cell[0].maxContOnLowS);
}

TEST(refuses_dead_cell_below_2v0) {
  auto rp = newRig();
  Rig &r = *rp;
  insert(r, 6, -0.012);  // ~1.9 V
  r.begin();
  r.run(1.0);
  CHECK(r.st(6) == ChState::Idle);
  CHECK(r.start(6, Program::Charge) != nullptr);
  CHECK(r.st(6) == ChState::FaultDeadCell);
  r.run(1.0);
  CHECK(!r.sim.chgOn(6));
  CHECK_EQ(r.sim.cell[5].ahIn, 0);
}

TEST(ov_fault_for_overcharged_cell_at_rest) {
  auto rp = newRig();
  Rig &r = *rp;
  insert(r, 7, 1.0085);  // ~3.76 V at rest
  r.begin();
  r.run(1.5);
  CHECK(r.st(7) == ChState::Idle);
  r.run(1.5);
  CHECK(r.st(7) == ChState::FaultOv);
  CHECK(r.start(7, Program::Discharge) != nullptr);  // reset required
}

TEST(cell_temperature_pause_resume_in_loop) {
  auto rp = newRig();
  Rig &r = *rp;
  insert(r, 4, 0.5);
  r.begin();
  r.run(1.0);
  CHECK(r.start(4, Program::Charge) == nullptr);
  r.run(2.0);
  CHECK(r.sim.chgOn(4));
  r.sim.cell[3].tempC = 57.0;
  r.run(1.0);
  CHECK(r.st(4) == ChState::Paused);
  CHECK(!r.sim.chgOn(4));
  r.sim.cell[3].tempC = 47.0;
  r.run(2.0);
  CHECK(r.st(4) == ChState::Paused);
  r.sim.cell[3].tempC = 44.0;
  r.run(1.0);
  CHECK(r.st(4) == ChState::ChargingCc);
  CHECK(r.sim.chgOn(4));
}

TEST(safe_rb_loss_aborts_all_jobs_and_writes_zero) {
  auto rp = newRig();
  Rig &r = *rp;
  insert(r, 1, 0.5);
  insert(r, 2, 0.5);
  r.begin();
  r.run(1.0);
  CHECK(r.start(1, Program::Charge) == nullptr);
  CHECK(r.start(2, Program::Discharge) == nullptr);
  r.run(2.0);
  CHECK(r.sim.chgOn(1) && r.sim.disOn(2));
  r.sim.estop = true;  // E-STOP pressed: hardware drops SAFE_EN
  r.run(0.1);
  CHECK(r.st(1) == ChState::FaultSafety);
  CHECK(r.st(2) == ChState::FaultSafety);
  CHECK(r.ctl.supervisor().alarm() == BoardAlarm::SafeLost);
  CHECK(strstr(r.ctl.supervisor().alarmText(), "E-STOP") != nullptr);
  CHECK_EQ(r.sim.word, 0);  // firmware writes all zeros
  // the supervisor drops the heartbeat: SAFE_EN stays low even after the
  // E-STOP is released, until a human acknowledges
  uint64_t toggles = r.sim.hbToggles;
  r.sim.estop = false;
  r.run(1.0);
  CHECK_EQ(r.sim.hbToggles, toggles);
  CHECK(!r.sim.safeEn());
  CHECK(!r.ctl.supervisor().powerAllowed());
  CHECK_EQ(r.sim.word, 0);
  CHECK(r.cmd(Command::Type::Reset, 1) == nullptr);
  CHECK(r.start(1, Program::Charge) != nullptr);  // still refused (alarm)
  CHECK(r.cmd(Command::Type::Ack) == nullptr);
  r.run(0.5);
  CHECK(r.sim.hbToggles > toggles + 200);
  CHECK(r.sim.safeEn());
  CHECK(r.ctl.supervisor().powerAllowed());
  CHECK(r.start(1, Program::Charge) == nullptr);
  // E-STOP still pressed when acknowledging: heartbeat restarts, SAFE stays
  // low, power stays disabled, no new alarm loop
  r.run(1.0);
  r.sim.estop = true;
  r.run(0.2);
  CHECK(r.ctl.supervisor().alarm() == BoardAlarm::SafeLost);
  CHECK(r.cmd(Command::Type::Ack) == nullptr);
  r.run(1.0);
  CHECK(!r.sim.safeEn());
  CHECK(!r.ctl.supervisor().powerAllowed());
  CHECK(r.start(2, Program::Charge) != nullptr);
}

TEST(stalled_loop_trips_hardware_watchdog_and_is_reported) {
  auto rp = newRig();
  Rig &r = *rp;
  insert(r, 3, 0.5);
  r.begin();
  r.run(1.0);
  CHECK(r.start(3, Program::Discharge) == nullptr);
  r.run(1.0);
  // the control loop hangs for 100 ms: no heartbeat edges
  r.sim.advance(100000);
  CHECK(!r.sim.safeEn());   // hardware isolated the cells on its own
  CHECK(!r.sim.disOn(3));
  r.run(0.1);
  CHECK(r.st(3) == ChState::FaultSafety);
  CHECK(r.ctl.supervisor().alarm() == BoardAlarm::SafeLost);
  CHECK(strstr(r.ctl.supervisor().alarmText(), "heartbeat") != nullptr);
  CHECK(!r.ctl.supervisor().heartbeatAllowed());  // latched off until ack
  CHECK(r.cmd(Command::Type::Ack) == nullptr);
  r.run(0.5);
  CHECK(r.sim.safeEn());
}

TEST(adc_crosscheck_mismatch_stops_heartbeat) {
  auto rp = newRig();
  Rig &r = *rp;
  insert(r, 2, 0.5);
  r.begin();
  r.sim.vchkErrV[1] = 0.030;  // within +-60 mV: accepted
  r.run(10.0);
  CHECK(r.ctl.supervisor().alarm() == BoardAlarm::None);
  CHECK(r.start(2, Program::Charge) == nullptr);
  r.sim.vchkErrV[1] = 0.150;  // ESP32 IO1 and ADS1115 disagree
  uint64_t t0 = r.sim.nowUs;
  bool tripped = r.runUntil([&] { return r.ctl.supervisor().alarm() != BoardAlarm::None; }, 60.0);
  CHECK(tripped);
  printf("    mismatch detected after %.1f s\n", (double)(r.sim.nowUs - t0) / 1e6);
  CHECK(r.ctl.supervisor().alarm() == BoardAlarm::AdcMismatch);
  CHECK(r.st(2) == ChState::FaultAdc);
  uint64_t toggles = r.sim.hbToggles;
  r.run(0.5);
  CHECK_EQ(r.sim.hbToggles, toggles);  // heartbeat stopped
  CHECK(!r.sim.safeEn());              // -> hardware isolates every cell
  CHECK_EQ(r.sim.word, 0);
  // fix the cause and acknowledge: heartbeat resumes
  r.sim.vchkErrV[1] = 0.0;
  CHECK(r.cmd(Command::Type::Ack) == nullptr);
  r.run(1.0);
  CHECK(r.sim.hbToggles > toggles + 400);
  CHECK(r.sim.safeEn());
}

TEST(stray_current_with_outputs_off_stops_heartbeat) {
  auto rp = newRig();
  Rig &r = *rp;
  insert(r, 8, 0.5);
  r.begin();
  r.run(1.0);
  r.sim.strayA[7] = -0.4;  // e.g. a shorted load MOSFET
  bool tripped = r.runUntil([&] { return r.ctl.supervisor().alarm() == BoardAlarm::StrayCurrent; }, 5.0);
  CHECK(tripped);
  uint64_t toggles = r.sim.hbToggles;
  r.run(0.3);
  CHECK_EQ(r.sim.hbToggles, toggles);
  CHECK(!r.sim.safeEn());
  CHECK_NEAR(r.sim.cell[7].i, 0.0, 1e-12);  // isolated by the hardware
}

TEST(i2c_errors_fault_channel_then_board) {
  auto rp = newRig();
  Rig &r = *rp;
  insert(r, 1, 0.5);
  insert(r, 2, 0.5);
  r.begin();
  r.run(1.0);
  CHECK(r.start(1, Program::Charge) == nullptr);
  CHECK(r.start(2, Program::Charge) == nullptr);
  r.run(1.0);
  // one failing transaction: the channel being measured stops (FAULT_ADC)
  r.runUntil([&] { return r.ctl.channel(1).last().valid && r.sim.muxCh() == 1; }, 1.0);
  r.sim.i2cFailNext = 1;
  r.run(0.5);
  int faulted = (r.st(1) == ChState::FaultAdc) + (r.st(2) == ChState::FaultAdc);
  CHECK_EQ(faulted, 1);
  CHECK(r.ctl.supervisor().alarm() == BoardAlarm::None);
  // persistent failure: board alarm and heartbeat stop
  r.sim.i2cFailAll = true;
  r.run(1.0);
  CHECK(r.ctl.supervisor().alarm() == BoardAlarm::I2c);
  CHECK(!r.ctl.supervisor().heartbeatAllowed());
  CHECK(!r.sim.safeEn());
  CHECK(!r.ctl.channel(1).jobActive() && !r.ctl.channel(2).jobActive());
}

TEST(missing_ads1115_never_enables_power) {
  auto rp = newRig();
  Rig &r = *rp;
  insert(r, 1, 0.5);
  r.sim.adsPresent = false;
  r.begin();
  r.run(1.0);
  CHECK(r.ctl.supervisor().alarm() == BoardAlarm::AdsMissing);
  CHECK_EQ(r.sim.hbToggles, 0);
  CHECK(!r.sim.safeEn());
  CHECK(r.cmd(Command::Type::Ack) != nullptr);  // still missing
  r.sim.adsPresent = true;
  CHECK(r.cmd(Command::Type::Ack) == nullptr);
  r.run(1.0);
  CHECK(r.sim.safeEn());
  CHECK(r.st(1) == ChState::Idle);
}

TEST(vin_out_of_window_aborts_jobs) {
  auto rp = newRig();
  Rig &r = *rp;
  insert(r, 1, 0.5);
  r.begin();
  r.run(3.0);
  CHECK(r.start(1, Program::Charge) == nullptr);
  r.sim.vin = 4.70;  // below the firmware window (4.80) but above the hardware UV (4.48)
  r.run(2.0);
  CHECK(r.ctl.supervisor().alarm() == BoardAlarm::Vin);
  CHECK(r.st(1) == ChState::FaultSafety);
  CHECK(r.sim.safeEn());  // heartbeat continues
  CHECK(!r.sim.chgOn(1));
  CHECK(r.cmd(Command::Type::Ack) != nullptr);  // still out of range
  r.sim.vin = 5.0;
  r.run(1.0);
  CHECK(r.cmd(Command::Type::Ack) == nullptr);
}

TEST(board_temperature_fan_iset_derate_and_discharge_pause) {
  auto rp = newRig();
  Rig &r = *rp;
  insert(r, 1, 0.5);
  insert(r, 2, 0.5);
  r.cfg.iset = 7;
  r.begin();
  r.run(1.0);
  CHECK(r.start(1, Program::Charge) == nullptr);
  r.run(1.0);
  CHECK_EQ((r.sim.word >> 27) & 7u, 7);
  CHECK_EQ(r.sim.fanPct, 40);  // a channel is active
  r.sim.tBoard = 52;           // ISET >= 6 only below 50 C
  r.run(2.0);
  CHECK_EQ((r.sim.word >> 27) & 7u, 5);
  CHECK_EQ(r.sim.fanPct, 100);  // above T_BOARD_FAN
  CHECK(r.start(2, Program::Discharge) == nullptr);
  r.run(1.0);
  CHECK(r.sim.disOn(2));
  r.sim.tBoard = 72;  // > T_BOARD_MAX: discharges paused, no new ones
  r.run(2.0);
  CHECK(r.st(2) == ChState::Paused);
  CHECK(!r.sim.disOn(2));
  CHECK(r.sim.chgOn(1));
  r.sim.tBoard = 60;
  r.run(2.0);
  CHECK(r.st(2) == ChState::Discharging);
  r.sim.tBoard = 78;  // > T_BOARD_MAX + 7: abort everything
  r.run(2.0);
  CHECK(r.ctl.supervisor().alarm() == BoardAlarm::BoardOvertemp);
  CHECK(!r.ctl.channel(1).jobActive() && !r.ctl.channel(2).jobActive());
}

TEST(calibration_commands) {
  auto rp = newRig();
  Rig &r = *rp;
  insert(r, 1, 0.5);
  r.begin();
  r.run(1.0);
  double v = r.ctl.channel(1).last().v;
  CHECK(r.cmd(Command::Type::CalV, 1, Program::None, v * 1.01) == nullptr);
  CHECK_NEAR(r.ctl.settings().calV[0], 1.01, 1e-4);
  CHECK(r.ctl.settingsDirty());
  r.run(0.5);
  CHECK_NEAR(r.ctl.channel(1).last().v, v * 1.01, 0.001);
  CHECK(r.cmd(Command::Type::CalV, 1, Program::None, v * 1.5) != nullptr);  // out of range
  CHECK(r.cmd(Command::Type::CalI, 1, Program::None, 1.0) != nullptr);      // no current
  CHECK(r.cmd(Command::Type::Iset, 0, Program::None, 9) != nullptr);
  CHECK(r.cmd(Command::Type::Iset, 0, Program::None, 3) == nullptr);
  CHECK_EQ(r.ctl.settings().iset, 3);
  CHECK(r.cmd(Command::Type::CalTboard, 0, Program::None, 30.0) == nullptr);
  r.run(1.0);
  static StatusSnapshot s;
  r.ctl.snapshot(s);
  CHECK_NEAR(s.b.tBoard, 30.0, 0.2);
}

TEST(maintenance_mode_stops_everything) {
  auto rp = newRig();
  Rig &r = *rp;
  insert(r, 1, 0.5);
  r.begin();
  r.run(1.0);
  CHECK(r.start(1, Program::Charge) == nullptr);
  r.run(1.0);
  r.ctl.setMaintenance(true);
  r.run(0.5);
  CHECK(!r.ctl.channel(1).jobActive());
  CHECK_EQ(r.sim.word, 0);
  CHECK(!r.sim.safeEn());
  CHECK(r.ctl.supervisor().alarm() == BoardAlarm::None);  // expected, not an alarm
  r.ctl.setMaintenance(false);
  r.run(1.0);
  CHECK(r.sim.safeEn());
  CHECK(r.ctl.supervisor().powerAllowed());
}

TEST(heartbeat_timing_and_boot_vref_delay) {
  auto rp = newRig();
  Rig &r = *rp;
  insert(r, 2, 0.5);
  r.begin();
  r.run(0.04);  // §6: SAFE_EN stays low ~50 ms after power-up (VREF) - not a fault
  CHECK(!r.sim.safeEn());
  CHECK(r.ctl.supervisor().alarm() == BoardAlarm::None);
  r.run(1.0);
  CHECK(r.sim.safeEn());
  CHECK(r.ctl.supervisor().alarm() == BoardAlarm::None);
  CHECK(r.sim.maxToggleGapUs <= 5000);  // >= 100 Hz square wave
  CHECK(r.start(2, Program::Discharge) == nullptr);
  r.run(1.0);
  r.sim.advance(15000);  // a 15 ms hiccup: below the 23 ms hardware trip
  r.run(0.5);
  CHECK(r.sim.safeEn());
  CHECK(r.st(2) == ChState::Discharging);
  r.sim.advance(40000);  // 40 ms: hardware trips, firmware reports it
  r.run(0.2);
  CHECK(r.ctl.supervisor().alarm() == BoardAlarm::SafeLost);
  CHECK(strstr(r.ctl.supervisor().alarmText(), "heartbeat") != nullptr);
  CHECK(r.st(2) == ChState::FaultSafety);
}

TEST(ov_latch_release_by_heartbeat_pause) {
  auto rp = newRig();
  Rig &r = *rp;
  insert(r, 3, 1.0085);  // ~3.76 V
  insert(r, 5, 0.5);
  r.begin();
  r.run(0.5);
  r.sim.cell[2].ovLatched = true;  // OV latch tripped in hardware (SAFE_EN high)
  r.run(3.0);
  CHECK(r.st(3) == ChState::FaultOv);
  CHECK(r.sim.cell[2].ovLatched);      // board was powered: SAFE high, latch held
  // another job is running: the release pulse waits for an idle board
  CHECK(r.start(5, Program::Discharge) == nullptr);
  r.run(1.0);
  CHECK(r.cmd(Command::Type::Reset, 3) == nullptr);
  r.run(1.0);
  CHECK(r.st(5) == ChState::Discharging);  // not disturbed
  CHECK(r.sim.cell[2].ovLatched);
  CHECK(r.start(3, Program::Discharge) != nullptr);  // release pending
  static StatusSnapshot snap;
  r.ctl.snapshot(snap);
  CHECK_EQ(snap.b.latchReset, 1);
  CHECK(snap.ch[2].ovPending);
  // board idle -> heartbeat paused >= 100 ms -> SAFE_EN low -> latch released
  uint64_t gap0 = r.sim.maxToggleGapUs;
  CHECK(r.cmd(Command::Type::Stop, 5) == nullptr);
  r.run(1.0);
  CHECK(r.sim.maxToggleGapUs >= 100000);
  CHECK(gap0 < 100000);
  CHECK(!r.sim.cell[2].ovLatched);
  CHECK(r.sim.safeEn());
  CHECK(r.ctl.supervisor().alarm() == BoardAlarm::None);  // planned, no alarm
  r.ctl.snapshot(snap);
  CHECK_EQ(snap.b.latchReset, 0);
  CHECK(!snap.ch[2].ovPending);
  // the cell is still above V_ABS_MAX -> the OV fault comes back (correct)
  r.run(3.0);
  CHECK(r.st(3) == ChState::FaultOv);
}

TEST(contact_resistance_hint_and_warning) {
  auto rp = newRig();
  Rig &r = *rp;
  insert(r, 1, 0.5);
  insert(r, 2, 0.5);
  r.sim.cell[0].contactR = 0.030;
  r.sim.cell[1].contactR = 0.40;  // bad contact / thin B- wire
  r.begin();
  r.run(1.0);
  CHECK(r.start(1, Program::Discharge) == nullptr);
  CHECK(r.start(2, Program::Discharge) == nullptr);
  r.run(25.0);  // several diagnostic rounds
  static StatusSnapshot s;
  r.ctl.snapshot(s);
  CHECK_NEAR(s.ch[0].contactMohm, 30.0, 8.0);
  CHECK(!s.ch[0].contactWarn);
  CHECK_NEAR(s.ch[1].contactMohm, 400.0, 20.0);
  CHECK(s.ch[1].contactWarn);
  CHECK(r.st(2) == ChState::Discharging);  // warning only
  printf("    contact: ch1 %.0f mOhm, ch2 %.0f mOhm (CONTACT_WARN)\n", s.ch[0].contactMohm, s.ch[1].contactMohm);
}

TEST(low_hardware_cv_terminates_by_current) {
  // §4: hardware CV can be 3.51 V; the charge must still end (I_TERM or plateau)
  auto rp = newRig();
  Rig &r = *rp;
  insert(r, 4, 0.90, 1.0);
  r.sim.cell[3].cvSet = 3.510;
  r.begin();
  r.run(1.0);
  CHECK(r.start(4, Program::Charge) == nullptr);
  double vmax = 0;
  bool done = r.runUntil([&] {
    vmax = fmax(vmax, r.sim.cell[3].vTerm());
    return r.st(4) == ChState::Done;
  }, 6 * 3600.0);
  CHECK(done);
  CHECK(vmax < 3.52);
  Term t = r.ctl.channel(4).result().chgTerm;
  CHECK(t == Term::ITerm || t == Term::Plateau);
}

TEST(crosscheck_dwell_no_false_alarm_with_rc_filter) {
  // IO1 has a 1M/1M + 10 nF divider (tau 5 ms). With empty, reversed and full
  // slots next to each other, reading too early would differ by > 60 mV.
  auto rp = newRig();
  Rig &r = *rp;
  insert(r, 1, 1.002);  // ~3.56 V
  insert(r, 3, 0.5);
  insert(r, 4, 0.5);
  r.sim.cell[3].reversed = true;
  insert(r, 6, 0.98);
  insert(r, 8, 0.004);  // ~2.7 V
  r.begin();
  r.run(120.0);  // 120 s: every channel cross-checked many times
  CHECK(r.ctl.supervisor().alarm() == BoardAlarm::None);
  static StatusSnapshot s;
  r.ctl.snapshot(s);
  CHECK_NEAR(s.ch[0].vchk, r.sim.cell[0].vTerm(), 0.03);
  CHECK_NEAR(s.ch[7].vchk, r.sim.cell[7].vTerm(), 0.03);
  CHECK(s.b.scanCycleMs <= 200);  // still >= 5 Hz per channel with the dwell
}

TEST(passive_ceiling_is_not_a_fault) {
  // ISET 7 (1.86 A set-point) on a 5.00 V rail: the ceiling limits the
  // current to ~0.8 A at 3.4 V; that must not look like a fault.
  auto rp = newRig();
  Rig &r = *rp;
  r.cfg.iset = 7;
  r.sim.vin = 5.00;
  insert(r, 2, 0.75, 20.0);
  r.begin();
  r.run(1.0);
  CHECK(r.start(2, Program::Charge) == nullptr);
  r.run(120.0);
  CHECK(r.st(2) == ChState::ChargingCc);
  CHECK(r.sim.cell[1].i > 0.6 && r.sim.cell[1].i < 1.0);
}

TEST(sim_watchdog_rejects_50hz_heartbeat) {
  // Self-test of the board model used by all tests: <= 50 Hz is rejected,
  // >= 100 Hz accepted (INTERFACE.md §6).
  SimBoard sim;
  bool lvl = false;
  for (int k = 0; k < 200; k++) {  // 100 Hz: edge every 5 ms
    sim.advance(5000);
    sim.setHeartbeat(lvl = !lvl);
  }
  CHECK(sim.safeEn());
  for (int k = 0; k < 200; k++) {  // 50 Hz: edge every 10 ms
    sim.advance(10000);
    sim.setHeartbeat(lvl = !lvl);
  }
  CHECK(!sim.safeEn());
}
