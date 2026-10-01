// Hardware abstraction layer. The pure-C++ core only talks to the board
// through this interface; hal_esp32.cpp implements it on the ESP32-C3 and
// test/sim_board.cpp implements a simulated board for host tests.
#pragma once
#include <stdint.h>

namespace lfp8 {

enum class AdcPin : uint8_t { Ntc = 0, Vchk = 1, Vin = 3, Tbrd = 4 };

class Hal {
 public:
  virtual ~Hal() {}
  // Monotonic time (wrap-around safe arithmetic is used everywhere).
  virtual uint32_t millis() = 0;
  virtual uint32_t micros() = 0;
  // 74HC595 chain: shift 32 bits MSB first, then pulse RCLK (bit i -> position i).
  virtual void shiftWrite(uint32_t word) = 0;
  // ADS1115 register access (I2C 0x48). Return false on any bus error.
  virtual bool adsWrite(uint8_t reg, uint16_t value) = 0;
  virtual bool adsRead(uint8_t reg, uint16_t &value) = 0;
  // ESP32 ADC in millivolts (analogReadMilliVolts, 11 dB). Negative = error.
  virtual int32_t adcReadMv(AdcPin pin) = 0;
  // GPIO
  virtual void setHeartbeat(bool level) = 0;  // IO10, toggled by the control loop only
  virtual bool safeRb() = 0;                  // IO20, true = hardware allows power
  virtual bool bootButton() = 0;              // IO9, true = pressed
  virtual void setFanPct(uint8_t pct) = 0;    // IO21
};

}  // namespace lfp8
