#include "history.h"

#include <math.h>

namespace lfp8 {

void History::reset(uint32_t nowMs) {
  head_ = 0;
  count_ = 0;
  total_ = 0;
  lastPushMs_ = nowMs;
  for (int c = 0; c < kNumCh; c++) {
    sumV_[c] = sumI_[c] = 0;
    n_[c] = 0;
  }
}

void History::accumulate(int ch0, float v, float i) {
  if (ch0 < 0 || ch0 >= kNumCh) return;
  sumV_[ch0] += v;
  sumI_[ch0] += i;
  if (n_[ch0] < 65535) n_[ch0]++;
}

static int16_t clamp16(float x) {
  if (x > 32767.0f) return 32767;
  if (x < -32767.0f) return -32767;
  return (int16_t)lroundf(x);
}

bool History::tick(uint32_t nowMs) {
  if (nowMs - lastPushMs_ < kHistIntervalMs) return false;
  lastPushMs_ += kHistIntervalMs;
  if (nowMs - lastPushMs_ >= kHistIntervalMs) lastPushMs_ = nowMs;  // long stall: resync
  for (int c = 0; c < kNumCh; c++) {
    HistSample s;
    if (n_[c] == 0) {
      s.mv = kHistNoData;
      s.ma = 0;
    } else {
      s.mv = clamp16(sumV_[c] / n_[c] * 1000.0f);
      s.ma = clamp16(sumI_[c] / n_[c] * 1000.0f);
    }
    buf_[c][head_] = s;
    sumV_[c] = sumI_[c] = 0;
    n_[c] = 0;
  }
  head_ = (head_ + 1) % kHistLen;
  if (count_ < kHistLen) count_++;
  total_++;
  return true;
}

int History::copy(int ch0, int from, int n, HistSample *out) const {
  if (ch0 < 0 || ch0 >= kNumCh || from < 0 || from >= count_) return 0;
  if (n > count_ - from) n = count_ - from;
  int oldest = (head_ - count_ + kHistLen) % kHistLen;
  for (int j = 0; j < n; j++) out[j] = buf_[ch0][(oldest + from + j) % kHistLen];
  return n;
}

int History::copyAbs(int ch0, uint32_t absFrom, int n, HistSample *out, uint32_t *first) const {
  uint32_t oldest = oldestAbs();
  if (absFrom < oldest) absFrom = oldest;
  if (first) *first = absFrom;
  if (absFrom >= total_) return 0;
  return copy(ch0, (int)(absFrom - oldest), n, out);
}

}  // namespace lfp8
