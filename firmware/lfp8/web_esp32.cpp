// HTTP server: dashboard + JSON API (see API.md). Runs in the Arduino loop
// task; the controller is only touched under the mutex, for copies.
#if defined(ARDUINO)
#include <Arduino.h>
#include <Update.h>
#include <WebServer.h>

#include "app_esp32.h"
#include "json_writer.h"
#include "webui_html.h"

namespace lfp8 {
namespace app {

namespace {
WebServer server(80);

// Buffers output and sends it as HTTP chunks.
class ChunkSink : public Sink {
 public:
  void write(const char *s, size_t n) override {
    while (n) {
      size_t k = sizeof(buf_) - len_;
      if (k > n) k = n;
      memcpy(buf_ + len_, s, k);
      len_ += k;
      s += k;
      n -= k;
      if (len_ == sizeof(buf_)) flush();
    }
  }
  void flush() {
    if (len_) server.chunkWrite(buf_, len_);
    len_ = 0;
  }

 private:
  char buf_[1024];
  size_t len_ = 0;
};

void noCache() { server.sendHeader("Cache-Control", "no-store"); }

void beginChunked(const char *type) {
  noCache();
  server.chunkResponseBegin(type);
}

void sendJsonText(int code, const char *json) {
  noCache();
  server.send(code, "application/json", json);
}

void sendResult(const char *err, int codeOnError = 400) {
  char buf[160];
  BufSink s(buf, sizeof(buf));
  JsonWriter j(s);
  apiResultJson(j, err);
  sendJsonText(err ? codeOnError : 200, buf);
}

bool authorized() {
  Settings s = settings();
  if (!s.adminPass[0]) return true;
  if (server.authenticate("admin", s.adminPass)) return true;
  server.requestAuthentication(BASIC_AUTH, "LFP-8");
  return false;
}

// ------------------------------------------------------------ handlers ----
void handleIndex() {
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML, sizeof(INDEX_HTML) - 1);
}

void handleStatus() {
  static StatusSnapshot snap;
  {
    Lock l;
    if (!l.ok) return sendResult("controller busy", 503);
    ctl().snapshot(snap);
  }
  NetInfo n;
  netInfo(n);
  beginChunked("application/json");
  ChunkSink cs;
  JsonWriter j(cs);
  apiStatusJson(j, snap, n);
  cs.flush();
  server.chunkResponseEnd();
}

void handleHistory() {
  long ch = server.arg("ch").toInt();
  if (ch < 1 || ch > kNumCh) return sendResult("ch must be 1..8");
  uint32_t abs0 = 0, total = 0;
  int count = 0;
  uint32_t ageS = 0;
  {
    Lock l;
    if (!l.ok) return sendResult("controller busy", 503);
    const History &h = ctl().history();
    abs0 = h.oldestAbs();
    total = h.total();
    count = h.count();
    ageS = (millis() - h.lastPushMs()) / 1000;
  }
  NetInfo n;
  netInfo(n);
  beginChunked("application/json");
  ChunkSink cs;
  JsonWriter j(cs);
  apiHistoryBegin(j, (uint8_t)ch, n.id, count, ageS);
  static HistSample buf[240];
  uint32_t next = abs0;
  while (next < total) {
    int got = 0;
    uint32_t first = next;
    {
      Lock l;
      if (!l.ok) break;
      int want = (int)((total - next) < 240u ? (total - next) : 240u);
      got = ctl().history().copyAbs((int)ch - 1, next, want, buf, &first);
    }
    if (got <= 0) break;
    apiHistorySamples(j, buf, got);
    next = first + (uint32_t)got;
  }
  apiHistoryEnd(j);
  cs.flush();
  server.chunkResponseEnd();
}

void handleIrTrace() {
  static IrPoint v[220], i[220];
  int nv = 0, ni = 0;
  IrResult r;
  uint8_t ch = 0;
  {
    Lock l;
    if (!l.ok) return sendResult("controller busy", 503);
    nv = ctl().irTrace(v, 220, i, 220, ni, r, ch);
  }
  NetInfo n;
  netInfo(n);
  beginChunked("application/json");
  ChunkSink cs;
  JsonWriter j(cs);
  apiIrTraceJson(j, n.id, ch, r, v, nv, i, ni);
  cs.flush();
  server.chunkResponseEnd();
}

void handlePeers() {
  NetInfo n;
  netInfo(n);
  beginChunked("application/json");
  ChunkSink cs;
  JsonWriter j(cs);
  apiPeersJson(j, peers(), n, millis());
  cs.flush();
  server.chunkResponseEnd();
}

void handleCmd() {
  String body = server.arg("plain");
  Command c;
  bool reboot = false;
  const char *err = apiParseCommand(body.c_str(), c, &reboot);
  if (err) return sendResult(err);
  if (reboot) {
    requestReboot(500);
    return sendResult(nullptr);
  }
  {
    Lock l;
    if (!l.ok) return sendResult("controller busy", 503);
    err = ctl().command(c);
  }
  sendResult(err, 409);
}

void handleConfigGet() {
  Settings s = settings();
  beginChunked("application/json");
  ChunkSink cs;
  JsonWriter j(cs);
  apiConfigJson(j, s);
  cs.flush();
  server.chunkResponseEnd();
}

void handleConfigPost() {
  if (!authorized()) return;
  String body = server.arg("plain");
  Settings s = settings();
  bool reboot = false;
  int changed = 0;
  const char *err = apiApplyConfig(body.c_str(), s, &reboot, &changed);
  if (err) return sendResult(err);
  bool persisted = applySettings(s);
  char buf[128];
  snprintf(buf, sizeof(buf), "{\"ok\":true,\"changed\":%d,\"reboot_required\":%s,\"persisted\":%s}", changed,
           reboot ? "true" : "false", persisted ? "true" : "false");
  sendJsonText(200, buf);
}

void handleExport() {
  static StatusSnapshot snap;
  {
    Lock l;
    if (!l.ok) return sendResult("controller busy", 503);
    ctl().snapshot(snap);
  }
  NetInfo n;
  netInfo(n);
  char disp[64];
  snprintf(disp, sizeof(disp), "attachment; filename=\"lfp8-board%u.csv\"", (unsigned)n.id);
  server.sendHeader("Content-Disposition", disp);
  beginChunked("text/csv");
  ChunkSink cs;
  apiCsvHeader(cs);
  for (int k = 0; k < kNumCh; k++) apiCsvRow(cs, n.id, snap.ch[k]);
  cs.flush();
  server.chunkResponseEnd();
}

void handlePack() {
  static PackCell cells[kPackMaxCells];
  static PackResult r;
  String body = server.arg("plain");
  int n = 0, S = 0, P = 0;
  PackSelect sel = PackSelect::Top;
  float w = 0.1f;
  const char *err = apiParsePack(body.c_str(), cells, kPackMaxCells, n, S, P, sel, w);
  if (err) return sendResult(err);
  packBuild(cells, n, S, P, sel, w, r);
  beginChunked("application/json");
  ChunkSink cs;
  JsonWriter j(cs);
  apiPackJson(j, cells, n, r);
  cs.flush();
  server.chunkResponseEnd();
}

// ---- OTA over HTTP ----
const char kUpdatePage[] PROGMEM =
    "<!doctype html><meta name=viewport content='width=device-width'><title>LFP-8 update</title>"
    "<body style='font-family:sans-serif;max-width:40em;margin:2em auto'><h2>LFP-8 firmware update</h2>"
    "<p>Upload <code>lfp8.ino.bin</code>. Refused while tests are running unless <i>force</i> is ticked "
    "(running jobs are then aborted).</p><form method=POST action='/update' enctype='multipart/form-data'>"
    "<input type=file name=fw accept='.bin'> <label><input type=checkbox name=force value=1 "
    "onchange=\"this.form.action='/update'+(this.checked?'?force=1':'')\"> force</label> "
    "<button>Upload</button></form><p><a href='/'>back</a></p>";

bool g_updRejected = false;
const char *g_updErr = nullptr;

void handleUpdatePage() { server.send_P(200, "text/html", kUpdatePage); }

void handleUpdateUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    g_updRejected = false;
    g_updErr = nullptr;
    Settings s = settings();
    if (s.adminPass[0] && !server.authenticate("admin", s.adminPass)) {
      g_updRejected = true;
      g_updErr = "authentication required";
      return;
    }
    bool force = server.arg("force") == "1";
    bool busy;
    {
      Lock l;
      busy = !l.ok || ctl().anyJobActive();
    }
    if (busy && !force) {
      g_updRejected = true;
      g_updErr = "tests are running - stop them or use force=1";
      return;
    }
    {
      Lock l;
      if (l.ok) ctl().setMaintenance(true);  // all outputs off, heartbeat stops on purpose
    }
    con().printf("[lfp8] HTTP OTA: %s\n", up.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) g_updErr = "Update.begin failed";
  } else if (g_updRejected || g_updErr) {
    return;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) g_updErr = "flash write failed";
  } else if (up.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) g_updErr = "image verification failed";
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    g_updErr = "upload aborted";
  }
}

