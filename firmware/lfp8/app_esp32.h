// ESP32 application glue shared by the control task, web server and network
// code. Only the Controller is shared between tasks; every access goes
// through the mutex (held for microseconds by the web side - snapshots are
// copied under the lock and formatted afterwards).
#pragma once
#include <stdint.h>

#include "api.h"
#include "controller.h"
#include "peers.h"
#include "settings.h"

class Print;  // Arduino

namespace lfp8 {
namespace app {

// USB-Serial/JTAG console (never UART0: its pins are SAFE_RB and FAN_PWM).
::Print &con();

void setup();  // called from setup()
void loop();   // called from loop()

Controller &ctl();
bool lock(uint32_t timeoutMs = 200);
void unlock();
struct Lock {
  bool ok;
  Lock() : ok(lock()) {}
  ~Lock() {
    if (ok) unlock();
  }
};

// Current settings (copy taken under the lock).
Settings settings();
// Replace settings (sanitized). Persisted to NVS immediately when no job is
// running (returns true), otherwise as soon as the board is idle (false).
bool applySettings(const Settings &s);
void requestReboot(uint32_t delayMs);

// ---- NVS (nvs_esp32.cpp) ----
bool nvsLoadSettings(Settings &s);
bool nvsSaveSettings(const Settings &s);
bool nvsLoadResults(ChResult r[kNumCh]);
bool nvsSaveResults(const ChResult r[kNumCh]);

// ---- network (net_esp32.cpp) ----
void netBegin(const Settings &s);
void netLoop();
void netInfo(NetInfo &n);
const PeerTable &peers();

// ---- web (web_esp32.cpp) ----
void webBegin();
void webLoop();

}  // namespace app
}  // namespace lfp8
