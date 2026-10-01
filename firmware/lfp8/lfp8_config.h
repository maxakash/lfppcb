// LFP-8 firmware - fixed board constants, pin map and timing.
// Source of truth: docs/INTERFACE.md (sections referenced below).
// Pure C++ (no Arduino headers) - shared by the ESP32 build and host tests.
#pragma once
#include <stdint.h>

#define LFP8_FW_VERSION "1.0.0"

namespace lfp8 {

constexpr int kNumCh = 8;  // channels per board

// ---- §8 fixed board constants ---------------------------------------------
constexpr float kRShunt = 0.100f;        // ohm, current shunt
constexpr float kRChgBallast = 1.2f;     // ohm, charge path (also the CC sense resistor)
constexpr float kRLoad = 2.75f;          // ohm, discharge load (4 x 11R)
constexpr float kDivVchk = 0.5f;         // ADC_VCHK = B+S * 0.5
constexpr float kDivVin = 0.3197f;       // ADC_VIN = +5V * 0.3197
constexpr float kNtcPullup = 10000.0f;   // ohm to 3.3 V
constexpr float kNtcR25 = 10000.0f;      // ohm
constexpr float kNtcBeta = 3950.0f;      // K
constexpr float kNtcSupply = 3.3f;       // V
constexpr float kVref = 2.495f;          // V
constexpr float kCvSetNom = 3.576f;      // V, hardware CV (nominal; 3.51..3.62 with tolerances)
constexpr float kOvTrip = 3.82f;         // V, hardware OV latch (3.78..3.87), releases < 2.74 V or SAFE_EN low
constexpr float kOvRelease = 2.74f;
constexpr float kUvTrip = 2.22f;         // V, hardware UV backstop under load (DIS_EN=1)
constexpr float kUvRearm = 2.59f;        // V, the load cannot (re)start below this
constexpr float kDisStartMinV = 2.62f;   // refuse discharge starts below (load would not start)
constexpr float kTbrdSlope = -0.00197f;  // V/K, MMBT3904 Vbe at ~100 uA (§8)
constexpr float kTbrdV25Default = 0.601f;// V at 25 C (§8)

// §4 charge-current DAC: set-point at a 5.20 V rail, scales with V_IN/5.20
constexpr float kIsetLutA[8] = {0.0f, 0.265f, 0.532f, 0.797f, 1.064f, 1.328f, 1.595f, 1.860f};
constexpr float kIsetRefRail = 5.20f;
// §4 passive ceiling of the linear charger: I_max = (V_IN - 0.38 V - V_cell) / 1.5 ohm
constexpr float kChgCeilDrop = 0.38f;
constexpr float kChgCeilR = 1.5f;
constexpr float kRailNominal = 5.20f;   // recommended +5V setting (5.00..5.25 allowed)
constexpr float kRailUvTrip = 4.48f;    // hardware rail UV
constexpr float kRailOvTrip = 5.50f;    // hardware rail OV
// §4 thermal policy of the SOT-23 pass FET
constexpr uint32_t kPulse50PeriodMs = 1000;  // 2.80..3.05 V: 50 % duty, 1 s period
constexpr uint32_t kPre20OnMs = 1000;        // 2.00..2.80 V: 20 % duty (1 s on / 4 s off)
constexpr uint32_t kPre20OffMs = 4000;
constexpr uint8_t kIsetDefault = 4;
constexpr uint8_t kIsetHotLimit = 5;     // ISET >= 6 only when board < 50 C
constexpr float kIsetHotTempC = 50.0f;
constexpr uint8_t kIsetIrPulse = 4;      // §7.1 charge-pulse fallback uses ISET 4

// ---- §2 GPIO map ----------------------------------------------------------
constexpr int kPinAdcNtc = 0;    // IO0  ADC1_CH0 mux D (cell NTC)
constexpr int kPinAdcVchk = 1;   // IO1  ADC1_CH1 mux A / 2
constexpr int kPinRclk = 2;      // IO2  74HC595 latch (strap)
constexpr int kPinAdcVin = 3;    // IO3  +5V * 0.3197
constexpr int kPinAdcTbrd = 4;   // IO4  board temperature (Vbe)
constexpr int kPinSda = 5;       // IO5
constexpr int kPinScl = 6;       // IO6
constexpr int kPinSer = 7;       // IO7  74HC595 serial data
constexpr int kPinSrclk = 8;     // IO8  74HC595 shift clock (strap)
constexpr int kPinBoot = 9;      // IO9  BOOT button, active low
constexpr int kPinHeartbeat = 10;// IO10 watchdog charge pump - software toggled only
constexpr int kPinSafeRb = 20;   // IO20 SAFE_EN read-back
constexpr int kPinFan = 21;      // IO21 fan MOSFET gate
constexpr uint8_t kAdsAddr = 0x48;

// ---- measurement timing ---------------------------------------------------
constexpr uint32_t kMuxSettleUs = 2500;     // >= 2 ms after mux change (§5)
constexpr uint32_t kVchkDwellUs = 26000;    // IO1 divider 1M/1M + 10 nF (tau 5 ms): >= 25 ms on the channel
constexpr uint32_t kSrRefreshMs = 20;       // periodic shift-register refresh
constexpr uint32_t kStaleSampleMs = 3000;   // active channel w/o valid sample -> ADC fault
constexpr uint32_t kSafeGraceMs = 300;      // heartbeat running this long -> SAFE_RB expected
constexpr uint32_t kSafeDebounceMs = 1;     // SAFE_RB low on 2 consecutive reads >= 1 ms apart
// hardware: heartbeat >= 100 Hz (toggle <= 5 ms), trips 23..33 ms after it stops,
// SAFE_EN low ~50 ms after power-up (VREF settling)
constexpr uint32_t kStartupSafeTimeoutMs = 2000;
constexpr uint32_t kOutputSettleMs = 300;   // after outputs off before "stray current" check
constexpr float kStrayCurrentA = 0.10f;
constexpr uint8_t kStrayCount = 3;
constexpr uint8_t kXchkFailCount = 3;       // consecutive cross-check failures -> fault
constexpr uint8_t kI2cFailBoard = 5;        // consecutive I2C failures -> board fault
constexpr float kIAbsMaxA = 2.2f;           // implausible |I| (PTC hold 2 A, charger <= 1.7 A)
constexpr float kVPresentMin = 0.5f;        // §5: below -> EMPTY (with no current)
constexpr float kVReversed = -0.5f;         // below -> REVERSED
constexpr float kIEmptyMax = 0.05f;
constexpr float kXchkMinCellV = 1.0f;       // cross-check only when a cell is present
// B- diagnostic (§5, AIN1-AIN3 = I * (R_bminus + 2 x 28 mOhm MOSFETs + 0.1 ohm shunt))
constexpr float kRbmFixed = 0.156f;         // ohm: MOSFETs + shunt
constexpr float kContactWarnOhm = 0.30f;    // implied B- force-path resistance -> CONTACT_WARN
constexpr float kContactMinI = 0.20f;       // A, needed for a meaningful estimate
// OV latch (§6)
constexpr float kOvLatchDetectV = 3.75f;    // CHG_EN=1, I ~ 0 and V >= this -> latch tripped
constexpr uint32_t kLatchResetMs = 120;     // heartbeat stop to release all latches (>= 100 ms)

// ---- IR (§7.1) ------------------------------------------------------------
constexpr int kIrBaseSamples = 16;          // V0/I0 average
constexpr float kIrTOhmic = 0.010f;         // s
constexpr float kIrTDc = 1.000f;            // s
constexpr float kIrPulseEnd = 1.012f;       // s, keep pulse until both V and I bracket 1 s
constexpr float kIrUvBlock = 2.65f;         // below: discharge pulse blocked by UV backstop
constexpr float kIrMinDeltaI = 0.30f;       // |I(t)-I0| below this -> pulse failed
constexpr uint32_t kIrMaxDurationMs = 2500; // engine watchdog
constexpr int kIrMaxSamples = 1100;

// ---- networking (§9) --------------------------------------------------------
constexpr uint16_t kHelloPort = 45454;
constexpr uint32_t kHelloPeriodMs = 2000;
constexpr uint32_t kPeerTimeoutMs = 10000;
constexpr int kMaxPeers = 16;

// ---- history ring -----------------------------------------------------------
constexpr uint32_t kHistIntervalMs = 30000;  // 1 sample / 30 s
constexpr int kHistLen = 2880;               // 24 h per channel

}  // namespace lfp8
