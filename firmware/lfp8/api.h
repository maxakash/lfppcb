// JSON / CSV builders and request parsers for the HTTP API (see API.md).
// Pure C++ so that every response format is unit-tested on the host.
#pragma once
#include <stdint.h>

#include "controller.h"
#include "history.h"
#include "json_writer.h"
#include "packbuilder.h"
#include "peers.h"
#include "settings.h"

namespace lfp8 {

struct NetInfo {
  uint8_t id = 1;
  const char *ip = "0.0.0.0";
  const char *host = "lfp8-1";
  const char *fw = LFP8_FW_VERSION;
  const char *mode = "AP";  // "STA" or "AP"
  int rssi = 0;
  bool dupId = false;
};

// GET /api/status
void apiStatusJson(JsonWriter &j, const StatusSnapshot &s, const NetInfo &n);
// GET /api/config (passwords are never returned, only *_set flags)
void apiConfigJson(JsonWriter &j, const Settings &s);
// POST /api/config: apply the keys present in body to s (then sanitized).
// Returns nullptr on success or an error. *reboot is set when WiFi or the
// board id changed (takes effect after a restart).
const char *apiApplyConfig(const char *body, Settings &s, bool *reboot, int *changed);
// POST /api/cmd. Returns nullptr on success. *reboot set for {"cmd":"reboot"}.
const char *apiParseCommand(const char *body, Command &cmd, bool *reboot);
// GET /api/history?ch=k  (streamed: begin, samples..., end)
void apiHistoryBegin(JsonWriter &j, uint8_t ch, uint8_t boardId, int count, uint32_t ageS);
void apiHistorySamples(JsonWriter &j, const HistSample *s, int n);
void apiHistoryEnd(JsonWriter &j);
// GET /api/peers
void apiPeersJson(JsonWriter &j, const PeerTable &t, const NetInfo &self, uint32_t nowMs);
// GET /api/export.csv
void apiCsvHeader(Sink &s);
void apiCsvRow(Sink &s, uint8_t boardId, const ChStatus &c);
// POST /api/pack  {"s":4,"p":10,"sel":"top|tight","irw":0.1,"cells":[id,cap,ir, id,cap,ir, ...]}
const char *apiParsePack(const char *body, PackCell *cells, int maxCells, int &n, int &S, int &P,
                         PackSelect &sel, float &irWeight);
void apiPackJson(JsonWriter &j, const PackCell *cells, int n, const PackResult &r);
// GET /api/irtrace
void apiIrTraceJson(JsonWriter &j, uint8_t boardId, uint8_t ch, const IrResult &r, const IrPoint *v, int nv,
                    const IrPoint *i, int ni);
// {"ok":true} / {"ok":false,"err":"..."}
void apiResultJson(JsonWriter &j, const char *err);

}  // namespace lfp8
