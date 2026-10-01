// JSON emitter / reader and every API document. Each API response is also
// written to --json-out so that `make test` can parse it with python3.
#include <math.h>
#include <string.h>

#include <memory>
#include <string>

#include "../lfp8/api.h"
#include "../lfp8/json_reader.h"
#include "../lfp8/json_writer.h"
#include "rig.h"
#include "test_framework.h"

using namespace lfp8;

namespace {
struct StrSink : Sink {
  std::string s;
  void write(const char *p, size_t n) override { s.append(p, n); }
};
}  // namespace

TEST(json_writer_basics_and_escaping) {
  StrSink s;
  JsonWriter j(s);
  j.beginObject();
  j.kv("a", "x\"y\\z\n\t\x01");
  j.kv("pi", 3.14159, 3).kv("nan", NAN, 2).kv("inf", INFINITY, 1).kv("negzero", -0.0001, 2);
  j.kvInt("i", -42).kvBool("t", true).key("n").null();
  j.key("arr").beginArray().integer(1).num(2.5, 1).str("s").beginObject().endObject().beginArray().endArray().endArray();
  j.key("utf8").str("\xc2\xb0" "C");
  j.endObject();
  CHECK_STREQ(s.s.c_str(),
              "{\"a\":\"x\\\"y\\\\z\\n\\t\\u0001\",\"pi\":3.142,\"nan\":null,\"inf\":null,\"negzero\":0.00,"
              "\"i\":-42,\"t\":true,\"n\":null,\"arr\":[1,2.5,\"s\",{},[]],\"utf8\":\"\xc2\xb0" "C\"}");
  CHECK(jr::validObject(s.s.c_str()));
  CHECK_EQ(j.depth(), 0);
  tf::writeArtifact("writer_basics.json", s.s.c_str());

  char buf[16];
  BufSink b(buf, sizeof(buf));
  JsonWriter k(b);
  k.beginObject().kv("long", "0123456789abcdef").endObject();
  CHECK(b.overflow());
  CHECK_EQ(strlen(buf), 15);
}

TEST(json_reader) {
  const char *js = " { \"cmd\" : \"charge\", \"ch\": 3, \"value\": -1.5e1, \"ok\": true, "
                   "\"nested\": {\"cmd\": \"no\", \"x\": [1, {\"y\": \"]\"}]}, \"s\": \"a\\\"b\\u0041\", "
                   "\"arr\": [1, 2.5, -3] } ";
  CHECK(jr::validObject(js));
  char str[16];
  CHECK(jr::getStr(js, "cmd", str, sizeof(str)));
  CHECK_STREQ(str, "charge");  // top-level key, not the nested one
  long ch = 0;
  CHECK(jr::getInt(js, "ch", ch));
  CHECK_EQ(ch, 3);
  double v = 0;
  CHECK(jr::getNum(js, "value", v));
  CHECK_NEAR(v, -15.0, 1e-12);
  bool b = false;
  CHECK(jr::getBool(js, "ok", b) && b);
  CHECK(jr::getStr(js, "s", str, sizeof(str)));
  CHECK_STREQ(str, "a\"bA");
  double a[4];
  CHECK_EQ(jr::getNumArray(js, "arr", a, 4), 3);
  CHECK_NEAR(a[2], -3.0, 0);
  CHECK_EQ(jr::getNumArray(js, "arr", a, 2), -2);
  CHECK(!jr::has(js, "y"));
  CHECK(!jr::getInt(js, "value", ch) || ch == -15);
  CHECK(!jr::getStr(js, "cmd", str, 4));  // does not fit
  // invalid documents
  const char *bad[] = {"", "[]", "{", "{\"a\":}", "{\"a\":1,}", "{\"a\":01}", "{'a':1}", "{\"a\":1} x",
                       "{\"a\":\"\x01\"}", "{\"a\":tru}", "{\"a\":[1,2}"};
  for (const char *x : bad) CHECK_MSG(!jr::validObject(x), "accepted: %s", x);
}

