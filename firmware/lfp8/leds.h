// Channel LED blink codes (green LED_k, one per channel).
//
//   EMPTY          off
//   IDLE           50 ms blip every 2 s
//   REVERSED       4 Hz fast blink
//   CHARGING_PRE   double blink every second
//   CHARGING_CC    1 Hz, 50 %
//   CHARGING_CV    on, short 100 ms gap every second
//   DISCHARGING    2 Hz, 50 %
//   RESTING        100 ms flash every second
//   IR_MEASURE     10 Hz
//   PAUSED         0.5 Hz slow blink (1 s on / 1 s off)
//   DONE           steady on
//   FAULT_*        N blinks (250 ms on / 250 ms off) then 1.5 s off;
//                  N = 2 OV, 3 DEAD_CELL, 4 TIMEOUT, 5 OVERTEMP, 6 ADC,
//                  7 CURRENT, 8 SAFETY
//   board alarm    all LEDs blink together at 2 Hz (only visible while
//                  SAFE_EN is high - otherwise the 74HC595 is high-Z)
//   identify       all LEDs 5 Hz for 10 s
#pragma once
#include <stdint.h>

#include "channel.h"

namespace lfp8 {

bool ledPattern(ChState st, uint32_t ms);

}  // namespace lfp8
