// Simulated LFP-8 board for host tests: implements lfp8::Hal.
//  * decodes the 74HC595 word (CHG/DIS/LED/MUX/ISET) exactly as the hardware,
//  * emulates the ADS1115 register interface (single-shot, OS bit, MUX, PGA,
//    data rate; the result is latched at the middle of the conversion),
//  * models 8 LiFePO4 cells (OCV(SoC) table + R0 + R1||C), the CC/CV
//    charger with ISET DAC and hardware CV limit, the 2.75 ohm load with the
//    UV backstop, the OV latch, reversed/empty cells and cell NTCs,
//  * models the SAFE_EN chain: heartbeat charge pump (trips after 70 ms
//    without a toggle), VIN window, board temperature, E-STOP. While SAFE_EN
//    is low the 595 outputs are high-Z (all power off, mux reads channel 1).
#pragma once
#include <stdint.h>

#include "../lfp8/hal.h"
#include "../lfp8/lfp8_config.h"

struct SimCell {
  bool present = true;
  bool reversed = false;
  double qAh = 15.0;     // true capacity
  double soc = 0.5;      // 0..1 (may go slightly outside)
  double r0 = 0.008;     // ohm
  double r1 = 0.004;     // ohm
  double tau = 0.5;      // s
  double vrc = 0;        // RC polarisation voltage
  double tempC = 25.0;
  bool ntcFitted = true;
  bool ntcShort = false;
  double cvSet = 3.576;  // hardware CV limit (Kelvin), 3.51..3.62 with tolerances
  double contactR = 0.03;// B- force path (wire + contact) resistance, ohm
  double isetScale = 1.0;// charger DAC tolerance
  bool ovLatched = false;
  bool uvTripped = false;
  double i = 0;          // A, > 0 charging
  bool disWasOn = false;
  // thermal-policy monitor: continuous CHG_EN time while OCV < 3.00 V
  double contOnLowS = 0, maxContOnLowS = 0;
  double ahIn = 0, ahOut = 0;   // ground truth (Ah)
  double whIn = 0, whOut = 0;
  uint64_t expDtUs = 0;  // cache for the RC decay factor
  double expTau = 0, expFac = 0;

  double ocv() const;
  double vTerm() const { return ocv() + vrc + i * r0; }
};

double lfpOcv(double soc);

class SimBoard : public lfp8::Hal {
 public:
  SimBoard();
  // ---- Hal ----
  uint32_t millis() override { return (uint32_t)(nowUs / 1000u); }
  uint32_t micros() override { return (uint32_t)nowUs; }
  void shiftWrite(uint32_t word) override;
  bool adsWrite(uint8_t reg, uint16_t value) override;
  bool adsRead(uint8_t reg, uint16_t &value) override;
  int32_t adcReadMv(lfp8::AdcPin pin) override;
  void setHeartbeat(bool level) override;
  bool safeRb() override { return safeEn(); }
  bool bootButton() override { return false; }
  void setFanPct(uint8_t pct) override { fanPct = pct; }

  // ---- simulation ----
  void advance(uint64_t dtUs);  // physics
  bool safeEn() const;
  bool chgOn(int k) const;  // effective (after SAFE gating)
  bool disOn(int k) const;
  uint8_t muxCh() const;

  uint64_t nowUs = 0;
  SimCell cell[lfp8::kNumCh];
  double vin = 5.20;
  double tBoard = 25.0;
  bool estop = false;
  bool adsPresent = true;
  int i2cFailNext = 0;        // fail the next N I2C transactions
  bool i2cFailAll = false;
  double vchkErrV[lfp8::kNumCh] = {0};   // error added to the ESP32 IO1 reading (B+ volts)
  double strayA[lfp8::kNumCh] = {0};     // current that flows regardless of outputs (stuck MOSFET)
  uint32_t noiseLsb = 0;      // +-N LSB pseudo-random noise on ADS codes

  // ---- observation ----
  uint32_t word = 0;
  uint64_t interlockViolations = 0;
  uint64_t hbToggles = 0;
  uint64_t lastToggleUs = 0;
  uint64_t hbRunningSinceUs = 0;
  uint64_t lastIntervalUs = 0;
  double emaIntervalUs = 1000.0;
  double vchkNode = 0;       // IO1 node (1M/1M + 10 nF, tau 5 ms)
  uint64_t maxToggleGapUs = 0;
  bool hbLevel = false;
  uint8_t fanPct = 0;
  uint64_t shiftWrites = 0;
  uint64_t adsTransactions = 0;

 private:
  bool i2cOk();
  void latchConversion();
  int16_t toCode(double volts, uint8_t pga);
  double bMinusAbs(int k) const;  // B- sense vs GND
  double bPlusAbs(int k) const;
  double cellV(int k) const;      // AIN0-AIN1
  uint16_t adsCfg_ = 0x8583;
  bool convPending_ = false;
  bool convLatched_ = false;
  uint64_t convStartUs_ = 0;
  uint32_t convPeriodUs_ = 0;
  int16_t convResult_ = 0;
  uint32_t rng_ = 12345;
};