TEST(api_status_json_from_live_controller) {
  std::unique_ptr<Rig> rp(new Rig);
  Rig &r = *rp;
  r.sim.cell[0].present = true;
  r.sim.cell[0].soc = 0.5;
  r.sim.cell[1].present = true;
  r.sim.cell[1].reversed = true;
  r.sim.cell[2].present = true;
  r.sim.cell[2].soc = 0.5;
  r.sim.cell[2].ntcFitted = false;
  r.begin();
  r.run(1.0);
  r.start(1, Program::CapTest);
  r.start(3, Program::Ir);
  r.run(12.0);
  static StatusSnapshot snap;
  r.ctl.snapshot(snap);
  NetInfo n;
  n.id = 2;
  n.ip = "192.168.1.52";
  n.host = "lfp8-2";
  n.mode = "STA";
  n.rssi = -61;
  StrSink s;
  JsonWriter j(s);
  apiStatusJson(j, snap, n);
  CHECK(jr::validObject(s.s.c_str()));
  CHECK(strstr(s.s.c_str(), "\"g\":9") != nullptr);  // global channel (2-1)*8+1
  CHECK(strstr(s.s.c_str(), "\"st\":\"REVERSED\"") != nullptr);
  CHECK(strstr(s.s.c_str(), "\"st\":\"CHARGING_CC\"") != nullptr);
  CHECK(strstr(s.s.c_str(), "\"ir_dc\":") != nullptr);
  tf::writeArtifact("status.json", s.s.c_str());

  // CSV export
  StrSink c;
  apiCsvHeader(c);
  for (int k = 0; k < kNumCh; k++) apiCsvRow(c, 2, snap.ch[k]);
  int lines = 0;
  for (char ch : c.s) lines += ch == '\n';
  CHECK_EQ(lines, 9);
  CHECK(c.s.find("2,3,11,DONE,none") != std::string::npos);
  tf::writeArtifact("export.csv", c.s.c_str());

  // IR trace
  static IrPoint v[200], i[200];
  int ni = 0;
  IrResult res;
  uint8_t ch = 0;
  int nv = r.ctl.irTrace(v, 200, i, 200, ni, res, ch);
  CHECK(nv > 50 && ni > 50);
  CHECK_EQ(ch, 3);
  StrSink t;
  JsonWriter jt(t);
  apiIrTraceJson(jt, 2, ch, res, v, nv, i, ni);
  CHECK(jr::validObject(t.s.c_str()));
  tf::writeArtifact("irtrace.json", t.s.c_str());

  // history (with "no data" entries)
  r.run(65.0);
  StrSink h;
  JsonWriter jh(h);
  const History &hist = r.ctl.history();
  apiHistoryBegin(jh, 2, 2, hist.count(), 3);
  HistSample buf[8];
  for (int from = 0; from < hist.count(); from += 8) apiHistorySamples(jh, buf, hist.copy(1, from, 8, buf));
  apiHistoryEnd(jh);
  CHECK(jr::validObject(h.s.c_str()));
  CHECK(strstr(h.s.c_str(), "[-3300,0,-3300,0") != nullptr);  // reversed cell, oldest first
  tf::writeArtifact("history.json", h.s.c_str());
  // intervals without a valid sample (e.g. SAFE_EN low) are null
  StrSink h2;
  JsonWriter jh2(h2);
  HistSample nd[2] = {{kHistNoData, 0}, {3300, -1050}};
  apiHistoryBegin(jh2, 1, 1, 2, 0);
  apiHistorySamples(jh2, nd, 2);
  apiHistoryEnd(jh2);
  CHECK(strstr(h2.s.c_str(), "\"d\":[null,null,3300,-1050]") != nullptr);
  tf::writeArtifact("history_nodata.json", h2.s.c_str());
}

