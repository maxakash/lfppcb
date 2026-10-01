// Board-level safety supervisor (INTERFACE.md §6, §7).
//
// Decides whether the heartbeat may toggle, whether power stages may be
// enabled, whether measurements are meaningful (SAFE_RB high: the 74HC595
// outputs, incl. the mux selects, are only driven while SAFE_EN is high),
// and requests a board-wide abort of all jobs.
//
// Alarm classes:
//  * heartbeat-kill alarms (latched until ack): SAFE_RB lost while expected
//    high, ADC cross-check mismatch, persistent I2C failure, ADS1115 missing,
//    stray current with outputs off, interlock violation. All jobs are
//    aborted and the heartbeat stops, so the hardware keeps every cell
//    isolated until a human acknowledges (ack restarts the heartbeat; if the
//    cause persists the alarm trips again).
//  * abort alarms (latched until ack, heartbeat keeps running): VIN out of
//    the firmware window, board over-temperature.
//  * conditions (not latched): waiting for SAFE_EN at boot, board hot
//    (derating), ISET derating above 50 C.
#pragma once
#include <stdint.h>

#include "settings.h"

namespace lfp8 {

enum class BoardAlarm : uint8_t {
  None,
  SafeLost,
  Vin,
  BoardOvertemp,
  AdcMismatch,
  I2c,
  AdsMissing,
  StrayCurrent,
  Interlock,
};
const char *boardAlarmName(BoardAlarm a);
bool boardAlarmKillsHeartbeat(BoardAlarm a);

struct SupInputs {
  uint32_t nowMs = 0;
  bool safeRb = false;
  bool vinValid = false;
  float vin = 0;
  bool tBoardValid = false;
  float tBoard = 0;
  uint32_t maxLoopGapMs = 0;  // longest control-loop gap since the previous update
};

class Supervisor {
 public:
  void init(uint32_t nowMs);
  void update(const SupInputs &in, const Settings &cfg);
  // Latch a heartbeat-kill alarm (measurement chain / hardware doubt).
  void killHeartbeat(BoardAlarm a, const char *detail, uint32_t nowMs);
  // Acknowledge latched alarms. Returns nullptr on success or a reason.
  const char *ack(uint32_t nowMs);
  // Maintenance (OTA): stop heartbeat intentionally, no alarm.
  void setMaintenance(bool on) { maintenance_ = on; }
  bool maintenance() const { return maintenance_; }
  // §6: stopping the heartbeat for >= 60 ms drops SAFE_EN, which releases
  // every OV latch on the board. Planned, so no alarm is raised.
  void startLatchReset(uint32_t nowMs, uint32_t durMs);
  bool latchResetActive() const { return latchReset_; }

  bool heartbeatAllowed() const { return !hbKilled_ && !maintenance_ && !latchReset_; }
  bool measurementAllowed() const { return safeStable_ && heartbeatAllowed(); }
  bool powerAllowed() const {
    return armed_ && safeStable_ && heartbeatAllowed() && alarm_ == BoardAlarm::None && !vinBadNow_;
  }
  // One-shot: true when all jobs must be aborted now.
  bool takeAbortRequest() {
    bool r = abortReq_;
    abortReq_ = false;
    return r;
  }

  bool safeStable() const { return safeStable_; }
  bool armed() const { return armed_; }
  bool boardHot() const { return boardHot_; }
  uint8_t isetLimit() const { return isetHot_ ? kIsetHotLimit : 7; }
  uint8_t fanPct(bool anyActive) const;
  BoardAlarm alarm() const { return alarm_; }
  const char *alarmText() const { return alarmText_; }
  const char *conditionText() const { return condText_; }
  uint32_t safeLossCount() const { return safeLossCount_; }
  uint32_t loopOverruns() const { return loopOverruns_; }

 private:
  void latch(BoardAlarm a, const char *text, bool abort);
  void setCond(const char *t);

  uint32_t startMs_ = 0;
  bool hbOn_ = false;
  uint32_t hbOnSinceMs_ = 0;
  bool hbKilled_ = false;
  bool maintenance_ = false;
  bool latchReset_ = false;
  uint32_t latchResetEndMs_ = 0;
  bool safeStable_ = false;
  bool armed_ = false;  // SAFE_EN has been seen high with the heartbeat running
  bool safeLowPending_ = false;
  uint32_t safeLowSinceMs_ = 0;
  uint32_t safeHighSinceMs_ = 0;
  bool safeHighPending_ = false;
  bool vinBadNow_ = false;
  uint32_t vinBadSinceMs_ = 0;
  bool vinBadTiming_ = false;
  bool tbInvalidTiming_ = false;
  uint32_t tbInvalidSinceMs_ = 0;
  bool tbInvalid_ = false;
  bool boardHot_ = false;
  bool isetHot_ = false;
  bool fanHot_ = false;
  bool abortReq_ = false;
  uint32_t lastLongGapMs_ = 0;
  bool haveLongGap_ = false;
  uint32_t loopOverruns_ = 0;
  uint32_t safeLossCount_ = 0;
  float lastVin_ = 0, lastTb_ = 0;
  bool lastVinValid_ = false, lastTbValid_ = false;
  float tBoardFan_ = 40.0f;
  BoardAlarm alarm_ = BoardAlarm::None;
  char alarmText_[72] = {0};
  char condText_[48] = {0};
};

}  // namespace lfp8
