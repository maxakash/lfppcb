// ESP32-C3 implementation of the HAL (Arduino-ESP32 core 3.x).
#pragma once
#include "hal.h"

namespace lfp8 {

class Esp32Hal : public Hal {
 public:
  // Configure GPIOs (shift register lines low, heartbeat low, inputs), I2C at
  // 400 kHz with a short timeout, ADC attenuation and the fan PWM.
  void begin();
  uint32_t millis() override;
  uint32_t micros() override;
  void shiftWrite(uint32_t word) override;
  bool adsWrite(uint8_t reg, uint16_t value) override;
  bool adsRead(uint8_t reg, uint16_t &value) override;
  int32_t adcReadMv(AdcPin pin) override;
  void setHeartbeat(bool level) override;
  bool safeRb() override;
  bool bootButton() override;
  void setFanPct(uint8_t pct) override;
};

}  // namespace lfp8
