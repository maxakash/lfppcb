#include "channel.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "lfp8_config.h"

namespace lfp8 {

namespace {
constexpr float kCvDetectV = 3.50f;      // V >= this in CC -> report CV
constexpr float kCvTaperMinV = 3.40f;    // taper-based CV detection only above this
constexpr float kCvTaperFrac = 0.85f;    // I < 85 % of the expected CC current -> CV
constexpr float kExpectMinA = 0.15f;     // expected current below this: no "no current" check
constexpr float kTermMinV = 3.45f;       // I_TERM termination only valid above this
constexpr float kNoCurrentA = 0.03f;     // charger on but less than this -> no current
constexpr uint32_t kNoChgCurrentMs = 10000;
constexpr uint32_t kNoDisCurrentMs = 5000;
constexpr float kDisNoCurrentA = -0.05f; // discharge current above this -> none
constexpr float kUvBackstopMaxV = 2.80f; // no discharge current below this = backstop tripped
constexpr float kWrongDirA = 0.10f;      // current in the wrong direction
constexpr float kDisMaxA = 1.6f;         // 2.75 ohm load can not draw more
// §4/§7 thermal policy: 20 % duty (1 s on / 4 s off) below V_PRECHARGE,
// 50 % duty (0.5 s on / 0.5 s off) between V_PRECHARGE and V_CC_MIN (3.05 V).
constexpr uint32_t kPre20DecideMs = 3000; // decide on the rest voltage >= 3 s into the off phase
constexpr uint32_t kPre50DecideMs = 300;  // ... >= 0.3 s into the 0.5 s off phase
constexpr uint32_t kPre20MaxMs = 4u * 3600u * 1000u;   // 20 % phase must recover the cell in 4 h
constexpr uint32_t kPulseMaxMs = 8u * 3600u * 1000u;   // whole pulsed phase
constexpr float kCcBackHyst = 0.08f;      // in CC, V under charge < V_CC_MIN - this -> pulse again
// End of charge if the CV current stops falling (low hardware CV, 3.51 V):
constexpr uint32_t kPlateauStepMs = 60000;
constexpr int kPlateauSpan = 10;          // minutes compared
constexpr float kPlateauMaxA = 0.20f;
constexpr float kPlateauMinDropA = 0.005f;
constexpr float kPlateauMinDropFrac = 0.03f;
constexpr uint32_t kOvRestMs = 2000;     // §7: > V_ABS_MAX for 2 s with CHG_EN=0
constexpr uint8_t kVmaxCount = 3;
constexpr uint8_t kCutCount = 3;
constexpr float kCutHardMargin = 0.15f;  // V below V_MIN_DIS -> immediate cut-off
constexpr uint32_t kMinRestMs = 2000;    // minimum dead time between power phases
}  // namespace

// ---------------------------------------------------------------- names ----
const char *chStateName(ChState s) {
  switch (s) {
    case ChState::Empty: return "EMPTY";
    case ChState::Idle: return "IDLE";
    case ChState::Reversed: return "REVERSED";
    case ChState::ChargingPre: return "CHARGING_PRE";
    case ChState::ChargingCc: return "CHARGING_CC";
    case ChState::ChargingCv: return "CHARGING_CV";
    case ChState::Discharging: return "DISCHARGING";
    case ChState::Resting: return "RESTING";
    case ChState::IrMeasure: return "IR_MEASURE";
    case ChState::Paused: return "PAUSED";
    case ChState::Done: return "DONE";
    case ChState::FaultOv: return "FAULT_OV";
    case ChState::FaultDeadCell: return "FAULT_DEAD_CELL";
    case ChState::FaultTimeout: return "FAULT_TIMEOUT";
    case ChState::FaultOvertemp: return "FAULT_OVERTEMP";
    case ChState::FaultAdc: return "FAULT_ADC";
    case ChState::FaultCurrent: return "FAULT_CURRENT";
    case ChState::FaultSafety: return "FAULT_SAFETY";
  }
  return "?";
}

bool chStateIsFault(ChState s) { return s >= ChState::FaultOv; }

uint8_t chFaultBlinks(ChState s) {
  switch (s) {
    case ChState::FaultOv: return 2;
    case ChState::FaultDeadCell: return 3;
    case ChState::FaultTimeout: return 4;
    case ChState::FaultOvertemp: return 5;
    case ChState::FaultAdc: return 6;
    case ChState::FaultCurrent: return 7;
    case ChState::FaultSafety: return 8;
    default: return 0;
  }
}

const char *programName(Program p) {
  switch (p) {
    case Program::Charge: return "charge";
    case Program::Discharge: return "discharge";
    case Program::CapTest: return "captest";
    case Program::Ir: return "ir";
    default: return "none";
  }
}

bool programFromName(const char *s, Program &p) {
  if (!strcmp(s, "charge")) p = Program::Charge;
  else if (!strcmp(s, "discharge")) p = Program::Discharge;
  else if (!strcmp(s, "captest")) p = Program::CapTest;
  else if (!strcmp(s, "ir")) p = Program::Ir;
  else return false;
  return true;
}

const char *stepName(Step s) {
  switch (s) {
    case Step::Charge: return "charge";
    case Step::RestAfterCharge: return "rest_chg";
    case Step::Discharge: return "discharge";
    case Step::RestAfterDischarge: return "rest_dis";
    case Step::StorageCharge: return "storage";
    case Step::RestBeforeIr: return "rest_ir";
    case Step::Ir: return "ir";
    default: return "none";
  }
}

const char *pauseName(PauseReason p) {
  switch (p) {
    case PauseReason::CellHot: return "cell_hot";
    case PauseReason::CellCold: return "cell_cold";
    case PauseReason::BoardHot: return "board_hot";
    case PauseReason::ChargerBlocked: return "iset0";
    default: return "";
  }
}

const char *termName(Term t) {
  switch (t) {
    case Term::ITerm: return "i_term";
    case Term::VMax: return "v_max";
    case Term::Target: return "target";
    case Term::Cutoff: return "cutoff";
    case Term::UvBackstop: return "uv_backstop";
    case Term::Plateau: return "plateau";
    default: return "";
  }
}

// --------------------------------------------------------------- basics ----
void Channel::init(uint8_t k) {
  *this = Channel();
  k_ = k;
}

void Channel::setMsg(const char *m) {
  strncpy(msg_, m ? m : "", sizeof(msg_) - 1);
  msg_[sizeof(msg_) - 1] = 0;
}

bool Channel::powerPhase() const {
  return st_ == ChState::ChargingPre || st_ == ChState::ChargingCc || st_ == ChState::ChargingCv ||
         st_ == ChState::Discharging;
}

void Channel::updateOutputs(uint32_t nowMs) {
  bool c = (st_ == ChState::ChargingPre && preOn_) || st_ == ChState::ChargingCc || st_ == ChState::ChargingCv;
  bool d = st_ == ChState::Discharging;
  if ((c && !outChg_) || (d && !outDis_)) outOnSinceMs_ = nowMs;
  outChg_ = c;
  outDis_ = d && !c;  // never both
}

void Channel::enterPulse(uint8_t duty, uint32_t nowMs) {
  if (duty == 20 && preDuty_ != 20) preStartMs_ = nowMs;
  st_ = ChState::ChargingPre;
  preDuty_ = duty;
  preOn_ = true;
  preT0_ = nowMs;
  prePulseSamples_ = 0;
  prePulseCur_ = false;
  noCurTiming_ = false;
}

uint32_t Channel::pulseOnMs() const { return preDuty_ == 50 ? kPulse50PeriodMs / 2 : kPre20OnMs; }
uint32_t Channel::pulseOffMs() const { return preDuty_ == 50 ? kPulse50PeriodMs / 2 : kPre20OffMs; }

uint8_t Channel::pulseDuty() const {
  bool pre = st_ == ChState::ChargingPre || (st_ == ChState::Paused && resumeSt_ == ChState::ChargingPre);
  return pre ? preDuty_ : 0;
}

void Channel::presence(const ChSample &s) {
  if (s.v < kVReversed) {
    st_ = ChState::Reversed;
  } else if (fabsf(s.v) < kVPresentMin && fabsf(s.i) < kIEmptyMax) {
    st_ = ChState::Empty;
  } else if (st_ == ChState::Empty || st_ == ChState::Reversed) {
    st_ = ChState::Idle;  // cell inserted
    setMsg("");
  }
}

// ------------------------------------------------------------ commands ----
const char *Channel::start(Program p, const ChannelCtx &c) {
  const Settings &cfg = *c.cfg;
  if (p == Program::None) return "unknown program";
  if (jobActive()) return "channel busy";
  if (chStateIsFault(st_)) return "channel in fault - reset first";
  if (st_ == ChState::Empty) return "no cell";
  if (st_ == ChState::Reversed) return "cell reversed";
  if (st_ != ChState::Idle && st_ != ChState::Done) return "channel not idle";
  if (!c.powerOk) return "power stages not enabled (SAFE/alarm)";
  if (!sampleFresh(c.nowMs)) return "no fresh measurement";
  const ChSample &s = last_;
  if (s.ntc == NtcStatus::Short || s.ntc == NtcStatus::Invalid) return "NTC fault";
  if (s.ntc == NtcStatus::Ok && s.tCell >= cfg.tCellMax) return "cell too hot";
  if (s.v > cfg.vAbsMax) return "cell above V_ABS_MAX";
  bool needsCharge = (p == Program::Charge || p == Program::CapTest);
  if (needsCharge) {
    if (c.isetEff == 0) return "ISET=0 (charger blocked)";
    if (s.v < cfg.vDead) {
      // §7: below V_DEAD refuse to charge -> DEAD_CELL
      jobLastS_ = 0;
      fault(ChState::FaultDeadCell, c, "V < V_DEAD: refusing to charge");
      return "dead cell (V < V_DEAD) - refusing to charge";
    }
  }
  if (p == Program::Discharge) {
    if (c.boardHot) return "board too hot - discharge derated";
    if (s.v <= cfg.vMinDis + 0.05f || s.v < kDisStartMinV) return "cell too low to discharge (load starts >= 2.59 V)";
  }
  if (p == Program::Ir && s.v < cfg.vDead) return "cell below V_DEAD";

  // Accept.
  prog_ = p;
  jobStartMs_ = c.nowMs;
  jobCc_.reset();
  res_ = ChResult();
  ntcAtStart_ = (s.ntc == NtcStatus::Ok);
  ovTiming_ = false;
  setMsg("");
  switch (p) {
    case Program::Charge:
    case Program::CapTest: enterCharge(Step::Charge, c); break;
    case Program::Discharge: enterDischarge(c); break;
    case Program::Ir: enterIr(c); break;
    default: break;
  }
  updateOutputs(c.nowMs);
  return nullptr;
}

void Channel::stop(const ChannelCtx &c, const char *why) {
  if (!jobActive()) {
    if (st_ == ChState::Done) st_ = ChState::Idle;
    return;
  }
  jobLastS_ = (c.nowMs - jobStartMs_) / 1000;
  prog_ = Program::None;
  step_ = Step::None;
  pause_ = PauseReason::None;
  st_ = ChState::Idle;
  setMsg(why ? why : "stopped");
  updateOutputs(c.nowMs);
}

void Channel::reset(const ChannelCtx &c) {
  prog_ = Program::None;
  step_ = Step::None;
  pause_ = PauseReason::None;
  res_ = ChResult();
  jobLastS_ = 0;
  ovTiming_ = false;
  bmWarn_ = false;
  setMsg("");
  st_ = ChState::Idle;
  if (last_.valid) presence(last_);
  updateOutputs(c.nowMs);
}

void Channel::fault(ChState f, const ChannelCtx &c, const char *why) {
  if (jobActive()) jobLastS_ = (c.nowMs - jobStartMs_) / 1000;
  prog_ = Program::None;
  step_ = Step::None;
  pause_ = PauseReason::None;
  st_ = f;
  setMsg(why);
  updateOutputs(c.nowMs);  // all off
}

void Channel::abortSafety(const ChannelCtx &c, const char *why) {
  if (jobActive()) fault(ChState::FaultSafety, c, why);
}

// --------------------------------------------------------------- steps ----
void Channel::enterCharge(Step st, const ChannelCtx &c) {
  const Settings &cfg = *c.cfg;
  step_ = st;
  stepStartMs_ = c.nowMs;
  stepActiveMs_ = 0;
  lastTickMs_ = c.nowMs;
  stepCc_.reset();
  termTiming_ = false;
  vmaxCount_ = 0;
  noCurTiming_ = false;
  pause_ = PauseReason::None;
  float v = last_.v;  // rest voltage (outputs are off when a charge step starts)
  if (v < cfg.vDead) {
    fault(ChState::FaultDeadCell, c, "V < V_DEAD: refusing to charge");
    return;
  }
  pulseStartMs_ = c.nowMs;
  preStartMs_ = c.nowMs;
  preNoCurPulses_ = 0;
  if (v < cfg.vPrecharge) {
    enterPulse(20, c.nowMs);  // 2.00..2.80 V: 20 % duty precharge
  } else if (v < cfg.vCcMin) {
    enterPulse(50, c.nowMs);  // 2.80..3.05 V: 50 % duty (SOT-23 pass FET)
  } else {
    st_ = ChState::ChargingCc;
  }
  ccLowCount_ = 0;
  cvN_ = 0;
  if (c.isetEff == 0) pauseFor(PauseReason::ChargerBlocked, c);
}

void Channel::enterDischarge(const ChannelCtx &c) {
  step_ = Step::Discharge;
  stepStartMs_ = c.nowMs;
  stepActiveMs_ = 0;
  lastTickMs_ = c.nowMs;
  stepCc_.reset();
  cutCount_ = 0;
  noCurTiming_ = false;
  pause_ = PauseReason::None;
  st_ = ChState::Discharging;
  if (c.boardHot) pauseFor(PauseReason::BoardHot, c);
}

void Channel::enterRest(Step st, const ChannelCtx &c) {
  step_ = st;
  stepStartMs_ = c.nowMs;
  stepActiveMs_ = 0;
  stepCc_.reset();
  pause_ = PauseReason::None;
  st_ = ChState::Resting;
}

void Channel::enterIr(const ChannelCtx &c) {
  step_ = Step::Ir;
  stepStartMs_ = c.nowMs;
  stepCc_.reset();
  pause_ = PauseReason::None;
  st_ = ChState::IrMeasure;
  irSub_ = IrSub::WaitRest;
  irQuietSinceMs_ = c.nowMs;
}

uint32_t Channel::restDurationMs(const Settings &cfg) const {
  uint32_t ms = 0;
  switch (step_) {
    case Step::RestAfterCharge: ms = (uint32_t)cfg.restAfterChgMin * 60000u; break;
    case Step::RestAfterDischarge: ms = (uint32_t)cfg.restAfterDisMin * 60000u; break;
    case Step::RestBeforeIr: ms = (uint32_t)cfg.restBeforeIrS * 1000u; break;
    default: break;
  }
  // Always leave a dead time between CHG and DIS phases, even if a rest is 0.
  return ms < kMinRestMs ? kMinRestMs : ms;
}

void Channel::finishJob(const ChannelCtx &c, const char *why) {
  jobLastS_ = (c.nowMs - jobStartMs_) / 1000;
  prog_ = Program::None;
  step_ = Step::None;
  pause_ = PauseReason::None;
  st_ = ChState::Done;
  setMsg(why);
  updateOutputs(c.nowMs);
}

void Channel::chargeComplete(Term t, const ChannelCtx &c) {
  if (step_ == Step::Charge) {
    res_.hasChg = true;
    res_.chgMah = (float)stepCc_.chgMah;
    res_.chgMwh = (float)stepCc_.chgMwh;
    res_.chgTerm = t;
  } else if (step_ == Step::StorageCharge) {
    res_.hasStore = true;
    res_.storeMah = (float)stepCc_.chgMah;
  }
  st_ = ChState::Idle;  // outputs off before the next step
  updateOutputs(c.nowMs);
  if (prog_ == Program::Charge) {
    finishJob(c, "charged");
  } else if (prog_ == Program::CapTest) {
    if (step_ == Step::Charge) {
      enterRest(Step::RestAfterCharge, c);
    } else {  // storage charge done
      if (c.cfg->capTestIr) enterRest(Step::RestBeforeIr, c);
      else finishJob(c, "capacity test complete");
    }
  }
  updateOutputs(c.nowMs);
}

void Channel::dischargeComplete(Term t, const ChannelCtx &c) {
  res_.hasCap = true;
  res_.capMah = (float)stepCc_.disMah;
  res_.capMwh = (float)stepCc_.disMwh;
  res_.capS = (c.nowMs - stepStartMs_) / 1000;
  res_.disTerm = t;
  st_ = ChState::Idle;
  updateOutputs(c.nowMs);
  if (prog_ == Program::Discharge) {
    finishJob(c, "discharged");
  } else {
    enterRest(Step::RestAfterDischarge, c);
  }
  updateOutputs(c.nowMs);
}

void Channel::restComplete(const ChannelCtx &c) {
  switch (step_) {
    case Step::RestAfterCharge: enterDischarge(c); break;
    case Step::RestAfterDischarge: {
      storeTargetMah_ = res_.capMah * (float)c.cfg->storagePct / 100.0f;
      if (c.cfg->storagePct == 0 || storeTargetMah_ < 1.0f) {
        if (c.cfg->capTestIr) enterIr(c);
        else finishJob(c, "capacity test complete");
      } else {
        enterCharge(Step::StorageCharge, c);
      }
      break;
    }
    case Step::RestBeforeIr: enterIr(c); break;
    default: finishJob(c, "done"); break;
  }
  updateOutputs(c.nowMs);
}

void Channel::pauseFor(PauseReason r, const ChannelCtx &c) {
  if (st_ != ChState::Paused) resumeSt_ = st_;
  st_ = ChState::Paused;
  pause_ = r;
  noCurTiming_ = false;
  termTiming_ = false;
  updateOutputs(c.nowMs);
}

void Channel::resume(const ChannelCtx &c) {
  st_ = resumeSt_;
  pause_ = PauseReason::None;
  noCurTiming_ = false;
  termTiming_ = false;
  vmaxCount_ = 0;
  cutCount_ = 0;
  if (st_ == ChState::ChargingPre) {
    preOn_ = true;
    preT0_ = c.nowMs;
    prePulseSamples_ = 0;
    prePulseCur_ = false;
  }
  cvN_ = 0;
  ccLowCount_ = 0;
  updateOutputs(c.nowMs);
}

void Channel::irDone(const IrResult &r, const ChannelCtx &c) {
  if (r.ok) {
    res_.hasIr = true;
    res_.irOhmicMohm = r.rOhmic * 1000.0f;
    res_.irDcMohm = r.rDc * 1000.0f;
    res_.irI = r.iPulse;
    res_.irV0 = r.v0;
    res_.irChargePulse = r.chargePulse;
    res_.irAtS = c.nowMs / 1000;
  }
  irSub_ = IrSub::WaitRest;
  char m[56];
  if (r.ok) snprintf(m, sizeof(m), "IR ok (%s pulse)", r.chargePulse ? "charge" : "discharge");
  else snprintf(m, sizeof(m), "IR failed: %s", r.err);
  if (prog_ == Program::CapTest) finishJob(c, r.ok ? "capacity test complete" : m);
  else finishJob(c, m);
}

// ------------------------------------------------------------- samples ----
void Channel::onSampleError(const ChannelCtx &c, const char *why) {
  last_.valid = false;
  if (jobActive()) fault(ChState::FaultAdc, c, why);
}

bool Channel::temperatureChecks(const ChSample &s, const ChannelCtx &c, bool charging) {
  const Settings &cfg = *c.cfg;
  if (s.ntc == NtcStatus::Short || s.ntc == NtcStatus::Invalid) {
    fault(ChState::FaultAdc, c, "NTC short/invalid");
    return false;
  }
  if (s.ntc == NtcStatus::Open) {
    if (ntcAtStart_) {
      fault(ChState::FaultAdc, c, "NTC disconnected during job");
      return false;
    }
    return true;  // no NTC fitted: board temperature still protects
  }
  if (s.tCell >= cfg.tCellFault) {
    fault(ChState::FaultOvertemp, c, "cell over-temperature");
    return false;
  }
  if (powerPhase()) {
    if (s.tCell >= cfg.tCellMax) {
      pauseFor(PauseReason::CellHot, c);
      return false;
    }
    if (charging && s.tCell < cfg.tCellMinChg) {
      pauseFor(PauseReason::CellCold, c);
      return false;
    }
  }
  return true;
}

void Channel::onSample(const ChSample &s, const ChannelCtx &c) {
  const Settings &cfg = *c.cfg;
  last_ = s;
  if (!s.valid) {
    onSampleError(c, "invalid sample");
    return;
  }
  lastValidMs_ = s.tMs;

  if (jobActive()) {
    stepCc_.add(s.v, s.i, s.tMs);
    jobCc_.add(s.v, s.i, s.tMs);
  }

  if (chStateIsFault(st_)) return;  // latched until reset

  // ---- §6 OV latch: charger enabled, no current, V >= 3.75 V -> the hardware
  // latch isolated the cell and killed the charger.
  if (outChg_ && fabsf(s.i) < kNoCurrentA && s.v >= kOvLatchDetectV && s.tMs - outOnSinceMs_ > 300) {
    fault(ChState::FaultOv, c, "hardware OV latch tripped (charger killed)");
    return;
  }
  // ---- OV: > V_ABS_MAX. With CHG_EN=1 immediately, otherwise for 2 s (§7).
  if (s.v > cfg.vAbsMax) {
    if (outChg_) {
      fault(ChState::FaultOv, c, "V > V_ABS_MAX while charging");
      return;
    }
    if (!ovTiming_) {
      ovTiming_ = true;
      ovT0_ = s.tMs;
    } else if (s.tMs - ovT0_ >= kOvRestMs) {
      fault(ChState::FaultOv, c, "V > V_ABS_MAX for 2 s");
      return;
    }
  } else {
    ovTiming_ = false;
  }

  if (!jobActive()) {
    presence(s);
    return;
  }

  // ---- job active: generic plausibility ----
  if (s.v < kVReversed) {
    fault(ChState::FaultAdc, c, "negative cell voltage during job");
    return;
  }
  if (fabsf(s.v) < kVPresentMin && !outChg_) {
    stop(c, "cell removed");
    st_ = ChState::Empty;
    return;
  }
  if (fabsf(s.i) > kIAbsMaxA) {
    fault(ChState::FaultCurrent, c, "over-current");
    return;
  }
  if (!temperatureChecks(s, c, isCharging(st_) || (st_ == ChState::Paused && isCharging(resumeSt_)))) return;

  switch (st_) {
    case ChState::ChargingPre:
    case ChState::ChargingCc:
    case ChState::ChargingCv: chargeSample(s, c); break;
    case ChState::Discharging: dischargeSample(s, c); break;
    case ChState::Resting:
      // outputs are off: after a short grace no current may flow
      if (s.tMs - stepStartMs_ > 1000 && (s.i > kWrongDirA || s.i < -kWrongDirA))
        fault(ChState::FaultCurrent, c, "current while resting");
      break;
    case ChState::IrMeasure:
      if (fabsf(s.i) > 0.05f) irQuietSinceMs_ = s.tMs;  // need >= irRestS without current
      break;
    case ChState::Paused:
      if (pause_ == PauseReason::CellHot) {
        if (s.ntc == NtcStatus::Ok && s.tCell < cfg.tCellResume) resume(c);
      } else if (pause_ == PauseReason::CellCold) {
        if (s.ntc != NtcStatus::Ok || s.tCell >= cfg.tCellMinChg + 3.0f) resume(c);
      }
      break;
    default: break;
  }
}

void Channel::chargeSample(const ChSample &s, const ChannelCtx &c) {
  const Settings &cfg = *c.cfg;
  const uint32_t now = s.tMs;
  if (s.i < -kWrongDirA) {
    fault(ChState::FaultCurrent, c, "discharge current while charging");
    return;
  }
  float iNom = isetNominalA(c.isetEff, c.vin);
  if (outChg_ && s.i > iNom * 1.35f + 0.15f) {
    fault(ChState::FaultCurrent, c, "charge current above ISET");
    return;
  }
  // §7: firmware stops the charge if V_MAX_CHG is exceeded.
  if (s.v >= cfg.vMaxChg) {
    if (++vmaxCount_ >= kVmaxCount) {
      chargeComplete(Term::VMax, c);
      return;
    }
  } else {
    vmaxCount_ = 0;
  }
  // Storage charge: stop at the Ah target.
  if (step_ == Step::StorageCharge && stepCc_.chgMah >= storeTargetMah_) {
    chargeComplete(Term::Target, c);
    return;
  }

  const float iExp = chargeExpectedA(c.isetEff, c.vin, s.v);
  if (st_ == ChState::ChargingPre) {
    const uint32_t settle = preDuty_ == 50 ? 200u : 400u;
    if (preOn_ && now - preT0_ >= settle) {  // pulse current settled
      prePulseSamples_++;
      if (s.i >= kNoCurrentA) prePulseCur_ = true;
    }
    const uint32_t decide = preDuty_ == 50 ? kPre50DecideMs : kPre20DecideMs;
    if (!preOn_ && now - preT0_ >= decide) {  // rest voltage in the off phase
      if (s.v < cfg.vDead) {
        fault(ChState::FaultDeadCell, c, "V < V_DEAD during precharge");
      } else if (s.v >= cfg.vCcMin) {
        st_ = ChState::ChargingCc;  // continuous CC allowed from 3.05 V
        noCurTiming_ = false;
        ccLowCount_ = 0;
        updateOutputs(now);
      } else if (preDuty_ == 20 && s.v >= cfg.vPrecharge) {
        enterPulse(50, now);
        preOn_ = false;  // continue with the off phase of the new pattern
        updateOutputs(now);
      }
    }
    return;
  }

  // No current although the charger is enabled and should deliver some
  // (OV latch, PTC, open wire). The expected current includes the passive
  // ceiling (V_IN - 0.38 - V_cell)/1.5 ohm, so a low rail is not a fault.
  bool expectCurrent = outChg_ && c.isetEff > 0 && iExp >= kExpectMinA && now - outOnSinceMs_ > 1000;
  bool lowCurrent = s.i < kNoCurrentA;
  bool termRegion = (st_ == ChState::ChargingCv && s.v >= kTermMinV);
  if (expectCurrent && lowCurrent && !termRegion) {
    if (!noCurTiming_) {
      noCurTiming_ = true;
      noCurT0_ = now;
    } else if (now - noCurT0_ >= kNoChgCurrentMs) {
      fault(ChState::FaultCurrent, c, "no charge current");
      return;
    }
  } else {
    noCurTiming_ = false;
  }

  if (st_ == ChState::ChargingCc) {
    // §4 thermal policy: V under charge fell below V_CC_MIN -> pulse again
    if (s.v < cfg.vCcMin - kCcBackHyst) {
      if (++ccLowCount_ >= 3) {
        enterPulse(s.v < cfg.vPrecharge ? 20 : 50, now);
        updateOutputs(now);
      }
      return;
    }
    ccLowCount_ = 0;
    // CV: hardware CV reached (V >= 3.50 V), or the current fell well below
    // what the charger should deliver at this voltage (set-point / ceiling).
    if (s.v >= kCvDetectV || (s.v >= kCvTaperMinV && iExp > 0.1f && s.i < kCvTaperFrac * iExp)) {
      st_ = ChState::ChargingCv;
      termTiming_ = false;
      cvN_ = 0;
    }
    return;
  }

  // CV: end of charge when I < I_TERM for t_term (60 s). Decided by the
  // current taper only - the hardware CV is 3.51..3.62 V, so reaching a
  // particular voltage is never required.
  if (st_ == ChState::ChargingCv) {
    if (s.i < cfg.iTerm && s.v >= kTermMinV && c.isetEff > 0) {
      if (!termTiming_) {
        termTiming_ = true;
        termT0_ = now;
      } else if (now - termT0_ >= (uint32_t)cfg.tTermS * 1000u) {
        chargeComplete(Term::ITerm, c);
        return;
      }
    } else {
      termTiming_ = false;
    }
    // Plateau: the CV current has stopped falling (dI over 10 min below
    // max(5 mA, 3 %)) while already small -> the cell is full.
    if (cvN_ == 0) {
      cvIema_ = s.i;
      cvPushMs_ = now;
      cvHist_[0] = s.i;
      cvN_ = 1;
    } else {
      cvIema_ += 0.1f * (s.i - cvIema_);
      if (now - cvPushMs_ >= kPlateauStepMs) {
        cvPushMs_ = now;
        for (int j = kPlateauSpan; j > 0; j--) cvHist_[j] = cvHist_[j - 1];
        cvHist_[0] = cvIema_;
        if (cvN_ <= kPlateauSpan) cvN_++;
        if (cvN_ > kPlateauSpan && s.v >= kTermMinV && c.isetEff > 0 && cvIema_ < kPlateauMaxA) {
          float old = cvHist_[kPlateauSpan];
          float drop = old - cvIema_;
          float need = old * kPlateauMinDropFrac;
          if (need < kPlateauMinDropA) need = kPlateauMinDropA;
          if (drop < need) chargeComplete(Term::Plateau, c);
        }
      }
    }
  }
}

void Channel::dischargeSample(const ChSample &s, const ChannelCtx &c) {
  const Settings &cfg = *c.cfg;
  const uint32_t now = s.tMs;
  if (s.i > kWrongDirA) {
    fault(ChState::FaultCurrent, c, "charge current while discharging");
    return;
  }
  if (s.i < -kDisMaxA) {
    fault(ChState::FaultCurrent, c, "discharge current too high");
    return;
  }
  // §7: cut-off at V_MIN_DIS measured under load (3 consecutive samples,
  // or immediately when far below).
  if (s.v <= cfg.vMinDis) {
    if (++cutCount_ >= kCutCount || s.v < cfg.vMinDis - kCutHardMargin) {
      dischargeComplete(Term::Cutoff, c);
      return;
    }
  } else {
    cutCount_ = 0;
  }
  bool expectCurrent = outDis_ && now - outOnSinceMs_ > 1000;
  if (expectCurrent && s.i > kDisNoCurrentA) {
    if (!noCurTiming_) {
      noCurTiming_ = true;
      noCurT0_ = now;
    } else if (now - noCurT0_ >= kNoDisCurrentMs) {
      if (s.v < kUvBackstopMaxV) dischargeComplete(Term::UvBackstop, c);
      else fault(ChState::FaultCurrent, c, "no discharge current");
    }
  } else {
    noCurTiming_ = false;
  }
}

// ---------------------------------------------------------------- tick ----
void Channel::tick(const ChannelCtx &c) {
  const Settings &cfg = *c.cfg;
  const uint32_t now = c.nowMs;
  uint32_t dt = now - lastTickMs_;
  lastTickMs_ = now;

  if (jobActive()) {
    // Stale measurement while a job runs -> stop (the IR engine has its own watchdog).
    if (!irRunning() && now - lastValidMs_ > kStaleSampleMs) {
      fault(ChState::FaultAdc, c, "no valid measurement");
      return;
    }
    if (!c.powerOk && (powerPhase() || irRunning())) {
      fault(ChState::FaultSafety, c, "power not allowed");
      return;
    }
    if (powerPhase()) stepActiveMs_ += dt;

    switch (st_) {
      case ChState::ChargingPre:
        if (preDuty_ == 20 && now - preStartMs_ >= kPre20MaxMs) {
          fault(ChState::FaultDeadCell, c, "precharge did not recover the cell");
          return;
        }
        if (now - pulseStartMs_ >= kPulseMaxMs) {
          fault(ChState::FaultTimeout, c, "pulsed charge phase too long");
          return;
        }
        if (preOn_ && now - preT0_ >= pulseOnMs()) {
          preOn_ = false;
          preT0_ = now;
          // A pulse that was sampled but never showed current: count it.
          bool expect = chargeExpectedA(c.isetEff, c.vin, last_.v) >= kExpectMinA;
          if (prePulseSamples_ > 0 && !prePulseCur_ && expect) {
            if (++preNoCurPulses_ >= 3) {
              fault(ChState::FaultCurrent, c, "no charge current in pulses");
              return;
            }
          } else if (prePulseCur_) {
            preNoCurPulses_ = 0;
          }
        } else if (!preOn_ && now - preT0_ >= pulseOffMs()) {
          preOn_ = true;
          preT0_ = now;
          prePulseSamples_ = 0;
          prePulseCur_ = false;
        }
        [[fallthrough]];  // common charge checks
      case ChState::ChargingCc:
      case ChState::ChargingCv:
        if (stepActiveMs_ >= (uint32_t)(cfg.maxChgH * 3600000.0f)) {
          fault(ChState::FaultTimeout, c, "charge timeout");
          return;
        }
        if (c.isetEff == 0) pauseFor(PauseReason::ChargerBlocked, c);
        break;
      case ChState::Discharging:
        if (stepActiveMs_ >= (uint32_t)(cfg.maxDisH * 3600000.0f)) {
          fault(ChState::FaultTimeout, c, "discharge timeout");
          return;
        }
        if (c.boardHot) pauseFor(PauseReason::BoardHot, c);
        break;
      case ChState::Resting:
        if (now - stepStartMs_ >= restDurationMs(cfg)) restComplete(c);
        break;
      case ChState::IrMeasure:
        if (irSub_ == IrSub::WaitRest && now - irQuietSinceMs_ >= (uint32_t)cfg.irRestS * 1000u) irSub_ = IrSub::Ready;
        break;
      case ChState::Paused:
        if (pause_ == PauseReason::BoardHot && !c.boardHot) resume(c);
        else if (pause_ == PauseReason::ChargerBlocked && c.isetEff > 0) resume(c);
        break;
      default: break;
    }
  }
  updateOutputs(now);
}

}  // namespace lfp8
