// ESP32 application: control task + persistence + glue.
#if defined(ARDUINO)
#include "app_esp32.h"

#include <Arduino.h>
#include <esp_mac.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "hal_esp32.h"

namespace lfp8 {
namespace app {

namespace {
#if ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT
HWCDC &usb() { return HWCDCSerial; }
#else
HWCDC g_usb;  // own instance: works with the default "USB CDC On Boot: Disabled"
HWCDC &usb() { return g_usb; }
#endif

Esp32Hal g_hal;
Controller g_ctl(g_hal);  // ~110 KB incl. the 24 h history ring
SemaphoreHandle_t g_mtx = nullptr;
TaskHandle_t g_ctlTask = nullptr;
uint32_t g_rebootAtMs = 0;
bool g_reboot = false;
uint32_t g_lastPersistMs = 0;
uint32_t g_savedResultsVersion = 0;
uint32_t g_bootPressedMs = 0;
bool g_cfgPending = false;  // settings changed but not yet written to NVS

// Higher than the Arduino loop task (1) and the web server, lower than the
// WiFi/lwIP tasks so networking keeps working.
constexpr UBaseType_t kCtlPriority = 10;
constexpr uint32_t kCtlStack = 6144;
constexpr uint32_t kTwdtMs = 3000;

// The main control loop. The heartbeat is toggled inside Controller::step()
// (software GPIO write, never a timer/LEDC/RMT): if this loop stalls, the
// hardware watchdog removes SAFE_EN within ~70 ms. The task watchdog resets
// the chip if the loop hangs for 3 s.
void controlTask(void *) {
  esp_task_wdt_add(nullptr);
  for (;;) {
    bool fast = false;
    // Short wait only: the hardware watchdog trips 23..33 ms after the last
    // heartbeat edge. The web side holds the lock for copies (microseconds).
    if (xSemaphoreTake(g_mtx, pdMS_TO_TICKS(3)) == pdTRUE) {
      g_ctl.step();
      fast = g_ctl.wantsFastLoop();
      xSemaphoreGive(g_mtx);
      esp_task_wdt_reset();
    }
    if (fast) {
      // IR pulse running: poll the ADS1115 at 860 SPS without sleeping a tick.
      esp_rom_delay_us(100);
      taskYIELD();
    } else {
      vTaskDelay(1);  // 1 ms tick -> ~500 Hz heartbeat square wave
    }
  }
}

// NVS writes may erase a flash sector, which stalls the whole CPU (cache off)
// for tens of ms - longer than the 23 ms hardware watchdog window. They are
// therefore only done while no job is running (or right before a reboot);
// changes are applied in RAM immediately.
void persistLoop(bool force) {
  uint32_t now = millis();
  if (!force && now - g_lastPersistMs < 1000) return;
  g_lastPersistMs = now;
  bool saveCfg = false, saveRes = false;
  Settings s;
  static ChResult res[kNumCh];
  if (lock()) {
    if (!force && g_ctl.anyJobActive()) {
      unlock();
      return;  // deferred until the board is idle
    }
    if (g_ctl.settingsDirty() || g_cfgPending) {
      s = g_ctl.settings();
      g_ctl.clearSettingsDirty();
      g_cfgPending = false;
      saveCfg = true;
    }
    if (g_ctl.resultsVersion() != g_savedResultsVersion) {
      g_savedResultsVersion = g_ctl.resultsVersion();
      g_ctl.getResults(res);
      saveRes = true;
    }
    unlock();
  }
  if (saveCfg) nvsSaveSettings(s);
  if (saveRes) nvsSaveResults(res);
}

void bootButtonLoop() {
  // Hold BOOT for 5 s at runtime: forget WiFi credentials and restart in AP mode.
  bool pressed = g_hal.bootButton();
  uint32_t now = millis();
  if (!pressed) {
    g_bootPressedMs = 0;
    return;
  }
  if (g_bootPressedMs == 0) g_bootPressedMs = now ? now : 1;
  if (now - g_bootPressedMs > 5000) {
    Settings s = settings();
    s.wifiSsid[0] = 0;
    s.wifiPass[0] = 0;
    applySettings(s);
    con().println("[lfp8] BOOT held 5 s: WiFi credentials cleared, rebooting to AP mode");
    requestReboot(200);
    g_bootPressedMs = now;
  }
}
}  // namespace

::Print &con() { return usb(); }

Controller &ctl() { return g_ctl; }

bool lock(uint32_t timeoutMs) { return g_mtx && xSemaphoreTake(g_mtx, pdMS_TO_TICKS(timeoutMs)) == pdTRUE; }
void unlock() { xSemaphoreGive(g_mtx); }

Settings settings() {
  Settings s;
  Lock l;
  s = g_ctl.settings();
  return s;
}

bool applySettings(const Settings &in) {
  Settings s = in;
  settingsSanitize(s);
  bool busy = true;
  {
    Lock l;
    if (l.ok) {
      g_ctl.setSettings(s);
      busy = g_ctl.anyJobActive();
    }
  }
  if (busy) {
    g_cfgPending = true;  // written by persistLoop() once the board is idle
    return false;
  }
  nvsSaveSettings(s);
  return true;
}

void requestReboot(uint32_t delayMs) {
  g_rebootAtMs = millis() + delayMs;
  g_reboot = true;
}

void setup() {
  usb().begin(115200);  // native USB console; UART0 pins are IO20/IO21 (SAFE_RB/FAN)
  g_hal.begin();              // outputs off, heartbeat low

  Settings s;
  bool loaded = nvsLoadSettings(s);
  if (!loaded) {
    settingsDefaults(s);
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    s.boardId = boardIdFromMac(mac);
  }
  settingsSanitize(s);

  esp_task_wdt_config_t twdt = {};
  twdt.timeout_ms = kTwdtMs;
  twdt.idle_core_mask = 0;
  twdt.trigger_panic = true;
  if (esp_task_wdt_reconfigure(&twdt) != ESP_OK) esp_task_wdt_init(&twdt);

  g_mtx = xSemaphoreCreateMutex();
  g_ctl.begin(s);
  static ChResult res[kNumCh];
  if (nvsLoadResults(res)) g_ctl.restoreResults(res);
  g_savedResultsVersion = g_ctl.resultsVersion();
  if (!loaded || g_ctl.settingsDirty()) {
    nvsSaveSettings(g_ctl.settings());
    g_ctl.clearSettingsDirty();
  }
  // Start the control loop (and with it the heartbeat) before WiFi so the
  // board is supervised while the network comes up.
  xTaskCreate(controlTask, "lfp8_ctl", kCtlStack, nullptr, kCtlPriority, &g_ctlTask);

  con().printf("[lfp8] LFP-8 firmware %s, board %u\n", LFP8_FW_VERSION, (unsigned)s.boardId);
  netBegin(s);
  webBegin();
}

void loop() {
  webLoop();
  netLoop();
  persistLoop(false);
  bootButtonLoop();
  if (g_reboot && (int32_t)(millis() - g_rebootAtMs) >= 0) {
    {
      Lock l;
      if (l.ok) g_ctl.setMaintenance(true);  // outputs off before the reset
    }
    persistLoop(true);  // heartbeat already stopped: flash stalls are harmless now
    delay(50);
    ESP.restart();
  }
  vTaskDelay(1);
}

}  // namespace app
}  // namespace lfp8
#endif  // ARDUINO
