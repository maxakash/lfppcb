#include "leds.h"

namespace lfp8 {

bool ledPattern(ChState st, uint32_t ms) {
  uint32_t p1000 = ms % 1000;
  switch (st) {
    case ChState::Empty: return false;
    case ChState::Idle: return (ms % 2000) < 50;
    case ChState::Reversed: return (ms % 250) < 125;
    case ChState::ChargingPre: return p1000 < 100 || (p1000 >= 200 && p1000 < 300);
    case ChState::ChargingCc: return p1000 < 500;
    case ChState::ChargingCv: return p1000 < 900;
    case ChState::Discharging: return (ms % 500) < 250;
    case ChState::Resting: return p1000 < 100;
    case ChState::IrMeasure: return (ms % 100) < 50;
    case ChState::Paused: return (ms % 2000) < 1000;
    case ChState::Done: return true;
    default: break;
  }
  uint8_t n = chFaultBlinks(st);
  if (n == 0) return false;
  uint32_t period = (uint32_t)n * 500u + 1500u;
  uint32_t ph = ms % period;
  if (ph >= (uint32_t)n * 500u) return false;
  return (ph % 500u) < 250u;
}

}  // namespace lfp8
