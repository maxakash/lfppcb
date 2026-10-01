// ESP32-C3 HAL. Only compiled by the Arduino build (host tests use sim_board).
#if defined(ARDUINO)
#include "hal_esp32.h"

#include <Arduino.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <esp_rom_sys.h>

#include "lfp8_config.h"

namespace lfp8 {

namespace {
constexpr uint32_t kI2cHz = 400000;
constexpr uint16_t kI2cTimeoutMs = 10;  // keep a hung bus from stalling the heartbeat
constexpr uint32_t kFanPwmHz = 25000;
constexpr uint8_t kFanPwmBits = 8;

inline void pinSet(int pin, bool v) { gpio_set_level((gpio_num_t)pin, v ? 1 : 0); }
}  // namespace

void Esp32Hal::begin() {
  // Heartbeat low first (no edges until the controller is happy).
  pinMode(kPinHeartbeat, OUTPUT);
  pinSet(kPinHeartbeat, false);
  // Shift-register lines. IO2/IO8 are boot straps held high by the level
  // shifters until now; from here on they are driven by firmware.
  pinMode(kPinSer, OUTPUT);
  pinMode(kPinSrclk, OUTPUT);
  pinMode(kPinRclk, OUTPUT);
  pinSet(kPinSer, false);
  pinSet(kPinSrclk, false);
  pinSet(kPinRclk, false);
  shiftWrite(0);  // all outputs off
  // Inputs
  pinMode(kPinSafeRb, INPUT);
  pinMode(kPinBoot, INPUT_PULLUP);
  // ADC: 12 bit, 11 dB attenuation on all analog inputs
  analogReadResolution(12);
  analogSetPinAttenuation(kPinAdcNtc, ADC_11db);
  analogSetPinAttenuation(kPinAdcVchk, ADC_11db);
  analogSetPinAttenuation(kPinAdcVin, ADC_11db);
  analogSetPinAttenuation(kPinAdcTbrd, ADC_11db);
  // I2C to the ADS1115 (via BSS138 level shifters)
  Wire.begin(kPinSda, kPinScl, kI2cHz);
  Wire.setTimeOut(kI2cTimeoutMs);
  // Fan PWM (LEDC is fine for the fan - never for the heartbeat)
  ledcAttach(kPinFan, kFanPwmHz, kFanPwmBits);
  ledcWrite(kPinFan, 0);
}

uint32_t Esp32Hal::millis() { return ::millis(); }
uint32_t Esp32Hal::micros() { return ::micros(); }

void Esp32Hal::shiftWrite(uint32_t word) {
  // MSB first; ~4 us per bit (< 400 kHz as required by the BSS138 shifters).
  for (int b = 31; b >= 0; b--) {
    pinSet(kPinSer, (word >> b) & 1u);
    esp_rom_delay_us(1);
    pinSet(kPinSrclk, true);  // rising edge shifts
    esp_rom_delay_us(2);
    pinSet(kPinSrclk, false);
    esp_rom_delay_us(1);
  }
  pinSet(kPinRclk, true);  // rising edge latches
  esp_rom_delay_us(2);
  pinSet(kPinRclk, false);
}

bool Esp32Hal::adsWrite(uint8_t reg, uint16_t value) {
  Wire.beginTransmission(kAdsAddr);
  Wire.write(reg);
  Wire.write((uint8_t)(value >> 8));
  Wire.write((uint8_t)(value & 0xFF));
  return Wire.endTransmission(true) == 0;
}

bool Esp32Hal::adsRead(uint8_t reg, uint16_t &value) {
  Wire.beginTransmission(kAdsAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;  // repeated start
  if (Wire.requestFrom((uint16_t)kAdsAddr, (size_t)2, true) != 2) return false;
  int hi = Wire.read();
  int lo = Wire.read();
  if (hi < 0 || lo < 0) return false;
  value = (uint16_t)((hi << 8) | lo);
  return true;
}

int32_t Esp32Hal::adcReadMv(AdcPin pin) {
  int p;
  switch (pin) {
    case AdcPin::Ntc: p = kPinAdcNtc; break;
    case AdcPin::Vchk: p = kPinAdcVchk; break;
    case AdcPin::Vin: p = kPinAdcVin; break;
    case AdcPin::Tbrd: p = kPinAdcTbrd; break;
    default: return -1;
  }
  // Average 4 conversions (the C3 ADC is noisy); ~4 x 15 us.
  uint32_t sum = 0;
  for (int i = 0; i < 4; i++) sum += analogReadMilliVolts(p);
  return (int32_t)(sum / 4);
}

void Esp32Hal::setHeartbeat(bool level) { pinSet(kPinHeartbeat, level); }

bool Esp32Hal::safeRb() { return gpio_get_level((gpio_num_t)kPinSafeRb) != 0; }

bool Esp32Hal::bootButton() { return gpio_get_level((gpio_num_t)kPinBoot) == 0; }

void Esp32Hal::setFanPct(uint8_t pct) {
  if (pct > 100) pct = 100;
  ledcWrite(kPinFan, (uint32_t)pct * ((1u << kFanPwmBits) - 1u) / 100u);
}

}  // namespace lfp8
#endif  // ARDUINO
