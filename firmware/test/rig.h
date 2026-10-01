// Controller + simulated board, stepped like the ESP32 control task
// (1 ms per iteration, 250 us while the IR engine is active).
#pragma once
#include <functional>

#include "../lfp8/controller.h"
#include "../lfp8/settings.h"
#include "sim_board.h"

struct Rig {
  SimBoard sim;
  lfp8::Controller ctl{sim};
  lfp8::Settings cfg;
  uint32_t stepUs = 1000;

  Rig() {
    lfp8::settingsDefaults(cfg);
    cfg.v25Valid = 1;
    cfg.v25 = lfp8::kTbrdV25Default;
    for (int k = 0; k < lfp8::kNumCh; k++) sim.cell[k].present = false;
  }
  void begin() { ctl.begin(cfg); }
  void stepOnce() {
    uint32_t us = ctl.wantsFastLoop() ? 250 : stepUs;
    sim.advance(us);
    ctl.step();
  }
  void run(double seconds) {
    uint64_t end = sim.nowUs + (uint64_t)(seconds * 1e6);
    while (sim.nowUs < end) stepOnce();
  }
  // Run until pred() is true or the timeout expires. Returns pred().
  bool runUntil(const std::function<bool()> &pred, double maxSeconds) {
    uint64_t end = sim.nowUs + (uint64_t)(maxSeconds * 1e6);
    while (sim.nowUs < end) {
      stepOnce();
      if (pred()) return true;
    }
    return pred();
  }
  const char *cmd(lfp8::Command::Type t, uint8_t ch = 0, lfp8::Program p = lfp8::Program::None, double v = 0) {
    lfp8::Command c;
    c.type = t;
    c.ch = ch;
    c.prog = p;
    c.value = v;
    return ctl.command(c);
  }
  const char *start(uint8_t ch, lfp8::Program p) { return cmd(lfp8::Command::Type::Start, ch, p); }
  lfp8::ChState st(int k) const { return ctl.channel(k).state(); }
};
