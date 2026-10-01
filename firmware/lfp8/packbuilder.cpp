#include "packbuilder.h"

#include <math.h>

namespace lfp8 {

namespace {
void sortIdxByCapDesc(int *idx, int n, const PackCell *c) {
  // insertion sort (n <= 128), stable
  for (int i = 1; i < n; i++) {
    int v = idx[i], j = i - 1;
    while (j >= 0 && c[idx[j]].capMah < c[v].capMah) {
      idx[j + 1] = idx[j];
      j--;
    }
    idx[j + 1] = v;
  }
}
}  // namespace

bool packBuild(const PackCell *cells, int n, int S, int P, PackSelect sel, float irWeight, PackResult &r) {
  r = PackResult();
  r.s = S;
  r.p = P;
  for (int i = 0; i < kPackMaxCells; i++) r.group[i] = -1;
  if (S < 1 || P < 1 || S > kPackMaxS || P > kPackMaxP) {
    r.err = "S or P out of range";
    return false;
  }
  if (n > kPackMaxCells) {
    r.err = "too many cells";
    return false;
  }
  const int N = S * P;
  if (N > n) {
    r.err = "not enough cells for S x P";
    return false;
  }
  for (int i = 0; i < n; i++) {
    if (!(cells[i].capMah > 0)) {
      r.err = "cell without capacity";
      return false;
    }
  }

  // ---- selection ----
  int idx[kPackMaxCells];
  for (int i = 0; i < n; i++) idx[i] = i;
  sortIdxByCapDesc(idx, n, cells);
  int start = 0;
  if (sel == PackSelect::Tight && N < n) {
    float best = 1e30f;
    for (int s0 = 0; s0 + N <= n; s0++) {
      float w = cells[idx[s0]].capMah - cells[idx[s0 + N - 1]].capMah;
      if (w < best) {
        best = w;
        start = s0;
      }
    }
  }
  int sel_[kPackMaxCells];
  for (int i = 0; i < N; i++) sel_[i] = idx[start + i];

  // ---- IR: unknown -> median of known ----
  float irs[kPackMaxCells];
  int nk = 0;
  for (int i = 0; i < N; i++)
    if (cells[sel_[i]].irMohm > 0) irs[nk++] = cells[sel_[i]].irMohm;
  float med = 1.0f;
  if (nk > 0) {
    for (int i = 1; i < nk; i++) {  // sort
      float v = irs[i];
      int j = i - 1;
      while (j >= 0 && irs[j] > v) {
        irs[j + 1] = irs[j];
        j--;
      }
      irs[j + 1] = v;
    }
    med = (nk % 2) ? irs[nk / 2] : 0.5f * (irs[nk / 2 - 1] + irs[nk / 2]);
  }
  float cap[kPackMaxCells], g[kPackMaxCells];  // per selected cell
  int grp[kPackMaxCells];
  for (int i = 0; i < N; i++) {
    cap[i] = cells[sel_[i]].capMah;
    float ir = cells[sel_[i]].irMohm > 0 ? cells[sel_[i]].irMohm : med;
    g[i] = 1.0f / ir;  // conductance
  }

  // ---- snake draft (selection is sorted by capacity, descending) ----
  for (int i = 0; i < N; i++) {
    int row = i / S, col = i % S;
    grp[i] = (row % 2 == 0) ? col : (S - 1 - col);
  }

  double gc[kPackMaxS] = {0}, gg[kPackMaxS] = {0};
  for (int i = 0; i < N; i++) {
    gc[grp[i]] += cap[i];
    gg[grp[i]] += g[i];
  }
  double meanC = 0, meanR = 0;
  for (int k = 0; k < S; k++) {
    meanC += gc[k];
    meanR += 1.0 / gg[k];
  }
  meanC /= S;
  meanR /= S;  // approximately constant; good enough as a normaliser
  auto term = [&](double c, double gsum) {
    double dc = (c - meanC) / meanC;
    double dr = (1.0 / gsum - meanR) / meanR;
    return dc * dc + (double)irWeight * dr * dr;
  };

  // ---- best-improvement swaps ----
  for (int pass = 0; pass < 500; pass++) {
    double bestGain = 1e-12;
    int bi = -1, bj = -1;
    for (int i = 0; i < N; i++) {
      for (int j = i + 1; j < N; j++) {
        int a = grp[i], b = grp[j];
        if (a == b) continue;
        double before = term(gc[a], gg[a]) + term(gc[b], gg[b]);
        double ca = gc[a] - cap[i] + cap[j], cb = gc[b] - cap[j] + cap[i];
        double ga = gg[a] - g[i] + g[j], gb = gg[b] - g[j] + g[i];
        double after = term(ca, ga) + term(cb, gb);
        double gain = before - after;
        if (gain > bestGain) {
          bestGain = gain;
          bi = i;
          bj = j;
        }
      }
    }
    if (bi < 0) break;
    int a = grp[bi], b = grp[bj];
    gc[a] += cap[bj] - cap[bi];
    gc[b] += cap[bi] - cap[bj];
    gg[a] += g[bj] - g[bi];
    gg[b] += g[bi] - g[bj];
    grp[bi] = b;
    grp[bj] = a;
    r.swaps++;
  }

  // ---- results ----
  for (int i = 0; i < N; i++) r.group[sel_[i]] = grp[i];
  float cmin = 1e30f, cmax = -1e30f, rmin = 1e30f, rmax = -1e30f;
  double csum = 0, rsum = 0;
  for (int k = 0; k < S; k++) {
    r.groupCap[k] = (float)gc[k];
    r.groupIr[k] = (float)(1.0 / gg[k]);
    cmin = fminf(cmin, r.groupCap[k]);
    cmax = fmaxf(cmax, r.groupCap[k]);
    rmin = fminf(rmin, r.groupIr[k]);
    rmax = fmaxf(rmax, r.groupIr[k]);
    csum += r.groupCap[k];
    rsum += r.groupIr[k];
  }
  r.capSpreadPct = (float)((cmax - cmin) / (csum / S) * 100.0);
  r.irSpreadPct = (float)((rmax - rmin) / (rsum / S) * 100.0);
  r.packCapMah = cmin;
  r.packIrMohm = (float)rsum;
  r.ok = true;
  return true;
}

}  // namespace lfp8
