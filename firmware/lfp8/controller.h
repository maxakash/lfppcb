// Board controller: owns the measurement scanner, the IR engine, the eight
// channel state machines, the safety supervisor, the shift-register word,
// the heartbeat, fan, LEDs and the history ring. Pure C++ on top of Hal.
//
// step() is non-blocking (at most one I2C transaction pair per call) and must
// be called continuously from the control loop (every <= 2 ms). It toggles
// the heartbeat at the end of every iteration in which the supervisor is
// happy - never from a timer.
#pragma once
#include <stdint.h>

#include "adc_conv.h"
#include "channel.h"
#include "hal.h"
#include "history.h"
#include "ir_calc.h"
#include "settings.h"
#include "shiftreg.h"
#include "supervisor.h"

namespace lfp8 {

struct Command {
  enum class Type : uint8_t { Start, Stop, Reset, Iset, Ack, StopAll, Identify, CalV, CalI, CalTboard, Reboot };
  Type type = Type::Stop;
  uint8_t ch = 0;  // 1..8 for channel commands
  Program prog = Program::None;
  double value = 0;
};

struct BoardStatus {
  uint32_t uptimeS = 0;
  float vin = 0;
  bool vinValid = false;
  float tBoard = 0;
  bool tBoardValid = false;
  bool safeRb = false, safeStable = false, hbAllowed = false, powerAllowed = false, measuring = false;
  bool boardHot = false, maintenance = false, identify = false;
  uint8_t fanPct = 0, iset = 0, isetEff = 0;
  float isetA = 0;
  BoardAlarm alarm = BoardAlarm::None;
  char alarmText[72] = {0};
  char cond[48] = {0};
  uint32_t loopMaxUs = 0, loopOverruns = 0, i2cErrors = 0, safeLosses = 0, scanCycleMs = 0;
  bool irBusy = false;
  uint8_t irCh = 0;
  uint32_t hbToggles = 0;
  uint8_t latchReset = 0;  // 0 none, 1 pending (waits for idle), 2 heartbeat paused
};

struct ChStatus {
  uint8_t k = 0;
  ChState st = ChState::Empty;
  Program prog = Program::None;
  Step step = Step::None;
  PauseReason pause = PauseReason::None;
  bool valid = false;
  float v = 0, i = 0, tCell = 0;
  NtcStatus ntc = NtcStatus::Open;
  float vchk = 0;   // ESP32 cross-check of B+ (V), NaN if not yet measured
  float vbm = 0;    // B- diagnostic (V), NaN if not yet measured
  bool bmWarn = false;
  float contactMohm = 0;  // implied B- force-path resistance (NaN if unknown)
  bool contactWarn = false;
  bool ovPending = false; // OV latch release pending
  uint8_t pulseDuty = 0;  // 20/50 % in a pulsed charge phase
  bool chgOn = false, disOn = false;
  float stepChgMah = 0, stepDisMah = 0, stepChgMwh = 0, stepDisMwh = 0;
  float storeTargetMah = 0;
  ChResult res;
  uint32_t elapsedS = 0, stepElapsedS = 0;
  char msg[56] = {0};
};

struct StatusSnapshot {
  BoardStatus b;
  ChStatus ch[kNumCh];
};

class Controller {
 public:
  explicit Controller(Hal &hal) : hal_(hal) {}
  // Writes all-zero to the shift registers, probes the ADS1115, calibrates
  // V25 on first boot (if not yet valid) and starts supervising.
  void begin(const Settings &s);
  void step();
  bool wantsFastLoop() const { return ir_.active; }

  // Commands (return nullptr on success, else a reason).
  const char *command(const Command &c);
  void setSettings(const Settings &s);
  const Settings &settings() const { return cfg_; }
  // Set when the controller changed settings itself (calibration, ISET,
  // V25 first-boot calibration); the caller persists and clears it.
  bool settingsDirty() const { return cfgDirty_; }
  void clearSettingsDirty() { cfgDirty_ = false; }

  void snapshot(StatusSnapshot &out) const;
  const History &history() const { return hist_; }
  bool anyJobActive() const;
  // OTA: stop everything, keep outputs at zero, stop the heartbeat.
  void setMaintenance(bool on);

  // Results persistence: version increments whenever any result changes.
  uint32_t resultsVersion() const { return resultsVersion_; }
  void getResults(ChResult out[kNumCh]) const;
  void restoreResults(const ChResult in[kNumCh]);

  // Last IR pulse (decimated) for the UI.
  int irTrace(IrPoint *v, int maxV, IrPoint *i, int maxI, int &nI, IrResult &res, uint8_t &ch) const;

  // Introspection (tests / diagnostics)
  const Channel &channel(int k) const { return ch_[k - 1]; }
  const Supervisor &supervisor() const { return sup_; }
  uint32_t lastWord() const { return lastWord_; }
  uint32_t heartbeatToggles() const { return hbToggles_; }
  uint32_t i2cErrors() const { return i2cErrors_; }

