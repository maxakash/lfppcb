// Per-channel state machine and test programs (INTERFACE.md §7).
// Pure C++. The channel decides which power output it *wants*; the
// controller gates the request with the supervisor and enforces the
// CHG/DIS interlock when building the shift-register word.
#pragma once
#include <stdint.h>

#include "adc_conv.h"
#include "coulomb.h"
#include "ir_calc.h"
#include "settings.h"

namespace lfp8 {

enum class ChState : uint8_t {
  Empty,
  Idle,
  Reversed,
  ChargingPre,
  ChargingCc,
  ChargingCv,
  Discharging,
  Resting,
  IrMeasure,
  Paused,
  Done,
  FaultOv,
  FaultDeadCell,
  FaultTimeout,
  FaultOvertemp,
  FaultAdc,
  FaultCurrent,
  FaultSafety,
};
const char *chStateName(ChState s);
bool chStateIsFault(ChState s);
// Number of LED blinks for a fault state (0 if not a fault).
uint8_t chFaultBlinks(ChState s);

enum class Program : uint8_t { None, Charge, Discharge, CapTest, Ir };
const char *programName(Program p);
bool programFromName(const char *s, Program &p);

enum class Step : uint8_t { None, Charge, RestAfterCharge, Discharge, RestAfterDischarge, StorageCharge, RestBeforeIr, Ir };
const char *stepName(Step s);

enum class PauseReason : uint8_t { None, CellHot, CellCold, BoardHot, ChargerBlocked };
const char *pauseName(PauseReason p);

enum class Term : uint8_t { None, ITerm, VMax, Target, Cutoff, UvBackstop, Plateau };
const char *termName(Term t);

struct ChSample {
  uint32_t tMs = 0;
  bool valid = false;
  float v = 0;     // cell voltage (V)
  float i = 0;     // cell current (A), > 0 charging
  float tCell = 0; // C (only meaningful when ntc == Ok)
  NtcStatus ntc = NtcStatus::Open;
};

struct ChannelCtx {
  const Settings *cfg = nullptr;
  uint32_t nowMs = 0;
  uint8_t isetEff = 0;   // effective board ISET (after derating)
  float vin = 5.0f;
  bool powerOk = false;  // supervisor: SAFE_RB high and no board alarm
  bool boardHot = false; // board above T_BOARD_MAX (with hysteresis)
};

struct ChResult {
  bool hasCap = false;
  float capMah = 0, capMwh = 0;   // discharge capacity to V_MIN_DIS
  uint32_t capS = 0;              // discharge duration
  Term disTerm = Term::None;
  bool hasChg = false;
  float chgMah = 0, chgMwh = 0;   // last full-charge input
  Term chgTerm = Term::None;
  bool hasStore = false;
  float storeMah = 0;             // storage-charge input
  bool hasIr = false;
  float irOhmicMohm = 0, irDcMohm = 0, irI = 0, irV0 = 0;
  bool irChargePulse = false;
  uint32_t irAtS = 0;             // uptime (s) of the IR measurement
};

class Channel {
 public:
  void init(uint8_t k);

  // ---- inputs ----
  void onSample(const ChSample &s, const ChannelCtx &c);
  // Measurement failed (I2C error, saturated/implausible reading).
  void onSampleError(const ChannelCtx &c, const char *why);
  // Called every control-loop iteration (timers, pulses, pauses, timeouts).
  void tick(const ChannelCtx &c);

  // ---- commands ---- (return nullptr on success, else a reason)
  const char *start(Program p, const ChannelCtx &c);
  void stop(const ChannelCtx &c, const char *why);
  void reset(const ChannelCtx &c);
  void fault(ChState f, const ChannelCtx &c, const char *why);
  void abortSafety(const ChannelCtx &c, const char *why);
  void setBmWarn(bool w) { bmWarn_ = w; }
  // Measurements stopped (SAFE_EN low): forget the last reading.
  void invalidate() { last_.valid = false; }
  // Restore persisted results after a reboot.
  void restoreResult(const ChResult &r) { res_ = r; }

  // ---- IR engine interface ----
  bool irReady() const { return st_ == ChState::IrMeasure && irSub_ == IrSub::Ready; }
  void irBegin() { irSub_ = IrSub::Running; }
  bool irRunning() const { return st_ == ChState::IrMeasure && irSub_ == IrSub::Running; }
  void irDone(const IrResult &r, const ChannelCtx &c);

  // ---- outputs requested by the state machine ----
  bool wantChg() const { return outChg_; }
  bool wantDis() const { return outDis_; }

