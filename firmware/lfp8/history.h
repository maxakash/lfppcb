// V/I history ring buffer: one averaged sample per 30 s per channel, 24 h
// deep (2880 samples x 8 channels x 4 bytes = 92 KB of RAM).
#pragma once
#include <stdint.h>

#include "lfp8_config.h"

namespace lfp8 {

struct HistSample {
  int16_t mv;  // cell voltage in mV, kHistNoData if no valid sample in the interval
  int16_t ma;  // current in mA (> 0 charging)
};
constexpr int16_t kHistNoData = -32768;

class History {
 public:
  void reset(uint32_t nowMs);
  // Add a measurement to the running average of the current interval.
  void accumulate(int ch0, float v, float i);
  // Close the interval when 30 s have elapsed. Returns true if a sample was pushed.
  bool tick(uint32_t nowMs);
  int count() const { return count_; }
  uint32_t total() const { return total_; }        // samples pushed since boot
  uint32_t lastPushMs() const { return lastPushMs_; }
  // Copy n samples of channel ch0 starting at index 'from' (0 = oldest).
  int copy(int ch0, int from, int n, HistSample *out) const;
  // Same, addressed by absolute sample number (0 = first sample since boot) so
  // a reader that copies in several chunks is not confused by new pushes.
  // Samples that were already overwritten are skipped; *first receives the
  // absolute number of out[0].
  int copyAbs(int ch0, uint32_t absFrom, int n, HistSample *out, uint32_t *first) const;
  uint32_t oldestAbs() const { return total_ - (uint32_t)count_; }

 private:
  HistSample buf_[kNumCh][kHistLen];
  int head_ = 0;   // next write position
  int count_ = 0;
  uint32_t total_ = 0;
  uint32_t lastPushMs_ = 0;
  float sumV_[kNumCh] = {0};
  float sumI_[kNumCh] = {0};
  uint16_t n_[kNumCh] = {0};
};

}  // namespace lfp8
