// WiFi (STA from NVS, AP fallback), mDNS, UDP discovery (§9), ArduinoOTA.
#if defined(ARDUINO)
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include "app_esp32.h"

namespace lfp8 {
namespace app {

namespace {
constexpr uint32_t kStaConnectTimeoutMs = 15000;
constexpr uint32_t kApOffAfterMs = 120000;  // STA up and no AP client: drop the AP
constexpr uint32_t kApOnAfterMs = 30000;    // STA lost this long: bring the AP back
const char *kApPass = "lfp8admin";

WiFiUDP g_udp;
PeerTable g_peers;
char g_host[16] = "lfp8-1";
char g_apSsid[16] = "LFP8-1";
char g_ip[16] = "0.0.0.0";
uint8_t g_id = 1;
bool g_staConfigured = false;
bool g_apOn = false;
bool g_otaStarted = false;
uint32_t g_lastHelloMs = 0;
uint32_t g_staUpSinceMs = 0, g_staDownSinceMs = 0;
bool g_staWasUp = false;

void startAp() {
  WiFi.mode(g_staConfigured ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(g_apSsid, kApPass);
  g_apOn = true;
  con().printf("[lfp8] AP \"%s\" (password %s) at %s\n", g_apSsid, kApPass, WiFi.softAPIP().toString().c_str());
}

void stopAp() {
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  g_apOn = false;
  con().println("[lfp8] STA connected - AP switched off");
}

void updateIp() {
  IPAddress ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP() : WiFi.softAPIP();
  snprintf(g_ip, sizeof(g_ip), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

void sendHello() {
  updateIp();
  char buf[96];
  size_t n = formatHello(buf, sizeof(buf), g_id, g_ip, LFP8_FW_VERSION);
  if (!n) return;
  if (WiFi.status() == WL_CONNECTED) {
    g_udp.beginPacket(WiFi.broadcastIP(), kHelloPort);
    g_udp.write((const uint8_t *)buf, n);
    g_udp.endPacket();
  }
  if (g_apOn) {
    g_udp.beginPacket(WiFi.softAPBroadcastIP(), kHelloPort);
    g_udp.write((const uint8_t *)buf, n);
    g_udp.endPacket();
  }
}

void receiveHellos() {
  for (int guard = 0; guard < 8; guard++) {
    int len = g_udp.parsePacket();
    if (len <= 0) break;
    char buf[160];
    int n = g_udp.read(buf, sizeof(buf) - 1);
    if (n <= 0) continue;
    buf[n] = 0;
    uint8_t id;
    char ip[16], fw[16];
    if (!parseHello(buf, id, ip, sizeof(ip), fw, sizeof(fw))) continue;
    if (strcmp(ip, g_ip) == 0) continue;  // our own broadcast
    g_peers.seen(id, ip, fw, millis());
  }
  g_peers.expire(millis());
}

bool anyJobActive() {
  Lock l;
  return l.ok ? ctl().anyJobActive() : true;  // unknown -> assume busy
}

void otaBegin(const Settings &s) {
  ArduinoOTA.setHostname(g_host);
  ArduinoOTA.setPassword(s.adminPass[0] ? s.adminPass : kApPass);
  ArduinoOTA.setMdnsEnabled(false);  // we run MDNS ourselves
  ArduinoOTA.onStart([]() {
    Lock l;
    if (l.ok) ctl().setMaintenance(true);  // outputs off, heartbeat stopped on purpose
    con().println("[lfp8] OTA start");
  });
  ArduinoOTA.onError([](ota_error_t e) {
    Lock l;
    if (l.ok) ctl().setMaintenance(false);
    con().printf("[lfp8] OTA error %d\n", (int)e);
  });
  ArduinoOTA.begin();
  MDNS.enableArduino(3232, true);
  g_otaStarted = true;
}
}  // namespace

const PeerTable &peers() { return g_peers; }

void netInfo(NetInfo &n) {
  updateIp();
  n.id = g_id;
  n.ip = g_ip;
  n.host = g_host;
  n.fw = LFP8_FW_VERSION;
  n.mode = (WiFi.status() == WL_CONNECTED) ? "STA" : "AP";
  n.rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
  n.dupId = g_peers.duplicateId(g_id, g_ip);
}

void netBegin(const Settings &s) {
  g_id = s.boardId;
  snprintf(g_host, sizeof(g_host), "lfp8-%u", (unsigned)g_id);
  snprintf(g_apSsid, sizeof(g_apSsid), "LFP8-%u", (unsigned)g_id);
  WiFi.persistent(false);  // credentials live in our own NVS blob
  WiFi.setHostname(g_host);
  g_staConfigured = s.wifiSsid[0] != 0;
  if (g_staConfigured) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(s.wifiSsid, s.wifiPass);
    con().printf("[lfp8] connecting to \"%s\"...\n", s.wifiSsid);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < kStaConnectTimeoutMs) delay(100);
  }
  if (WiFi.status() == WL_CONNECTED) {
    g_staWasUp = true;
    g_staUpSinceMs = millis();
    con().printf("[lfp8] STA %s\n", WiFi.localIP().toString().c_str());
  } else {
    startAp();  // config page reachable at 192.168.4.1
  }
  updateIp();
  if (MDNS.begin(g_host)) MDNS.addService("http", "tcp", 80);
  g_udp.begin(kHelloPort);
  otaBegin(s);
}

void netLoop() {
  uint32_t now = millis();
  // ---- STA / AP maintenance ----
  bool up = WiFi.status() == WL_CONNECTED;
  if (up && !g_staWasUp) g_staUpSinceMs = now;
  if (!up && g_staWasUp) g_staDownSinceMs = now;
  g_staWasUp = up;
  if (g_staConfigured) {
    if (up && g_apOn && now - g_staUpSinceMs > kApOffAfterMs && WiFi.softAPgetStationNum() == 0) stopAp();
    if (!up && !g_apOn && now - g_staDownSinceMs > kApOnAfterMs) startAp();
  }
  // ---- discovery ----
  if (now - g_lastHelloMs >= kHelloPeriodMs) {
    g_lastHelloMs = now;
    sendHello();
  }
  receiveHellos();
  // ---- ArduinoOTA: only offered while no test is running ----
  static bool busy = true;
  static uint32_t lastBusyCheckMs = 0;
  if (now - lastBusyCheckMs >= 500) {
    lastBusyCheckMs = now;
    busy = anyJobActive();
  }
  if (g_otaStarted && !busy) ArduinoOTA.handle();
}

}  // namespace app
}  // namespace lfp8
#endif  // ARDUINO