TEST(api_config_roundtrip_and_validation) {
  Settings a;
  settingsDefaults(a);
  a.boardId = 3;
  strcpy(a.wifiSsid, "lab \"wifi\"");
  strcpy(a.wifiPass, "secret123");
  a.calV[2] = 1.0123f;
  StrSink s;
  JsonWriter j(s);
  apiConfigJson(j, a);
  CHECK(jr::validObject(s.s.c_str()));
  CHECK(strstr(s.s.c_str(), "secret123") == nullptr);  // never returned
  CHECK(strstr(s.s.c_str(), "\"wifi_pass_set\":true") != nullptr);
  tf::writeArtifact("config.json", s.s.c_str());
  Settings b;
  settingsDefaults(b);
  bool reboot = false;
  int changed = 0;
  CHECK(apiApplyConfig(s.s.c_str(), b, &reboot, &changed) == nullptr);
  CHECK(reboot);  // board id and SSID changed
  CHECK(changed > 25);
  CHECK_EQ(b.boardId, 3);
  CHECK_STREQ(b.wifiSsid, "lab \"wifi\"");
  CHECK_NEAR(b.calV[2], 1.0123, 1e-5);
  CHECK_NEAR(b.vMaxChg, a.vMaxChg, 1e-6);
  // unsafe values are clamped, malformed ones rejected
  CHECK(apiApplyConfig("{\"v_max_chg\":4.2,\"v_min_dis\":1.0}", b, &reboot, &changed) == nullptr);
  CHECK_NEAR(b.vMaxChg, 3.65, 1e-6);
  CHECK_NEAR(b.vMinDis, 2.30, 1e-6);
  CHECK(!reboot);
  CHECK(apiApplyConfig("{\"iset\":2.5}", b, &reboot, &changed) != nullptr);
  CHECK(apiApplyConfig("{\"cal_v\":[1,1,1]}", b, &reboot, &changed) != nullptr);
  CHECK(apiApplyConfig("{\"cal_i\":[1,1,1,1,1,1,1,1.3]}", b, &reboot, &changed) != nullptr);
  CHECK(apiApplyConfig("{\"wifi_pass\":\"short\"}", b, &reboot, &changed) != nullptr);
  CHECK(apiApplyConfig("not json", b, &reboot, &changed) != nullptr);
  CHECK_NEAR(b.vMaxChg, 3.65, 1e-6);  // failed requests change nothing
}

TEST(api_command_parsing) {
  Command c;
  bool reboot = false;
  CHECK(apiParseCommand("{\"ch\":3,\"cmd\":\"captest\"}", c, &reboot) == nullptr);
  CHECK(c.type == Command::Type::Start && c.prog == Program::CapTest && c.ch == 3);
  CHECK(apiParseCommand("{\"ch\":1,\"cmd\":\"charge\"}", c, &reboot) == nullptr && c.prog == Program::Charge);
  CHECK(apiParseCommand("{\"ch\":8,\"cmd\":\"discharge\"}", c, &reboot) == nullptr && c.prog == Program::Discharge);
  CHECK(apiParseCommand("{\"ch\":2,\"cmd\":\"ir\"}", c, &reboot) == nullptr && c.prog == Program::Ir);
  CHECK(apiParseCommand("{\"ch\":2,\"cmd\":\"stop\"}", c, &reboot) == nullptr && c.type == Command::Type::Stop);
  CHECK(apiParseCommand("{\"cmd\":\"stop\"}", c, &reboot) == nullptr && c.type == Command::Type::StopAll);
  CHECK(apiParseCommand("{\"ch\":2,\"cmd\":\"reset\"}", c, &reboot) == nullptr && c.type == Command::Type::Reset);
  CHECK(apiParseCommand("{\"cmd\":\"iset\",\"value\":6}", c, &reboot) == nullptr);
  CHECK(c.type == Command::Type::Iset && c.value == 6);
  CHECK(apiParseCommand("{\"cmd\":\"ack\"}", c, &reboot) == nullptr && c.type == Command::Type::Ack);
  CHECK(apiParseCommand("{\"cmd\":\"calv\",\"ch\":4,\"value\":3.301}", c, &reboot) == nullptr);
  CHECK(c.type == Command::Type::CalV && c.ch == 4 && fabs(c.value - 3.301) < 1e-12);
  CHECK(apiParseCommand("{\"cmd\":\"reboot\"}", c, &reboot) == nullptr && reboot);
  CHECK(apiParseCommand("{\"ch\":9,\"cmd\":\"charge\"}", c, &reboot) != nullptr);
  CHECK(apiParseCommand("{\"cmd\":\"charge\"}", c, &reboot) != nullptr);
  CHECK(apiParseCommand("{\"ch\":1,\"cmd\":\"explode\"}", c, &reboot) != nullptr);
  CHECK(apiParseCommand("{\"cmd\":\"iset\"}", c, &reboot) != nullptr);
  CHECK(apiParseCommand("ch=1&cmd=charge", c, &reboot) != nullptr);
  StrSink s;
  JsonWriter j(s);
  apiResultJson(j, "bad \"thing\"");
  CHECK(jr::validObject(s.s.c_str()));
  tf::writeArtifact("cmd_result.json", s.s.c_str());
}

