// 32-bit 74HC595 word builder (INTERFACE.md §3). Bit i of the word appears
// on output position i. CHG_EN_k = 3(k-1), DIS_EN_k = 3(k-1)+1,
// LED_k = 3(k-1)+2, MUX S0..S2 = 24..26, ISET B0..B2 = 27..29, AUX = 30,
// bit 31 unused (always 0).
#pragma once
#include <stdint.h>

#include "lfp8_config.h"

namespace lfp8 {

struct SrOutputs {
  bool chg[kNumCh];
  bool dis[kNumCh];
  bool led[kNumCh];
  uint8_t muxCh;  // selected channel 1..8 (0 is treated as 1)
  uint8_t iset;   // 0..7
  bool aux;
};

void srClear(SrOutputs &o);

constexpr uint32_t srBitChg(int k) { return 1u << (3 * (k - 1)); }
constexpr uint32_t srBitDis(int k) { return 1u << (3 * (k - 1) + 1); }
constexpr uint32_t srBitLed(int k) { return 1u << (3 * (k - 1) + 2); }
constexpr int kSrMuxShift = 24;
constexpr int kSrIsetShift = 27;
constexpr uint32_t kSrAuxBit = 1u << 30;

// Build the word. If CHG and DIS are both requested for a channel, BOTH are
// forced off (interlock) and the channel's bit is set in *interlockMask.
uint32_t srBuild(const SrOutputs &o, uint8_t *interlockMask);
// Decode a word back into outputs (used by the simulator and tests).
void srDecode(uint32_t word, SrOutputs &o);
// Mask of all CHG/DIS bits.
uint32_t srPowerBits();

}  // namespace lfp8
