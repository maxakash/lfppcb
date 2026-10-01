#include "shiftreg.h"

namespace lfp8 {

void srClear(SrOutputs &o) {
  for (int i = 0; i < kNumCh; i++) {
    o.chg[i] = false;
    o.dis[i] = false;
    o.led[i] = false;
  }
  o.muxCh = 1;
  o.iset = 0;
  o.aux = false;
}

uint32_t srBuild(const SrOutputs &o, uint8_t *interlockMask) {
  uint32_t w = 0;
  uint8_t il = 0;
  for (int k = 1; k <= kNumCh; k++) {
    bool c = o.chg[k - 1];
    bool d = o.dis[k - 1];
    if (c && d) {  // interlock: never both - force both off
      il |= (uint8_t)(1u << (k - 1));
      c = d = false;
    }
    if (c) w |= srBitChg(k);
    if (d) w |= srBitDis(k);
    if (o.led[k - 1]) w |= srBitLed(k);
  }
  uint8_t n = (o.muxCh >= 1 && o.muxCh <= kNumCh) ? (uint8_t)(o.muxCh - 1) : 0;  // n -> channel n+1
  w |= (uint32_t)(n & 7u) << kSrMuxShift;
  uint8_t is = o.iset > 7 ? 7 : o.iset;
  w |= (uint32_t)(is & 7u) << kSrIsetShift;
  if (o.aux) w |= kSrAuxBit;
  w &= 0x7FFFFFFFu;  // bit 31 unused, always 0
  if (interlockMask) *interlockMask = il;
  return w;
}

void srDecode(uint32_t w, SrOutputs &o) {
  for (int k = 1; k <= kNumCh; k++) {
    o.chg[k - 1] = (w & srBitChg(k)) != 0;
    o.dis[k - 1] = (w & srBitDis(k)) != 0;
    o.led[k - 1] = (w & srBitLed(k)) != 0;
  }
  o.muxCh = (uint8_t)(((w >> kSrMuxShift) & 7u) + 1);
  o.iset = (uint8_t)((w >> kSrIsetShift) & 7u);
  o.aux = (w & kSrAuxBit) != 0;
}

uint32_t srPowerBits() {
  uint32_t m = 0;
  for (int k = 1; k <= kNumCh; k++) m |= srBitChg(k) | srBitDis(k);
  return m;
}

}  // namespace lfp8
