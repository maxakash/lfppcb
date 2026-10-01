// Pack builder: groups tested cells into S series groups of P parallel cells
// with balanced group capacity (primary) and similar group IR (secondary).
//
// 1. select S*P cells (highest capacity, or the tightest capacity window),
// 2. snake draft by capacity,
// 3. best-improvement pairwise swaps between groups minimising
//    sum((cap_g - mean)/mean)^2 + w * sum((R_g - meanR)/meanR)^2,
//    where R_g is the parallel resistance of the group.
#pragma once
#include <stdint.h>

namespace lfp8 {

constexpr int kPackMaxCells = 128;
constexpr int kPackMaxS = 32;
constexpr int kPackMaxP = 64;

struct PackCell {
  int id;        // global channel number (or any user id)
  float capMah;  // measured capacity
  float irMohm;  // DC-IR, <= 0 if unknown (treated as the median)
};

enum class PackSelect : uint8_t { Top, Tight };

struct PackResult {
  bool ok = false;
  const char *err = "";
  int s = 0, p = 0;
  int group[kPackMaxCells];   // per input cell: group index 0..S-1, or -1 = unused
  float groupCap[kPackMaxS];  // mAh (sum)
  float groupIr[kPackMaxS];   // mOhm (parallel)
  float capSpreadPct = 0;     // (max-min)/mean of group capacities
  float irSpreadPct = 0;
  float packCapMah = 0;       // weakest group
  float packIrMohm = 0;       // sum of group resistances
  int swaps = 0;
};

bool packBuild(const PackCell *cells, int n, int S, int P, PackSelect sel, float irWeight, PackResult &r);

}  // namespace lfp8
