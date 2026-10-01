#include "controller.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "leds.h"

namespace lfp8 {

namespace {
constexpr uint32_t kBoardReadMs = 100;
constexpr uint32_t kLoopMaxWindowMs = 10000;
constexpr uint32_t kIdentifyMs = 10000;
constexpr float kIrVCollapse = kUvTrip - 0.10f;
constexpr uint8_t kBmCount = 3;
constexpr float kBmIdleMax = 0.10f;   // |B-S - SHN| with no current -> B- sense warning
constexpr uint8_t kContactCount = 2;  // consecutive estimates > 0.3 ohm -> CONTACT_WARN
}  // namespace

// ------------------------------------------------------------------ setup ----
void Controller::begin(const Settings &s) {
  cfg_ = s;
  settingsSanitize(cfg_);
  const uint32_t now = hal_.millis();
  hal_.setHeartbeat(false);
  hb_ = false;
  hal_.shiftWrite(0);  // everything off before anything else
  lastWord_ = 0;
  lastWriteMs_ = now;
  hal_.setFanPct(0);
  fanPct_ = 0;
  sup_.init(now);
  hist_.reset(now);
  for (int k = 0; k < kNumCh; k++) {
    ch_[k].init((uint8_t)(k + 1));
    vchk_[k] = NAN;
    vbm_[k] = NAN;
    contactOhm_[k] = NAN;
    contactWarn_[k] = false;
    contactCount_[k] = 0;
    outOn_[k] = false;
    outOffSinceMs_[k] = now;
  }
  ir_.active = false;
  scanSt_ = ScanSt::Select;
  scanCh_ = kNumCh;
  cycleStartMs_ = now;
  loopMaxResetMs_ = now;

  // Probe the ADS1115: write a config (no conversion) and read it back.
  const uint16_t w = (uint16_t)(ads::configWord(ads::kAin0Ain1, ads::kFs4096, ads::kSps128) & ~ads::kOsBit);
  uint16_t rb = 0;
  bool ok = hal_.adsWrite(ads::kRegConfig, w) && hal_.adsRead(ads::kRegConfig, rb);
  if (!ok) sup_.killHeartbeat(BoardAlarm::AdsMissing, "no I2C response at 0x48", now);
  else if ((rb & 0x7FFFu) != (w & 0x7FFFu)) sup_.killHeartbeat(BoardAlarm::AdsMissing, "config read-back mismatch", now);

  // First boot: calibrate the board-temperature diode assuming ambient = 25 C.
  if (!cfg_.v25Valid) {
    int32_t sum = 0, n = 0;
    for (int j = 0; j < 8; j++) {
      int32_t mv = hal_.adcReadMv(AdcPin::Tbrd);
      if (mv >= 0) {
        sum += mv;
        n++;
      }
    }
    float v = n ? (float)sum / (float)n / 1000.0f : -1.0f;
    cfg_.v25 = (n && v > 0.55f && v < 0.73f) ? v : kTbrdV25Default;
    cfg_.v25Valid = 1;
    cfgDirty_ = true;
  }
  readBoardInputs(now);
  lastBoardReadMs_ = now;
}

void Controller::setSettings(const Settings &s) {
  cfg_ = s;
  settingsSanitize(cfg_);
}

uint8_t Controller::isetEff() const {
  uint8_t lim = sup_.isetLimit();
  return cfg_.iset < lim ? cfg_.iset : lim;
}

ChannelCtx Controller::ctx(uint32_t nowMs) const {
  ChannelCtx c;
  c.cfg = &cfg_;
  c.nowMs = nowMs;
  c.isetEff = isetEff();
  c.vin = vinValid_ ? vin_ : 5.0f;
  c.powerOk = sup_.powerAllowed();
  c.boardHot = sup_.boardHot();
  return c;
}

bool Controller::anyJobActive() const {
  for (int k = 0; k < kNumCh; k++)
    if (ch_[k].jobActive()) return true;
  return false;
}

void Controller::readBoardInputs(uint32_t nowMs) {
  (void)nowMs;
  int32_t mv = hal_.adcReadMv(AdcPin::Vin);
  if (mv >= 0) {
    float v = vinFromAdc((float)mv / 1000.0f);
    vin_ = vinValid_ ? 0.6f * vin_ + 0.4f * v : v;
    vinValid_ = true;
  } else {
    vinValid_ = false;
  }
  mv = hal_.adcReadMv(AdcPin::Tbrd);
  if (mv >= 0 && boardDiodePlausible((float)mv / 1000.0f)) {
    float v = (float)mv / 1000.0f;
    tbrdV_ = tBoardValid_ ? 0.6f * tbrdV_ + 0.4f * v : v;
    tBoard_ = boardTempC(tbrdV_, cfg_.v25);
    tBoardValid_ = true;
  } else {
    tBoardValid_ = false;
  }
}

void Controller::invalidateSamples() {
  if (samplesInvalidated_) return;
  samplesInvalidated_ = true;
  for (int k = 0; k < kNumCh; k++) ch_[k].invalidate();
}

void Controller::abortAll(const char *why, uint32_t nowMs) {
  ChannelCtx c = ctx(nowMs);
  if (ir_.active) {
    ir_.pulseOn = false;
    ir_.active = false;
    scanSt_ = ScanSt::Select;
  }
  for (int k = 0; k < kNumCh; k++) ch_[k].abortSafety(c, why);
  writeOutputs(nowMs, true);
}

void Controller::setMaintenance(bool on) {
  uint32_t now = hal_.millis();
  if (on) {
    abortAll("maintenance (OTA)", now);
    sup_.setMaintenance(true);
    hal_.shiftWrite(0);
    lastWord_ = 0;
    hb_ = false;
    hal_.setHeartbeat(false);
  } else {
    sup_.setMaintenance(false);
  }
}

// ------------------------------------------------------------------ loop -----
void Controller::step() {
  const uint32_t nowUs = hal_.micros();
  const uint32_t nowMs = hal_.millis();
  if (haveLastStep_) {
    uint32_t gap = nowUs - lastStepUs_;
    if (gap > gapMaxUs_) gapMaxUs_ = gap;
    if (gap > loopMaxUs_) loopMaxUs_ = gap;
  }
  haveLastStep_ = true;
  lastStepUs_ = nowUs;
  if (nowMs - loopMaxResetMs_ >= kLoopMaxWindowMs) {  // publish the max of the last window
    loopMaxPub_ = loopMaxUs_;
    loopMaxUs_ = 0;
    loopMaxResetMs_ = nowMs;
  }

  // ---- board inputs + supervisor ----
  safeRb_ = hal_.safeRb();
  if (nowMs - lastBoardReadMs_ >= kBoardReadMs) {
    lastBoardReadMs_ = nowMs;
    readBoardInputs(nowMs);
  }
  SupInputs in;
  in.nowMs = nowMs;
  in.safeRb = safeRb_;
  in.vinValid = vinValid_;
  in.vin = vin_;
  in.tBoardValid = tBoardValid_;
  in.tBoard = tBoard_;
  in.maxLoopGapMs = gapMaxUs_ / 1000u;
  gapMaxUs_ = 0;
  sup_.update(in, cfg_);
  if (sup_.takeAbortRequest()) abortAll(sup_.alarmText(), nowMs);

  // ---- measurement ----
  if (!sup_.measurementAllowed()) {
    if (ir_.active) {
      IrResult r;
      r.err = "SAFE_EN lost";
      irFinish(false, r, nowMs, ChState::FaultSafety, "SAFE_EN lost during IR pulse");
    }
    scanSt_ = ScanSt::Select;
    invalidateSamples();
  } else {
    samplesInvalidated_ = false;
    if (ir_.active) irStep(nowUs, nowMs);
    else scanStep(nowUs, nowMs);
  }

  // ---- channel timers ----
  ChannelCtx c = ctx(nowMs);
  for (int k = 0; k < kNumCh; k++) ch_[k].tick(c);
  if (ir_.active && !ch_[ir_.ch - 1].irRunning()) {  // stopped/faulted meanwhile
    ir_.pulseOn = false;
    ir_.active = false;
    scanSt_ = ScanSt::Select;
    writeOutputs(nowMs, true);
  }
  for (int k = 0; k < kNumCh; k++) {
    bool a = ch_[k].jobActive();
    if (wasActive_[k] && !a) resultsVersion_++;
    wasActive_[k] = a;
  }
  // ---- OV latch release: heartbeat pause, only while the board is idle ----
  if (latchResetPending_ && !anyJobActive() && !ir_.active && !sup_.latchResetActive() &&
      sup_.heartbeatAllowed()) {
    sup_.startLatchReset(nowMs, kLatchResetMs);
    latchResetPending_ = false;
    latchResetRunning_ = true;
  } else if (latchResetRunning_ && !sup_.latchResetActive()) {
    latchResetRunning_ = false;
    ovPendingMask_ = 0;
  }
  identify_ = identifyUntilMs_ != 0 && (int32_t)(identifyUntilMs_ - nowMs) > 0;
  hist_.tick(nowMs);

  // ---- outputs ----
  writeOutputs(nowMs, false);
  uint8_t f = sup_.fanPct(anyJobActive());
  if (f != fanPct_) {
    fanPct_ = f;
    hal_.setFanPct(f);
  }

  // ---- heartbeat: last action, only when the supervisor is happy ----
  if (sup_.heartbeatAllowed()) {
    hb_ = !hb_;
    hal_.setHeartbeat(hb_);
    hbToggles_++;
  } else if (hb_) {
    hb_ = false;
    hal_.setHeartbeat(false);
  }
}

// --------------------------------------------------------------- outputs -----
void Controller::writeOutputs(uint32_t nowMs, bool force) {
  uint32_t word = 0;
  if (sup_.measurementAllowed()) {
    SrOutputs o;
    srClear(o);
    const bool power = sup_.powerAllowed();
    const bool alarm = sup_.alarm() != BoardAlarm::None;
    for (int k = 1; k <= kNumCh; k++) {
      bool c = power && ch_[k - 1].wantChg();
      bool d = power && ch_[k - 1].wantDis();
      if (ir_.active && ir_.ch == k) {
        c = power && ir_.pulseOn && ir_.chargeMode;
        d = power && ir_.pulseOn && !ir_.chargeMode;
      }
      o.chg[k - 1] = c;
      o.dis[k - 1] = d;
      bool led;
      if (identify_) led = (nowMs % 200u) < 100u;
      else if (alarm) led = (nowMs % 500u) < 250u;
      else led = ledPattern(ch_[k - 1].state(), nowMs);
      o.led[k - 1] = led;
    }
    o.muxCh = ir_.active ? ir_.ch : muxCh_;
    o.iset = (ir_.active && ir_.pulseOn && ir_.chargeMode) ? kIsetIrPulse : isetEff();
    uint8_t il = 0;
    word = srBuild(o, &il);
    if (il) {
      // Never reached by design (channels request one or the other); if it
      // ever is, treat it as a firmware fault and let the hardware isolate.
      sup_.killHeartbeat(BoardAlarm::Interlock, "CHG_EN and DIS_EN requested together", nowMs);
      word = 0;
    }
  }
  for (int k = 1; k <= kNumCh; k++) {
    bool on = (word & (srBitChg(k) | srBitDis(k))) != 0;
    if (outOn_[k - 1] && !on) outOffSinceMs_[k - 1] = nowMs;
    outOn_[k - 1] = on;
  }
  if (force || word != lastWord_ || nowMs - lastWriteMs_ >= kSrRefreshMs) {
    hal_.shiftWrite(word);
    lastWord_ = word;
    lastWriteMs_ = nowMs;
  }
}

// --------------------------------------------------------------- scanner -----
bool Controller::startConv(ads::Mux m, ads::Pga p, ads::Rate r, uint32_t nowUs) {
  (void)nowUs;
  return hal_.adsWrite(ads::kRegConfig, ads::configWord(m, p, r));
}

int Controller::pollConv(ads::Rate r, uint32_t startUs, uint32_t nowUs, int16_t &code) {
  uint32_t el = nowUs - startUs;
  if (el < ads::waitUs(r)) return 0;
  uint16_t reg = 0;
  if (!hal_.adsRead(ads::kRegConfig, reg)) return -1;
  if (!(reg & ads::kOsBit)) return (el > 4u * ads::periodUs(r) + 2000u) ? -1 : 0;
  uint16_t raw = 0;
  if (!hal_.adsRead(ads::kRegConv, raw)) return -1;
  code = (int16_t)raw;
  return 1;
}

void Controller::i2cFail(uint8_t k, const char *why, uint32_t nowMs) {
  i2cErrors_++;
  if (i2cStreak_ < 255) i2cStreak_++;
  ChannelCtx c = ctx(nowMs);
  ch_[k - 1].onSampleError(c, why);  // active job -> FAULT_ADC
  if (i2cStreak_ >= kI2cFailBoard) sup_.killHeartbeat(BoardAlarm::I2c, "repeated I2C/ADS1115 failures", nowMs);
  scanSt_ = ScanSt::Select;
}

void Controller::scanStep(uint32_t nowUs, uint32_t nowMs) {
  switch (scanSt_) {
    case ScanSt::Select: {
      // An IR measurement that is ready takes the ADC between two channels, but
      // only after a full sweep since the previous one (queued IRs would
      // otherwise run back to back and starve every other channel).
      if (sup_.powerAllowed() && scansSinceIr_ >= kNumCh) {
        // round robin from the channel after the scan position, so a queue of
        // IRs is served fairly
        for (int n = 0; n < kNumCh; n++) {
          int k = (scanCh_ + n) % kNumCh;
          if (ch_[k].irReady()) {
            scansSinceIr_ = 0;
            irStart((uint8_t)(k + 1), nowMs);
            return;
          }
        }
      }
      if (scansSinceIr_ < kNumCh) scansSinceIr_++;
      scanCh_ = (uint8_t)(scanCh_ % kNumCh + 1);
      if (scanCh_ == 1) {
        scanCycleMs_ = nowMs - cycleStartMs_;
        cycleStartMs_ = nowMs;
        cycle_++;
      }
      // One extra conversion per scan cycle, round robin: B+ cross-check
      // (AIN0-GND vs ESP32 IO1) and B- diagnostic (AIN1-AIN3) alternate.
      uint8_t extraCh = (uint8_t)(cycle_ % kNumCh + 1);
      extra_ = Extra::None;
      if (scanCh_ == extraCh) extra_ = ((cycle_ / kNumCh) % 2 == 0) ? Extra::Xchk : Extra::Diag;
      muxCh_ = scanCh_;
      writeOutputs(nowMs, true);  // mux select now
      scanTUs_ = hal_.micros();
      scanSelUs_ = scanTUs_;
      scanSt_ = ScanSt::Settle;
      return;
    }
    case ScanSt::Settle:
      if (nowUs - scanTUs_ < kMuxSettleUs) return;
      ntcMv_ = hal_.adcReadMv(AdcPin::Ntc);
      if (!startConv(ads::kAin0Ain1, ads::kFs4096, ads::kSps128, nowUs)) {
        i2cFail(scanCh_, "I2C error (V)", nowMs);
        return;
      }
      scanTUs_ = hal_.micros();
      scanSt_ = ScanSt::WaitV;
      return;
    case ScanSt::WaitV: {
      int r = pollConv(ads::kSps128, scanTUs_, nowUs, vCode_);
      if (r == 0) return;
      if (r < 0 || !startConv(ads::kAin2Ain3, ads::kFs256, ads::kSps128, nowUs)) {
        i2cFail(scanCh_, "I2C/ADS error (V)", nowMs);
        return;
      }
      scanTUs_ = hal_.micros();
      scanSt_ = ScanSt::WaitI;
      return;
    }
    case ScanSt::WaitI: {
      int r = pollConv(ads::kSps128, scanTUs_, nowUs, iCode_);
      if (r == 0) return;
      if (r < 0) {
        i2cFail(scanCh_, "I2C/ADS error (I)", nowMs);
        return;
      }
      if (extra_ == Extra::Xchk) {
        scanSt_ = ScanSt::XDwell;  // IO1 needs >= 25 ms on this channel (1M/1M + 10 nF)
        return;
      }
      if (extra_ == Extra::Diag) {
        // ±1.024 V (instead of ±0.256 V) so that up to ~0.8 ohm of B- path
        // resistance can be measured at ~1 A.
        if (!startConv(ads::kAin1Ain3, ads::kFs1024, ads::kSps128, nowUs)) {
          i2cFail(scanCh_, "I2C error (extra)", nowMs);
          return;
        }
        scanTUs_ = hal_.micros();
        scanSt_ = ScanSt::WaitX;
        return;
      }
      finishSample(nowMs);
      scanSt_ = ScanSt::Select;
      return;
    }
    case ScanSt::XDwell:
      if (nowUs - scanSelUs_ < kVchkDwellUs) return;
      vchkMvA_ = hal_.adcReadMv(AdcPin::Vchk);
      if (!startConv(ads::kAin0Gnd, ads::kFs4096, ads::kSps128, nowUs)) {
        i2cFail(scanCh_, "I2C error (extra)", nowMs);
        return;
      }
      scanTUs_ = hal_.micros();
      scanSt_ = ScanSt::WaitX;
      return;
    case ScanSt::WaitX: {
      int r = pollConv(ads::kSps128, scanTUs_, nowUs, xCode_);
      if (r == 0) return;
      if (r < 0) {
        i2cFail(scanCh_, "I2C/ADS error (extra)", nowMs);
        return;
      }
      if (extra_ == Extra::Xchk) vchkMvB_ = hal_.adcReadMv(AdcPin::Vchk);
      finishSample(nowMs);
      scanSt_ = ScanSt::Select;
      return;
    }
  }
}

void Controller::finishSample(uint32_t nowMs) {
  const uint8_t k = scanCh_;
  const int k0 = k - 1;
  ChannelCtx c = ctx(nowMs);
  i2cStreak_ = 0;  // the bus worked

  if (adsCodeSaturated(vCode_) || adsCodeSaturated(iCode_)) {
    ch_[k0].onSampleError(c, "ADC reading saturated");
    return;
  }
  ChSample s;
  s.tMs = nowMs;
  s.valid = true;
  s.v = adsCodeToCellV(vCode_, cfg_.calV[k0]);
  s.i = adsCodeToCellI(iCode_, cfg_.calI[k0]);
  if (ntcMv_ < 0) {
    s.ntc = NtcStatus::Invalid;
    s.tCell = NAN;
  } else {
    s.tCell = ntcTempC((float)ntcMv_ / 1000.0f, s.ntc);
  }

  // ---- ADC cross-check: ESP32 IO1 (B+S / 2) vs ADS1115 AIN0-GND ----
  if (extra_ == Extra::Xchk) {
    float bpAds = adsCodeToVolts(xCode_, ads::kFs4096) * cfg_.calV[k0];
    bool espOk = vchkMvA_ >= 0 && vchkMvB_ >= 0;
    float bpEsp = espOk ? vchkToBplus((float)(vchkMvA_ + vchkMvB_) / 2000.0f) : NAN;
    vchk_[k0] = bpEsp;
    if (s.v > kXchkMinCellV) {  // only meaningful with a cell present
      bool bad = !espOk || adsCodeSaturated(xCode_) || fabsf(bpEsp - bpAds) > cfg_.xchkTolV;
      if (bad) {
        if (++xchkFail_[k0] >= kXchkFailCount) {
          char m[56];
          snprintf(m, sizeof(m), "ch%u B+ ESP32 %.3f V vs ADS %.3f V", (unsigned)k, (double)bpEsp, (double)bpAds);
          ch_[k0].fault(ChState::FaultAdc, c, "ADC cross-check mismatch");
          sup_.killHeartbeat(BoardAlarm::AdcMismatch, m, nowMs);
          xchkFail_[k0] = 0;
          return;
        }
      } else {
        xchkFail_[k0] = 0;
      }
    } else {
      xchkFail_[k0] = 0;
    }
  }
  // ---- B- diagnostic (AIN1-AIN3 = I * (R_bminus + 2 x 28 mOhm + 0.1 ohm)) ----
  // Warnings only: the implied B- force-path (wire + contact) resistance and
  // an idle offset (no current but B-S far from the shunt: open B- wire?).
  if (extra_ == Extra::Diag) {
    float vbm = adsCodeToVolts(xCode_, ads::kFs1024);
    vbm_[k0] = vbm;
    bool present = s.v > kVPresentMin;
    if (!present) {
      contactOhm_[k0] = NAN;
      contactWarn_[k0] = false;
      contactCount_[k0] = 0;
      bmCount_[k0] = 0;
    } else if (fabsf(s.i) >= kContactMinI) {
      float r = vbm / s.i - kRbmFixed;  // saturated code -> lower bound
      contactOhm_[k0] = r;
      if (r > kContactWarnOhm || adsCodeSaturated(xCode_)) {
        if (contactCount_[k0] < 255) contactCount_[k0]++;
      } else {
        contactCount_[k0] = 0;
      }
      contactWarn_[k0] = contactCount_[k0] >= kContactCount;
      bmCount_[k0] = 0;
    } else if (fabsf(s.i) < 0.02f) {
      if (fabsf(vbm) > kBmIdleMax) {
        if (bmCount_[k0] < 255) bmCount_[k0]++;
      } else {
        bmCount_[k0] = 0;
      }
    }
    ch_[k0].setBmWarn(bmCount_[k0] >= kBmCount);
  }
  // ---- stray current: outputs off (and settled) but current flows ----
  bool off = !outOn_[k0] && nowMs - outOffSinceMs_[k0] >= kOutputSettleMs;
  if (off && fabsf(s.i) > kStrayCurrentA) {
    if (++stray_[k0] >= kStrayCount) {
      char m[48];
      snprintf(m, sizeof(m), "ch%u %.2f A with outputs off", (unsigned)k, (double)s.i);
      ch_[k0].fault(ChState::FaultCurrent, c, "current with outputs off");
      sup_.killHeartbeat(BoardAlarm::StrayCurrent, m, nowMs);
      stray_[k0] = 0;
      return;
    }
  } else {
    stray_[k0] = 0;
  }

  ch_[k0].onSample(s, c);
  hist_.accumulate(k0, s.v, s.i);
}

// ------------------------------------------------------------- IR engine -----
void Controller::irStart(uint8_t k, uint32_t nowMs) {
  ir_.active = true;
  ir_.ch = k;
  ir_.ph = IrPh::Settle;
  ir_.startMs = nowMs;
  ir_.nBaseV = ir_.nBaseI = 0;
  ir_.sumV = ir_.sumI = 0;
  ir_.nv = ir_.ni = 0;
  ir_.pulseOn = false;
  ir_.chargeMode = false;
  lastIrValid_ = false;
  ch_[k - 1].irBegin();
  muxCh_ = k;
  writeOutputs(nowMs, true);  // mux locked on the channel
  ir_.tUs = hal_.micros();
}

void Controller::irFinish(bool ok, const IrResult &r, uint32_t nowMs, ChState faultState, const char *why) {
  const uint8_t k = ir_.ch;
  ir_.pulseOn = false;
  ir_.active = false;
  writeOutputs(nowMs, true);  // load/charger off immediately
  ChannelCtx c = ctx(nowMs);
  if (chStateIsFault(faultState)) {
    ch_[k - 1].fault(faultState, c, why);
  } else {
    IrResult rr = r;
    rr.ok = ok && r.ok;
    ch_[k - 1].irDone(rr, c);
  }
  lastIr_ = r;
  lastIr_.ok = ok && r.ok;
  lastIrCh_ = k;
  lastIrValid_ = ir_.nv > 0 || ir_.ni > 0;
  scanSt_ = ScanSt::Select;
}

void Controller::irStep(uint32_t nowUs, uint32_t nowMs) {
  const uint8_t k = ir_.ch;
  IrResult fail;
  fail.v0 = ir_.v0;
  fail.i0 = ir_.i0;
  fail.chargePulse = ir_.chargeMode;
  if (nowMs - ir_.startMs > kIrMaxDurationMs) {
    fail.err = "IR engine timeout";
    irFinish(false, fail, nowMs, ChState::FaultAdc, "IR engine timeout");
    return;
  }
  if (ir_.ph == IrPh::Settle) {
    if (nowUs - ir_.tUs < kMuxSettleUs) return;
    ir_.convIsV = true;
    if (!startConv(ads::kAin0Ain1, ads::kFs4096, ads::kSps860, nowUs)) {
      i2cErrors_++;
      fail.err = "I2C error";
      irFinish(false, fail, nowMs, ChState::FaultAdc, "I2C error during IR");
      return;
    }
    ir_.tUs = hal_.micros();
    ir_.ph = IrPh::Base;
    return;
  }

  int16_t code = 0;
  int pr = pollConv(ads::kSps860, ir_.tUs, nowUs, code);
  if (pr == 0) return;
  if (pr < 0) {
    i2cErrors_++;
    fail.err = "I2C error";
    irFinish(false, fail, nowMs, ChState::FaultAdc, "I2C error during IR");
    return;
  }
  if (adsCodeSaturated(code)) {
    fail.err = "ADC saturated";
    irFinish(false, fail, nowMs, ChState::FaultAdc, "ADC saturated during IR");
    return;
  }
  const bool wasV = ir_.convIsV;
  const uint32_t convStart = ir_.tUs;
  const float val = wasV ? adsCodeToCellV(code, cfg_.calV[k - 1]) : adsCodeToCellI(code, cfg_.calI[k - 1]);

  if (ir_.ph == IrPh::Base) {
    if (wasV) {
      ir_.sumV += val;
      ir_.nBaseV++;
    } else {
      ir_.sumI += val;
      ir_.nBaseI++;
    }
    if (ir_.nBaseV >= kIrBaseSamples && ir_.nBaseI >= kIrBaseSamples) {
      ir_.v0 = (float)(ir_.sumV / ir_.nBaseV);
      ir_.i0 = (float)(ir_.sumI / ir_.nBaseI);
      fail.v0 = ir_.v0;
      fail.i0 = ir_.i0;
      if (fabsf(ir_.i0) > 0.05f) {
        fail.err = "current flowing before the pulse";
        irFinish(false, fail, nowMs, ChState::Idle, nullptr);
        return;
      }
      if (ir_.v0 > cfg_.vAbsMax) {
        fail.err = "V0 above V_ABS_MAX";
        irFinish(false, fail, nowMs, ChState::FaultOv, "V > V_ABS_MAX before IR");
        return;
      }
      if (ir_.v0 < cfg_.vDead) {
        fail.err = "cell below V_DEAD";
        irFinish(false, fail, nowMs, ChState::Idle, nullptr);
        return;
      }
      // §7.1: below 2.65 V the UV backstop blocks the load -> charge pulse (ISET 4).
      ir_.chargeMode = ir_.v0 < kIrUvBlock;
      fail.chargePulse = ir_.chargeMode;
      if (ir_.chargeMode && cfg_.iset == 0) {
        fail.err = "ISET=0 blocks the charge pulse";
        irFinish(false, fail, nowMs, ChState::Idle, nullptr);
        return;
      }
      if (!sup_.powerAllowed()) {
        fail.err = "power not allowed";
        irFinish(false, fail, nowMs, ChState::Idle, nullptr);
        return;
      }
      ir_.pulseOn = true;
      writeOutputs(nowMs, true);
      ir_.t0Us = hal_.micros();
      ir_.ph = IrPh::Pulse;
      ir_.convIsV = true;
      if (!startConv(ads::kAin0Ain1, ads::kFs4096, ads::kSps860, nowUs)) {
        i2cErrors_++;
        fail.err = "I2C error";
        irFinish(false, fail, nowMs, ChState::FaultAdc, "I2C error during IR");
        return;
      }
      ir_.tUs = hal_.micros();
      return;
    }
  } else {  // Pulse
    // Timestamp = middle of the conversion window, relative to the pulse start.
    float t = (float)(int32_t)(convStart + ads::periodUs(ads::kSps860) / 2u - ir_.t0Us) / 1e6f;
    const int cap = kIrMaxSamples / 2;
    if (wasV) {
      if (ir_.nv < cap) ir_.v[ir_.nv++] = IrPoint{t, val};
      if (val > cfg_.vAbsMax) {
        fail.err = "V > V_ABS_MAX during pulse";
        irFinish(false, fail, nowMs, ChState::FaultOv, "V > V_ABS_MAX during IR pulse");
        return;
      }
      if (val < kIrVCollapse) {
        fail.err = "cell voltage collapsed under load";
        irFinish(false, fail, nowMs, ChState::Idle, nullptr);
        return;
      }
    } else {
      if (ir_.ni < cap) ir_.i[ir_.ni++] = IrPoint{t, val};
      if (fabsf(val) > kIAbsMaxA) {
        fail.err = "over-current during pulse";
        irFinish(false, fail, nowMs, ChState::FaultCurrent, "over-current during IR pulse");
        return;
      }
    }
    bool vDone = ir_.nv > 0 && ir_.v[ir_.nv - 1].t >= kIrPulseEnd;
    bool iDone = ir_.ni > 0 && ir_.i[ir_.ni - 1].t >= kIrPulseEnd;
    if ((vDone && iDone) || ir_.nv >= cap || ir_.ni >= cap) {
      ir_.pulseOn = false;
      writeOutputs(nowMs, true);  // load off first, then compute
      IrResult res;
      bool ok = irCompute(ir_.v0, ir_.i0, ir_.v, ir_.nv, ir_.i, ir_.ni, ir_.chargeMode, res);
      irFinish(ok, res, nowMs, ChState::Idle, nullptr);
      return;
    }
  }
  // Next conversion, alternating V and I.
  ir_.convIsV = !wasV;
  bool ok = ir_.convIsV ? startConv(ads::kAin0Ain1, ads::kFs4096, ads::kSps860, nowUs)
                        : startConv(ads::kAin2Ain3, ads::kFs256, ads::kSps860, nowUs);
  if (!ok) {
    i2cErrors_++;
    fail.err = "I2C error";
    irFinish(false, fail, nowMs, ChState::FaultAdc, "I2C error during IR");
    return;
  }
  ir_.tUs = hal_.micros();
}

int Controller::irTrace(IrPoint *v, int maxV, IrPoint *i, int maxI, int &nI, IrResult &res, uint8_t &ch) const {
  nI = 0;
  if (!lastIrValid_ || ir_.active) return 0;
  res = lastIr_;
  ch = lastIrCh_;
  auto decimate = [](const IrPoint *src, int n, IrPoint *dst, int maxN) {
    // keep everything in the first 50 ms, decimate the rest evenly
    int early = 0;
    while (early < n && src[early].t < 0.05f) early++;
    if (early > maxN / 2) early = maxN / 2;
    int out = 0;
    for (int j = 0; j < early; j++) dst[out++] = src[j];
    int rest = n - early, room = maxN - out;
    if (rest <= 0 || room <= 0) return out;
    int stepN = (rest + room - 1) / room;
    for (int j = early; j < n && out < maxN; j += stepN) dst[out++] = src[j];
    if (out < maxN && n > early && dst[out - 1].t != src[n - 1].t) dst[out++] = src[n - 1];
    return out;
  };
  int nV = decimate(ir_.v, ir_.nv, v, maxV);
  nI = decimate(ir_.i, ir_.ni, i, maxI);
  return nV;
}

// -------------------------------------------------------------- commands -----
const char *Controller::command(const Command &cmd) {
  const uint32_t now = hal_.millis();
  ChannelCtx c = ctx(now);
  const bool chCmd = cmd.type == Command::Type::Start || cmd.type == Command::Type::Stop ||
                     cmd.type == Command::Type::Reset || cmd.type == Command::Type::CalV ||
                     cmd.type == Command::Type::CalI;
  if (chCmd && (cmd.ch < 1 || cmd.ch > kNumCh)) return "channel must be 1..8";
  Channel *ch = chCmd ? &ch_[cmd.ch - 1] : nullptr;
  switch (cmd.type) {
    case Command::Type::Start:
      if (cmd.prog == Program::None) return "unknown program";
      if (ovPendingMask_ & (1u << (cmd.ch - 1)))
        return "hardware OV latch release pending (runs when no job is active on this board)";
      {
        const char *e = ch->start(cmd.prog, c);
        writeOutputs(now, true);
        return e;
      }
    case Command::Type::Stop:
      ch->stop(c, "stopped by user");
      if (ir_.active && ir_.ch == cmd.ch) {
        ir_.pulseOn = false;
        ir_.active = false;
        scanSt_ = ScanSt::Select;
      }
      writeOutputs(now, true);
      return nullptr;
    case Command::Type::Reset:
      if (ir_.active && ir_.ch == cmd.ch) {
        ir_.pulseOn = false;
        ir_.active = false;
        scanSt_ = ScanSt::Select;
      }
      if (ch->state() == ChState::FaultOv) {
        // §6: the OV latch only releases when SAFE_EN drops -> pause the
        // heartbeat for >= 100 ms as soon as no job is running on the board.
        ovPendingMask_ |= (uint8_t)(1u << (cmd.ch - 1));
        latchResetPending_ = true;
      }
      ch->reset(c);
      xchkFail_[cmd.ch - 1] = 0;
      contactOhm_[cmd.ch - 1] = NAN;
      contactWarn_[cmd.ch - 1] = false;
      contactCount_[cmd.ch - 1] = 0;
      bmCount_[cmd.ch - 1] = 0;
      stray_[cmd.ch - 1] = 0;
      resultsVersion_++;
      writeOutputs(now, true);
      return nullptr;
    case Command::Type::StopAll:
      for (int k = 0; k < kNumCh; k++) ch_[k].stop(c, "stopped by user (all)");
      if (ir_.active) {
        ir_.pulseOn = false;
        ir_.active = false;
        scanSt_ = ScanSt::Select;
      }
      writeOutputs(now, true);
      return nullptr;
    case Command::Type::Iset: {
      double v = cmd.value;
      if (!(v >= 0 && v <= 7) || v != (double)(int)v) return "ISET must be an integer 0..7";
      cfg_.iset = (uint8_t)v;
      settingsSanitize(cfg_);
      cfgDirty_ = true;
      return nullptr;
    }
    case Command::Type::Ack: {
      bool wasAdsMissing = sup_.alarm() == BoardAlarm::AdsMissing;
      const char *e = sup_.ack(now);
      if (e) return e;
      for (int k = 0; k < kNumCh; k++) {
        xchkFail_[k] = stray_[k] = 0;
        if (ch_[k].state() == ChState::FaultOv) latchResetPending_ = true;  // release latches too
      }
      i2cStreak_ = 0;
      if (wasAdsMissing) {
        const uint16_t w = (uint16_t)(ads::configWord(ads::kAin0Ain1, ads::kFs4096, ads::kSps128) & ~ads::kOsBit);
        uint16_t rb = 0;
        if (!hal_.adsWrite(ads::kRegConfig, w) || !hal_.adsRead(ads::kRegConfig, rb) || (rb & 0x7FFFu) != (w & 0x7FFFu)) {
          sup_.killHeartbeat(BoardAlarm::AdsMissing, "still no ADS1115", now);
          return "ADS1115 still not responding";
        }
      }
      return nullptr;
    }
    case Command::Type::Identify:
      identifyUntilMs_ = now + kIdentifyMs;
      if (identifyUntilMs_ == 0) identifyUntilMs_ = 1;
      return nullptr;
    case Command::Type::CalV: {
      const ChSample &s = ch->last();
      if (!ch->sampleFresh(now)) return "no fresh measurement";
      if (s.v < 1.0f) return "no cell (need a cell to calibrate)";
      double ratio = cmd.value / (double)s.v;
      double nc = cfg_.calV[cmd.ch - 1] * ratio;
      if (!(nc >= 0.90 && nc <= 1.10)) return "resulting CAL_V outside 0.90..1.10";
      cfg_.calV[cmd.ch - 1] = (float)nc;
      cfgDirty_ = true;
      return nullptr;
    }
    case Command::Type::CalI: {
      const ChSample &s = ch->last();
      if (!ch->sampleFresh(now)) return "no fresh measurement";
      if (fabsf(s.i) < 0.2f) return "need >= 0.2 A flowing (start a charge or discharge)";
      if ((cmd.value > 0) != (s.i > 0)) return "reference current has the wrong sign";
      double nc = cfg_.calI[cmd.ch - 1] * (cmd.value / (double)s.i);
      if (!(nc >= 0.90 && nc <= 1.10)) return "resulting CAL_I outside 0.90..1.10";
      cfg_.calI[cmd.ch - 1] = (float)nc;
      cfgDirty_ = true;
      return nullptr;
    }
    case Command::Type::CalTboard: {
      if (!tBoardValid_) return "board temperature reading invalid";
      double v25 = (double)tbrdV_ - (double)kTbrdSlope * (cmd.value - 25.0);
      if (!(v25 >= 0.45 && v25 <= 0.80)) return "resulting V25 implausible";
      cfg_.v25 = (float)v25;
      cfg_.v25Valid = 1;
      cfgDirty_ = true;
      return nullptr;
    }
    case Command::Type::Reboot:
      return "reboot is executed by the caller";
  }
  return "unknown command";
}

// ---------------------------------------------------------------- status -----
void Controller::getResults(ChResult out[kNumCh]) const {
  for (int k = 0; k < kNumCh; k++) out[k] = ch_[k].result();
}

void Controller::restoreResults(const ChResult in[kNumCh]) {
  for (int k = 0; k < kNumCh; k++) ch_[k].restoreResult(in[k]);
}

void Controller::snapshot(StatusSnapshot &o) const {
  const uint32_t now = hal_.millis();
  BoardStatus &b = o.b;
  b.uptimeS = now / 1000;
  b.vin = vin_;
  b.vinValid = vinValid_;
  b.tBoard = tBoard_;
  b.tBoardValid = tBoardValid_;
  b.safeRb = safeRb_;
  b.safeStable = sup_.safeStable();
  b.hbAllowed = sup_.heartbeatAllowed();
  b.powerAllowed = sup_.powerAllowed();
  b.measuring = sup_.measurementAllowed();
  b.boardHot = sup_.boardHot();
  b.maintenance = sup_.maintenance();
  b.identify = identify_;
  b.fanPct = fanPct_;
  b.iset = cfg_.iset;
  b.isetEff = isetEff();
  b.isetA = isetNominalA(b.isetEff, vinValid_ ? vin_ : 5.0f);
  b.alarm = sup_.alarm();
  strncpy(b.alarmText, sup_.alarmText(), sizeof(b.alarmText) - 1);
  b.alarmText[sizeof(b.alarmText) - 1] = 0;
  strncpy(b.cond, sup_.conditionText(), sizeof(b.cond) - 1);
  b.cond[sizeof(b.cond) - 1] = 0;
  b.loopMaxUs = loopMaxPub_ > loopMaxUs_ ? loopMaxPub_ : loopMaxUs_;
  b.loopOverruns = sup_.loopOverruns();
  b.i2cErrors = i2cErrors_;
  b.safeLosses = sup_.safeLossCount();
  b.scanCycleMs = scanCycleMs_;
  b.irBusy = ir_.active;
  b.irCh = ir_.active ? ir_.ch : 0;
  b.hbToggles = hbToggles_;
  b.latchReset = sup_.latchResetActive() ? 2 : (latchResetPending_ ? 1 : 0);
  for (int k = 0; k < kNumCh; k++) {
    const Channel &c = ch_[k];
    ChStatus &s = o.ch[k];
    s.k = (uint8_t)(k + 1);
    s.st = c.state();
    s.prog = c.program();
    s.step = c.step();
    s.pause = c.pause();
    s.valid = c.last().valid;
    s.v = c.last().v;
    s.i = c.last().i;
    s.tCell = c.last().tCell;
    s.ntc = c.last().ntc;
    s.vchk = vchk_[k];
    s.vbm = vbm_[k];
    s.contactMohm = contactOhm_[k] * 1000.0f;
    s.contactWarn = contactWarn_[k];
    s.ovPending = (ovPendingMask_ >> k) & 1u;
    s.pulseDuty = c.pulseDuty();
    s.bmWarn = c.bmWarn();
    s.chgOn = (lastWord_ & srBitChg(k + 1)) != 0;
    s.disOn = (lastWord_ & srBitDis(k + 1)) != 0;
    s.stepChgMah = (float)c.stepCounter().chgMah;
    s.stepDisMah = (float)c.stepCounter().disMah;
    s.stepChgMwh = (float)c.stepCounter().chgMwh;
    s.stepDisMwh = (float)c.stepCounter().disMwh;
    s.storeTargetMah = c.storageTargetMah();
    s.res = c.result();
    s.elapsedS = c.jobElapsedS(now);
    s.stepElapsedS = c.stepElapsedS(now);
    strncpy(s.msg, c.msg(), sizeof(s.msg) - 1);
    s.msg[sizeof(s.msg) - 1] = 0;
  }
}

}  // namespace lfp8