TEST(api_peers_and_hello) {
  char buf[96];
  size_t n = formatHello(buf, sizeof(buf), 3, "192.168.1.53", "1.0.0");
  CHECK(n > 0);
  CHECK_STREQ(buf, "{\"t\":\"hello\",\"id\":3,\"ip\":\"192.168.1.53\",\"fw\":\"1.0.0\"}");
  uint8_t id = 0;
  char ip[16], fw[16];
  CHECK(parseHello(buf, id, ip, sizeof(ip), fw, sizeof(fw)));
  CHECK_EQ(id, 3);
  CHECK_STREQ(ip, "192.168.1.53");
  CHECK(!parseHello("{\"t\":\"hello\",\"id\":3,\"ip\":\"999.1.1.1\"}", id, ip, sizeof(ip), fw, sizeof(fw)));
  CHECK(!parseHello("{\"t\":\"bye\",\"id\":3,\"ip\":\"1.1.1.1\"}", id, ip, sizeof(ip), fw, sizeof(fw)));
  CHECK(!parseHello("{\"t\":\"hello\",\"id\":0,\"ip\":\"1.1.1.1\"}", id, ip, sizeof(ip), fw, sizeof(fw)));
  CHECK(!parseHello("garbage", id, ip, sizeof(ip), fw, sizeof(fw)));
  PeerTable t;
  t.seen(2, "192.168.1.52", "1.0.0", 1000);
  t.seen(3, "192.168.1.53", "1.0.0", 2000);
  t.seen(2, "192.168.1.52", "1.0.1", 3000);  // refresh, same IP
  CHECK_EQ(t.count(), 2);
  t.seen(1, "192.168.1.60", "1.0.0", 3000);
  CHECK(t.duplicateId(1, "192.168.1.51"));
  CHECK(!t.duplicateId(4, "192.168.1.54"));
  t.expire(12500);  // id 3 last seen at 2 s -> > 10 s old
  CHECK_EQ(t.count(), 2);
  NetInfo self;
  self.id = 1;
  self.ip = "192.168.1.51";
  StrSink s;
  JsonWriter j(s);
  apiPeersJson(j, t, self, 13000);
  CHECK(jr::validObject(s.s.c_str()));
  tf::writeArtifact("peers.json", s.s.c_str());
  tf::writeArtifact("hello.json", buf);
}

TEST(api_pack_request_and_response) {
  std::string body = "{\"s\":4,\"p\":10,\"sel\":\"top\",\"irw\":0.1,\"cells\":[";
  for (int k = 1; k <= 42; k++) {
    char c[48];
    snprintf(c, sizeof(c), "%s%d,%.1f,%.2f", k > 1 ? "," : "", k, 14000.0 + (k * 37) % 1500, 6.0 + (k * 13) % 9);
    body += c;
  }
  body += "]}";
  static PackCell cells[kPackMaxCells];
  int n = 0, S = 0, P = 0;
  PackSelect sel;
  float w;
  CHECK(apiParsePack(body.c_str(), cells, kPackMaxCells, n, S, P, sel, w) == nullptr);
  CHECK_EQ(n, 42);
  CHECK_EQ(S, 4);
  CHECK_EQ(P, 10);
  static PackResult r;
  CHECK(packBuild(cells, n, S, P, sel, w, r));
  StrSink s;
  JsonWriter j(s);
  apiPackJson(j, cells, n, r);
  CHECK(jr::validObject(s.s.c_str()));
  tf::writeArtifact("pack.json", s.s.c_str());
  CHECK(apiParsePack("{\"s\":4,\"p\":10,\"cells\":[1,2]}", cells, kPackMaxCells, n, S, P, sel, w) != nullptr);
  CHECK(apiParsePack("{\"s\":40,\"p\":10,\"cells\":[]}", cells, kPackMaxCells, n, S, P, sel, w) != nullptr);
  PackResult bad;
  packBuild(cells, 3, 4, 10, sel, w, bad);
  StrSink e;
  JsonWriter je(e);
  apiPackJson(je, cells, 3, bad);
  CHECK(jr::validObject(e.s.c_str()));
  tf::writeArtifact("pack_error.json", e.s.c_str());
}
