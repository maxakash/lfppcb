// ADC conversions (INTERFACE.md §5) and ADS1115 config-word builder.
#pragma once
#include <stdint.h>

namespace lfp8 {

namespace ads {
constexpr uint8_t kRegConv = 0x00;
constexpr uint8_t kRegConfig = 0x01;
constexpr uint16_t kOsBit = 0x8000;  // write: start single conversion, read: 1 = idle/done

enum Mux : uint8_t {
  kAin0Ain1 = 0,  // cell voltage  (B+S - B-S)
  kAin0Ain3 = 1,
  kAin1Ain3 = 2,  // B-S vs shunt bottom (diagnostic)
  kAin2Ain3 = 3,  // shunt (current)
  kAin0Gnd = 4,   // B+S single ended (cross-check against ESP32 IO1)
  kAin1Gnd = 5,
  kAin2Gnd = 6,
  kAin3Gnd = 7,
};
enum Pga : uint8_t { kFs6144 = 0, kFs4096 = 1, kFs2048 = 2, kFs1024 = 3, kFs512 = 4, kFs256 = 5 };
enum Rate : uint8_t {
  kSps8 = 0, kSps16 = 1, kSps32 = 2, kSps64 = 3, kSps128 = 4, kSps250 = 5, kSps475 = 6, kSps860 = 7
};

// Single-shot, OS=1 (start), comparator disabled (COMP_QUE=11).
uint16_t configWord(Mux m, Pga p, Rate r);
// Full-scale voltage of a PGA setting (V).
float fullScale(Pga p);
// Nominal conversion period in microseconds.
uint32_t periodUs(Rate r);
// Time to wait before the first poll of the OS bit (nominal + 2 %).
uint32_t waitUs(Rate r);
}  // namespace ads

// ---- ADS1115 code -> physical -----------------------------------------------
// V_cell = code * 4.096/32768 * CAL_V
float adsCodeToCellV(int16_t code, float calV);
// I_cell = code * 0.256/32768 / R_SHUNT * CAL_I   (> 0 charging, < 0 discharging)
float adsCodeToCellI(int16_t code, float calI);
// Generic: code * FS / 32768
float adsCodeToVolts(int16_t code, ads::Pga pga);
// True if the code is at a rail (saturated / implausible).
bool adsCodeSaturated(int16_t code);

// ---- ESP32 ADC (millivolts in) ----------------------------------------------
enum class NtcStatus : uint8_t { Ok = 0, Open = 1, Short = 2, Invalid = 3 };
// IO1: V_B+S = 2 * V
float vchkToBplus(float adcVolts);
// IO3: V_IN = V / 0.3197
float vinFromAdc(float adcVolts);
// IO0: R_ntc = 10k * V / (3.3 - V); returns <0 if not computable
float ntcResistance(float adcVolts);
// IO0 -> temperature (Beta model, B=3950, R25=10k).
// V > 3.2 V => no NTC fitted (Open); V < 0.10 V => Short; T < -20 C is treated
// as Open as well (ESP32-C3 ADC saturates below 3.3 V, see README).
float ntcTempC(float adcVolts, NtcStatus &status);
// IO4: T = 25 + (V - V25) / (-0.0021)
float boardTempC(float adcVolts, float v25);
// Board temperature diode reading plausible?
bool boardDiodePlausible(float adcVolts);
// Charge-current set-point for an ISET code, scaled by the +5V rail (V_IN/5.20).
float isetNominalA(uint8_t iset, float vin);
// Passive ceiling of the linear charger: (V_IN - 0.38 V - V_cell) / 1.5 ohm, >= 0.
float chargeCeilingA(float vin, float vcell);
// Expected CC current = min(set-point, ceiling).
float chargeExpectedA(uint8_t iset, float vin, float vcell);

}  // namespace lfp8
