#include <math.h>

#include "../lfp8/packbuilder.h"
#include "test_framework.h"

using namespace lfp8;

namespace {
uint32_t g_seed = 1;
double rnd() {
  g_seed = g_seed * 1664525u + 1013904223u;
  return (double)(g_seed >> 8) / 16777216.0;
}
void makeCells(PackCell *c, int n, uint32_t seed) {
  g_seed = seed;
  for (int k = 0; k < n; k++) {
    c[k].id = k + 1;
    c[k].capMah = (float)(13800.0 + 2000.0 * rnd());  // 13.8 .. 15.8 Ah
    c[k].irMohm = (float)(6.0 + 10.0 * rnd());       // 6 .. 16 mOhm
  }
}
void checkPartition(const PackResult &r, int n, int S, int P) {
  int cnt[kPackMaxS] = {0}, used = 0;
  for (int k = 0; k < n; k++) {
    if (r.group[k] >= 0) {
      CHECK(r.group[k] < S);
      cnt[r.group[k]]++;
      used++;
    }
  }
  CHECK_EQ(used, S * P);
  for (int g = 0; g < S; g++) CHECK_EQ(cnt[g], P);
}
}  // namespace

TEST(pack_40_random_cells_4s10p_spread_below_1pct) {
  PackCell cells[40];
  for (uint32_t seed = 1; seed <= 20; seed++) {  // many random sets
    makeCells(cells, 40, seed);
    PackResult r;
    CHECK(packBuild(cells, 40, 4, 10, PackSelect::Top, 0.1f, r));
    checkPartition(r, 40, 4, 10);
    CHECK_MSG(r.capSpreadPct < 1.0, "seed %u spread %.3f %%", seed, r.capSpreadPct);
    double sum = 0, mn = 1e9;
    for (int g = 0; g < 4; g++) {
      sum += r.groupCap[g];
      mn = fmin(mn, r.groupCap[g]);
    }
    CHECK_NEAR(r.packCapMah, mn, 1e-3);
    double all = 0;
    for (int k = 0; k < 40; k++) all += cells[k].capMah;
    CHECK_NEAR(sum, all, 1.0);
    if (seed == 1) printf("    4S10P: capacity spread %.3f %%, IR spread %.2f %%, %d swaps\n", r.capSpreadPct, r.irSpreadPct, r.swaps);
  }
}

TEST(pack_40_cells_8s5p_and_10s4p) {
  PackCell cells[40];
  makeCells(cells, 40, 99);
  PackResult r;
  CHECK(packBuild(cells, 40, 8, 5, PackSelect::Top, 0.1f, r));
  checkPartition(r, 40, 8, 5);
  CHECK(r.capSpreadPct < 1.0);
  printf("    8S5P: capacity spread %.3f %%, IR spread %.2f %%\n", r.capSpreadPct, r.irSpreadPct);
  CHECK(packBuild(cells, 40, 10, 4, PackSelect::Top, 0.1f, r));
  checkPartition(r, 40, 10, 4);
  CHECK(r.capSpreadPct < 1.5);
  // group IR balancing does not destroy capacity balance
  CHECK(packBuild(cells, 40, 4, 10, PackSelect::Top, 1.0f, r));
  CHECK(r.capSpreadPct < 1.0);
}

TEST(pack_selection_and_errors) {
  PackCell cells[45];
  makeCells(cells, 45, 5);
  PackResult r;
  CHECK(packBuild(cells, 45, 4, 10, PackSelect::Top, 0.1f, r));
  checkPartition(r, 45, 4, 10);
  // Top: the 5 unused cells are the 5 lowest capacities
  float minUsed = 1e9f, maxUnused = 0;
  for (int k = 0; k < 45; k++) {
    if (r.group[k] >= 0) minUsed = fminf(minUsed, cells[k].capMah);
    else maxUnused = fmaxf(maxUnused, cells[k].capMah);
  }
  CHECK(maxUnused <= minUsed);
  CHECK(packBuild(cells, 45, 4, 10, PackSelect::Tight, 0.1f, r));
  checkPartition(r, 45, 4, 10);
  CHECK(!packBuild(cells, 45, 5, 10, PackSelect::Top, 0.1f, r));  // 50 > 45
  CHECK(!r.ok);
  CHECK(!packBuild(cells, 45, 0, 10, PackSelect::Top, 0.1f, r));
  cells[3].capMah = 0;  // untested cell
  CHECK(!packBuild(cells, 45, 4, 10, PackSelect::Top, 0.1f, r));
  // unknown IR is tolerated
  makeCells(cells, 40, 11);
  for (int k = 0; k < 40; k += 3) cells[k].irMohm = 0;
  CHECK(packBuild(cells, 40, 4, 10, PackSelect::Top, 0.1f, r));
  CHECK(r.capSpreadPct < 1.0);
}