 private:
  enum class ScanSt : uint8_t { Select, Settle, WaitV, WaitI, XDwell, WaitX };
  enum class Extra : uint8_t { None, Xchk, Diag };
  enum class IrPh : uint8_t { Settle, Base, Pulse };

  ChannelCtx ctx(uint32_t nowMs) const;
  uint8_t isetEff() const;
  void readBoardInputs(uint32_t nowMs);
  void scanStep(uint32_t nowUs, uint32_t nowMs);
  bool startConv(ads::Mux m, ads::Pga p, ads::Rate r, uint32_t nowUs);
  // 1 = done (code valid), 0 = not ready, -1 = error
  int pollConv(ads::Rate r, uint32_t startUs, uint32_t nowUs, int16_t &code);
  void finishSample(uint32_t nowMs);
  void i2cFail(uint8_t k, const char *why, uint32_t nowMs);
  void irStart(uint8_t k, uint32_t nowMs);
  void irStep(uint32_t nowUs, uint32_t nowMs);
  void irFinish(bool ok, const IrResult &r, uint32_t nowMs, ChState faultState, const char *why);
  void writeOutputs(uint32_t nowMs, bool force);
  void abortAll(const char *why, uint32_t nowMs);
  void invalidateSamples();

  Hal &hal_;
  Settings cfg_;
  bool cfgDirty_ = false;
  Channel ch_[kNumCh];
  Supervisor sup_;
  History hist_;

  // board inputs
  float vin_ = 0, tBoard_ = 0, tbrdV_ = 0;
  bool vinValid_ = false, tBoardValid_ = false;
  uint32_t lastBoardReadMs_ = 0;
  bool safeRb_ = false;

  // loop timing
  bool haveLastStep_ = false;
  uint32_t lastStepUs_ = 0, gapMaxUs_ = 0, loopMaxUs_ = 0, loopMaxPub_ = 0;
  uint32_t loopMaxResetMs_ = 0;

  // scanner
  ScanSt scanSt_ = ScanSt::Select;
  uint8_t scanCh_ = kNumCh;  // next Select moves to 1
  uint32_t scanTUs_ = 0;
  uint32_t scanSelUs_ = 0;  // time of the mux change (IO1 dwell)
  int16_t vCode_ = 0, iCode_ = 0, xCode_ = 0;
  int32_t ntcMv_ = -1, vchkMvA_ = -1, vchkMvB_ = -1;
  Extra extra_ = Extra::None;
  uint32_t cycle_ = 0;
  uint32_t cycleStartMs_ = 0, scanCycleMs_ = 0;
  uint8_t i2cStreak_ = 0;
  uint32_t i2cErrors_ = 0;
  uint8_t xchkFail_[kNumCh] = {0};
  uint8_t bmCount_[kNumCh] = {0};
  uint8_t stray_[kNumCh] = {0};
  float vchk_[kNumCh];
  float vbm_[kNumCh];
  float contactOhm_[kNumCh];
  bool contactWarn_[kNumCh];
  uint8_t contactCount_[kNumCh];
  // OV latch release (heartbeat pause) bookkeeping
  bool latchResetPending_ = false;
  bool latchResetRunning_ = false;
  uint8_t ovPendingMask_ = 0;
  bool samplesInvalidated_ = false;

  // outputs
  uint8_t muxCh_ = 1;
  uint32_t lastWord_ = 0;
  uint32_t lastWriteMs_ = 0;
  bool wroteOnce_ = false;
  bool outOn_[kNumCh] = {false};
  uint32_t outOffSinceMs_[kNumCh] = {0};
  bool hb_ = false;
  uint32_t hbToggles_ = 0;
  uint8_t fanPct_ = 0;
  uint32_t identifyUntilMs_ = 0;
  bool identify_ = false;

  // results
  uint32_t resultsVersion_ = 0;
  bool wasActive_[kNumCh] = {false};

  // IR engine
  struct IrEngine {
    bool active = false;
    uint8_t ch = 0;
    IrPh ph = IrPh::Settle;
    bool convIsV = true;
    uint32_t tUs = 0;     // phase/conversion start
    uint32_t t0Us = 0;    // pulse start
    uint32_t startMs = 0;
    int nBaseV = 0, nBaseI = 0;
    double sumV = 0, sumI = 0;
    float v0 = 0, i0 = 0;
    bool chargeMode = false;
    bool pulseOn = false;
    int nv = 0, ni = 0;
    IrPoint v[kIrMaxSamples / 2];
    IrPoint i[kIrMaxSamples / 2];
  } ir_;
  // copy of the last finished pulse for the UI
  bool lastIrValid_ = false;
  uint8_t lastIrCh_ = 0;
  IrResult lastIr_;
};

}  // namespace lfp8