  // ---- status ----
  uint8_t k() const { return k_; }
  ChState state() const { return st_; }
  Program program() const { return prog_; }
  Step step() const { return step_; }
  PauseReason pause() const { return pause_; }
  bool jobActive() const { return prog_ != Program::None; }
  bool powerPhase() const;  // a state in which CHG or DIS may be on
  const ChSample &last() const { return last_; }
  bool sampleFresh(uint32_t nowMs) const { return last_.valid && nowMs - lastValidMs_ < 2000; }
  const ChResult &result() const { return res_; }
  const Coulomb &stepCounter() const { return stepCc_; }
  const Coulomb &jobCounter() const { return jobCc_; }
  uint32_t jobElapsedS(uint32_t nowMs) const { return jobActive() ? (nowMs - jobStartMs_) / 1000 : jobLastS_; }
  uint32_t stepElapsedS(uint32_t nowMs) const { return jobActive() ? (nowMs - stepStartMs_) / 1000 : 0; }
  const char *msg() const { return msg_; }
  bool bmWarn() const { return bmWarn_; }
  float storageTargetMah() const { return storeTargetMah_; }
  // Pulse duty in % while in a pulsed charge phase (20 or 50), else 0.
  uint8_t pulseDuty() const;

 private:
  enum class IrSub : uint8_t { WaitRest, Ready, Running };
  void setMsg(const char *m);
  void presence(const ChSample &s);
  void enterCharge(Step st, const ChannelCtx &c);
  void enterPulse(uint8_t duty, uint32_t nowMs);
  uint32_t pulseOnMs() const;
  uint32_t pulseOffMs() const;
  void enterDischarge(const ChannelCtx &c);
  void enterRest(Step st, const ChannelCtx &c);
  void enterIr(const ChannelCtx &c);
  void chargeComplete(Term t, const ChannelCtx &c);
  void dischargeComplete(Term t, const ChannelCtx &c);
  void restComplete(const ChannelCtx &c);
  void finishJob(const ChannelCtx &c, const char *why);
  void pauseFor(PauseReason r, const ChannelCtx &c);
  void resume(const ChannelCtx &c);
  void chargeSample(const ChSample &s, const ChannelCtx &c);
  void dischargeSample(const ChSample &s, const ChannelCtx &c);
  bool temperatureChecks(const ChSample &s, const ChannelCtx &c, bool charging);
  void updateOutputs(uint32_t nowMs);
  bool isCharging(ChState s) const {
    return s == ChState::ChargingPre || s == ChState::ChargingCc || s == ChState::ChargingCv;
  }
  uint32_t restDurationMs(const Settings &cfg) const;

  uint8_t k_ = 1;
  ChState st_ = ChState::Empty;
  ChState resumeSt_ = ChState::Idle;
  Program prog_ = Program::None;
  Step step_ = Step::None;
  PauseReason pause_ = PauseReason::None;
  ChSample last_;
  uint32_t lastValidMs_ = 0;
  uint32_t jobStartMs_ = 0, stepStartMs_ = 0, stepActiveMs_ = 0, lastTickMs_ = 0;
  uint32_t jobLastS_ = 0;
  Coulomb stepCc_, jobCc_;
  ChResult res_;
  // charge
  bool preOn_ = false;
  uint32_t preT0_ = 0, preStartMs_ = 0;
  uint8_t prePulseSamples_ = 0, preNoCurPulses_ = 0;
  bool prePulseCur_ = false;
  uint8_t preDuty_ = 20;        // 20 % (below V_PRECHARGE) or 50 % (below V_CC_MIN)
  uint32_t pulseStartMs_ = 0;
  uint8_t ccLowCount_ = 0;
  // CV plateau detection (one EMA value per minute, 10 min span)
  float cvHist_[11] = {0};
  float cvIema_ = 0;
  uint32_t cvPushMs_ = 0;
  uint8_t cvN_ = 0;
  bool termTiming_ = false;
  uint32_t termT0_ = 0;
  uint8_t vmaxCount_ = 0;
  bool noCurTiming_ = false;
  uint32_t noCurT0_ = 0;
  uint32_t outOnSinceMs_ = 0;
  float storeTargetMah_ = 0;
  // discharge
  uint8_t cutCount_ = 0;
  // OV at rest
  bool ovTiming_ = false;
  uint32_t ovT0_ = 0;
  // NTC present when the job started
  bool ntcAtStart_ = false;
  // IR
  IrSub irSub_ = IrSub::WaitRest;
  uint32_t irQuietSinceMs_ = 0;
  // outputs
  bool outChg_ = false, outDis_ = false;
  bool bmWarn_ = false;
  char msg_[56] = {0};
};

}  // namespace lfp8
