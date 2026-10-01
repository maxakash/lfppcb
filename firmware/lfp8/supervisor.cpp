#include "supervisor.h"

#include <stdio.h>
#include <string.h>

#include "lfp8_config.h"

namespace lfp8 {

namespace {
constexpr uint32_t kVinDebounceMs = 1000;
constexpr uint32_t kStartupGraceMs = 2000;
constexpr uint32_t kSafeHighStableMs = 50;
constexpr uint32_t kLoopOverrunMs = 15;  // hardware trips 23..33 ms after the last edge
constexpr float kBoardHotHyst = 8.0f;
constexpr float kBoardAbortMargin = 7.0f;  // T_BOARD_MAX + 7 C -> abort all jobs
constexpr float kIsetHotHyst = 3.0f;
constexpr float kFanHyst = 3.0f;
constexpr uint32_t kTbInvalidMs = 2000;
}  // namespace

const char *boardAlarmName(BoardAlarm a) {
  switch (a) {
    case BoardAlarm::None: return "";
    case BoardAlarm::SafeLost: return "SAFE_LOST";
    case BoardAlarm::Vin: return "VIN";
    case BoardAlarm::BoardOvertemp: return "BOARD_OVERTEMP";
    case BoardAlarm::AdcMismatch: return "ADC_MISMATCH";
    case BoardAlarm::I2c: return "I2C";
    case BoardAlarm::AdsMissing: return "ADS1115_MISSING";
    case BoardAlarm::StrayCurrent: return "STRAY_CURRENT";
    case BoardAlarm::Interlock: return "INTERLOCK";
  }
  return "?";
}

bool boardAlarmKillsHeartbeat(BoardAlarm a) {
  return a == BoardAlarm::SafeLost || a == BoardAlarm::AdcMismatch || a == BoardAlarm::I2c ||
         a == BoardAlarm::AdsMissing || a == BoardAlarm::StrayCurrent || a == BoardAlarm::Interlock;
}

void Supervisor::init(uint32_t nowMs) {
  *this = Supervisor();
  startMs_ = nowMs;
}

void Supervisor::setCond(const char *t) {
  strncpy(condText_, t, sizeof(condText_) - 1);
  condText_[sizeof(condText_) - 1] = 0;
}

void Supervisor::latch(BoardAlarm a, const char *text, bool abort) {
  // The first alarm is kept (root cause); a heartbeat-kill alarm replaces a
  // non-kill alarm because it is the more severe state.
  bool replace = alarm_ == BoardAlarm::None ||
                 (boardAlarmKillsHeartbeat(a) && !boardAlarmKillsHeartbeat(alarm_));
  if (replace) {
    alarm_ = a;
    snprintf(alarmText_, sizeof(alarmText_), "%s: %s", boardAlarmName(a), text);
  }
  if (abort) abortReq_ = true;
}

void Supervisor::startLatchReset(uint32_t nowMs, uint32_t durMs) {
  latchReset_ = true;
  latchResetEndMs_ = nowMs + durMs;
}

void Supervisor::killHeartbeat(BoardAlarm a, const char *detail, uint32_t nowMs) {
  (void)nowMs;
  hbKilled_ = true;
  latch(a, detail, true);
}

const char *Supervisor::ack(uint32_t nowMs) {
  (void)nowMs;
  if (alarm_ == BoardAlarm::None && !hbKilled_) return nullptr;
  if (!boardAlarmKillsHeartbeat(alarm_)) {
    if (!safeStable_) return "SAFE_EN still low";
    if (vinBadNow_) return "VIN still out of range";
    if (lastTbValid_ && boardHot_) return "board still hot";
  }
  // Heartbeat-kill alarms: re-enable the heartbeat; if the cause persists
  // the alarm will trip again on the next check.
  alarm_ = BoardAlarm::None;
  alarmText_[0] = 0;
  hbKilled_ = false;
  return nullptr;
}

uint8_t Supervisor::fanPct(bool anyActive) const {
  if (fanHot_ || tbInvalid_) return 100;
  return anyActive ? 40 : 0;
}

void Supervisor::update(const SupInputs &in, const Settings &cfg) {
  const uint32_t now = in.nowMs;
  lastVin_ = in.vin;
  lastVinValid_ = in.vinValid;
  lastTb_ = in.tBoard;
  lastTbValid_ = in.tBoardValid;
  tBoardFan_ = cfg.tBoardFan;

  if (latchReset_ && (int32_t)(now - latchResetEndMs_) >= 0) latchReset_ = false;

  // ---- control-loop health ----
  if (in.maxLoopGapMs > kLoopOverrunMs) {
    loopOverruns_++;
    lastLongGapMs_ = now;
    haveLongGap_ = true;
  }

  // ---- heartbeat running time ----
  if (heartbeatAllowed()) {
    if (!hbOn_) {
      hbOn_ = true;
      hbOnSinceMs_ = now;
    }
  } else {
    hbOn_ = false;
  }
  bool expectSafe = hbOn_ && (now - hbOnSinceMs_ >= kSafeGraceMs);

  // ---- SAFE_RB debounce: low after 5 ms, high after 50 ms stable ----
  if (in.safeRb) {
    safeLowPending_ = false;
    if (!safeStable_) {
      if (!safeHighPending_) {
        safeHighPending_ = true;
        safeHighSinceMs_ = now;
      } else if (now - safeHighSinceMs_ >= kSafeHighStableMs) {
        safeStable_ = true;
        safeHighPending_ = false;
      }
    }
  } else {
    safeHighPending_ = false;
    if (safeStable_) {
      if (!safeLowPending_) {
        safeLowPending_ = true;
        safeLowSinceMs_ = now;
      } else if (now - safeLowSinceMs_ >= kSafeDebounceMs) {
        safeStable_ = false;
        safeLowPending_ = false;
        if (armed_ && expectSafe) {
          // SAFE_RB dropped although the firmware expected it high.
          safeLossCount_++;
          const char *why;
          if (in.vinValid && (in.vin < kRailUvTrip + 0.07f || in.vin > kRailOvTrip - 0.05f)) why = "VIN out of range";
          else if (in.tBoardValid && in.tBoard > 70.0f) why = "board over-temperature";
          else if (haveLongGap_ && now - lastLongGapMs_ < 1000) why = "heartbeat (control loop stalled)";
          else why = "E-STOP pressed or watchdog/unknown";
          // Keep the hardware off until a human acknowledges: stop the heartbeat
          // so SAFE_EN cannot come back by itself (e.g. on E-STOP release).
          hbKilled_ = true;
          latch(BoardAlarm::SafeLost, why, true);
        }
      }
    }
  }
  if (safeStable_ && expectSafe) armed_ = true;

  // ---- VIN window ----
  bool vinBad = !in.vinValid || in.vin < cfg.vinMin || in.vin > cfg.vinMax;
  if (now - startMs_ < kStartupGraceMs) vinBad = false;
  if (vinBad) {
    if (!vinBadTiming_) {
      vinBadTiming_ = true;
      vinBadSinceMs_ = now;
    } else if (now - vinBadSinceMs_ >= kVinDebounceMs && !vinBadNow_) {
      vinBadNow_ = true;
      char t[48];
      if (in.vinValid) snprintf(t, sizeof(t), "VIN %.2f V outside %.2f..%.2f V", (double)in.vin, (double)cfg.vinMin, (double)cfg.vinMax);
      else snprintf(t, sizeof(t), "VIN reading invalid");
      latch(BoardAlarm::Vin, t, true);
    }
  } else {
    vinBadTiming_ = false;
    vinBadNow_ = false;
  }

  // ---- board temperature ----
  if (!in.tBoardValid) {
    if (!tbInvalidTiming_) {
      tbInvalidTiming_ = true;
      tbInvalidSinceMs_ = now;
    } else if (now - tbInvalidSinceMs_ >= kTbInvalidMs) {
      tbInvalid_ = true;
    }
  } else {
    tbInvalidTiming_ = false;
    tbInvalid_ = false;
  }
  if (tbInvalid_) {
    boardHot_ = true;  // unknown temperature: derate
    isetHot_ = true;
  } else if (in.tBoardValid) {
    if (in.tBoard > cfg.tBoardMax) boardHot_ = true;
    else if (in.tBoard < cfg.tBoardMax - kBoardHotHyst) boardHot_ = false;
    if (in.tBoard >= kIsetHotTempC) isetHot_ = true;
    else if (in.tBoard < kIsetHotTempC - kIsetHotHyst) isetHot_ = false;
    if (in.tBoard > cfg.tBoardFan) fanHot_ = true;
    else if (in.tBoard < cfg.tBoardFan - kFanHyst) fanHot_ = false;
    if (in.tBoard > cfg.tBoardMax + kBoardAbortMargin && alarm_ != BoardAlarm::BoardOvertemp) {
      char t[40];
      snprintf(t, sizeof(t), "board %.1f C", (double)in.tBoard);
      latch(BoardAlarm::BoardOvertemp, t, true);
    }
  }

  // ---- informational condition ----
  if (maintenance_) setCond("maintenance (OTA) - outputs off");
  else if (latchReset_) setCond("releasing OV latches (heartbeat paused)");
  else if (!safeStable_ && heartbeatAllowed() && now - startMs_ > kStartupSafeTimeoutMs)
    setCond("SAFE_EN low (E-STOP? VIN? temperature?)");
  else if (tbInvalid_) setCond("board temperature sensor invalid - derated");
  else if (boardHot_) setCond("board hot - discharges paused");
  else if (isetHot_) setCond("board > 50 C - ISET limited to 5");
  else if (in.vinValid && (in.vin < 5.00f - 0.05f || in.vin > 5.25f + 0.05f))
    setCond("+5V rail outside 5.00..5.25 V (set it to 5.20 V)");
  else setCond("");
}

}  // namespace lfp8