void handleUpdateDone() {
  if (g_updRejected) return sendResult(g_updErr, 409);
  if (g_updErr || Update.hasError()) {
    Update.abort();
    Lock l;
    if (l.ok) ctl().setMaintenance(false);
    return sendResult(g_updErr ? g_updErr : "update failed", 500);
  }
  sendResult(nullptr);
  requestReboot(800);
}

void handleOptions() {
  server.sendHeader("Access-Control-Max-Age", "600");
  server.send(204);
}

void handleNotFound() {
  if (server.method() == HTTP_OPTIONS) return handleOptions();
  sendResult("not found", 404);
}
}  // namespace

void webBegin() {
  server.enableCORS(true);  // Access-Control-Allow-Origin: * (multi-board dashboard)
  server.on("/", HTTP_GET, handleIndex);
  server.on("/index.html", HTTP_GET, handleIndex);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/history", HTTP_GET, handleHistory);
  server.on("/api/irtrace", HTTP_GET, handleIrTrace);
  server.on("/api/peers", HTTP_GET, handlePeers);
  server.on("/api/cmd", HTTP_POST, handleCmd);
  server.on("/api/config", HTTP_GET, handleConfigGet);
  server.on("/api/config", HTTP_POST, handleConfigPost);
  server.on("/api/export.csv", HTTP_GET, handleExport);
  server.on("/api/pack", HTTP_POST, handlePack);
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.onNotFound(handleNotFound);
  server.begin();
}

void webLoop() { server.handleClient(); }

}  // namespace app
}  // namespace lfp8
#endif  // ARDUINO
