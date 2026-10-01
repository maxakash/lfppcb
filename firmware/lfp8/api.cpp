#include "api.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "json_reader.h"

namespace lfp8 {

namespace {

const char *ntcName(NtcStatus s) {
  switch (s) {
    case NtcStatus::Ok: return "ok";
    case NtcStatus::Open: return "none";
    case NtcStatus::Short: return "short";
    default: return "invalid";
  }
}

bool isChargeStep(Step s) { return s == Step::Charge || s == Step::StorageCharge; }

// Table of numeric settings exposed by /api/config.
enum class FT : uint8_t { F32, U8, U16 };
struct CfgField {
  const char *name;
  FT t;
  size_t off;
  int dec;
  bool reboot;
};
#define CF(n, t, f, d) {n, FT::t, offsetof(Settings, f), d, false}
const CfgField kFields[] = {
    {"board_id", FT::U8, offsetof(Settings, boardId), 0, true},
    CF("iset", U8, iset, 0),
    CF("v25", F32, v25, 4),
    CF("v_max_chg", F32, vMaxChg, 3),
    CF("v_abs_max", F32, vAbsMax, 3),
    CF("i_term", F32, iTerm, 3),
    CF("t_term_s", U16, tTermS, 0),
    CF("v_min_dis", F32, vMinDis, 3),
    CF("v_precharge", F32, vPrecharge, 3),
    CF("v_cc_min", F32, vCcMin, 3),
    CF("v_dead", F32, vDead, 3),
    CF("t_cell_max", F32, tCellMax, 1),
    CF("t_cell_resume", F32, tCellResume, 1),
    CF("t_cell_fault", F32, tCellFault, 1),
    CF("t_cell_min_chg", F32, tCellMinChg, 1),
    CF("t_board_fan", F32, tBoardFan, 1),
    CF("t_board_max", F32, tBoardMax, 1),
    CF("max_chg_h", F32, maxChgH, 1),
    CF("max_dis_h", F32, maxDisH, 1),
    CF("rest_chg_min", U16, restAfterChgMin, 0),
    CF("rest_dis_min", U16, restAfterDisMin, 0),
    CF("rest_ir_s", U16, restBeforeIrS, 0),
    CF("ir_rest_s", U16, irRestS, 0),
    CF("storage_pct", U8, storagePct, 0),
    CF("captest_ir", U8, capTestIr, 0),
    CF("nominal_mah", F32, nominalMah, 0),
    CF("xchk_tol_v", F32, xchkTolV, 3),
    CF("vin_min", F32, vinMin, 2),
    CF("vin_max", F32, vinMax, 2),
};
#undef CF

double readField(const Settings &s, const CfgField &f) {
  const uint8_t *p = reinterpret_cast<const uint8_t *>(&s) + f.off;
  switch (f.t) {
    case FT::F32: {
      float v;
      memcpy(&v, p, sizeof(v));
      return v;
    }
    case FT::U8: return *p;
    case FT::U16: {
      uint16_t v;
      memcpy(&v, p, sizeof(v));
      return v;
    }
  }
  return 0;
}

bool writeField(Settings &s, const CfgField &f, double v) {
  uint8_t *p = reinterpret_cast<uint8_t *>(&s) + f.off;
  if (!(v == v)) return false;
  switch (f.t) {
    case FT::F32: {
      float x = (float)v;
      memcpy(p, &x, sizeof(x));
      return true;
    }
    case FT::U8:
      if (v < 0 || v > 255 || v != (double)(long)v) return false;
      *p = (uint8_t)v;
      return true;
    case FT::U16: {
      if (v < 0 || v > 65535 || v != (double)(long)v) return false;
      uint16_t x = (uint16_t)v;
      memcpy(p, &x, sizeof(x));
      return true;
    }
  }
  return false;
}

}  // namespace

// ------------------------------------------------------------------ status ---
void apiStatusJson(JsonWriter &j, const StatusSnapshot &s, const NetInfo &n) {
  const BoardStatus &b = s.b;
  j.beginObject();
  j.kvInt("id", n.id).kv("fw", n.fw).kv("host", n.host).kv("ip", n.ip).kv("mode", n.mode);
  j.kvInt("rssi", n.rssi).kvBool("dup_id", n.dupId).kvInt("uptime", b.uptimeS);
  j.key("board").beginObject();
  j.key("vin").num(b.vinValid ? b.vin : NAN, 3);
  j.key("tb").num(b.tBoardValid ? b.tBoard : NAN, 1);
  j.kvBool("safe", b.safeStable).kvBool("safe_rb", b.safeRb).kvBool("hb", b.hbAllowed);
  j.kvBool("power", b.powerAllowed).kvBool("meas", b.measuring);
  j.kvInt("fan", b.fanPct).kvInt("iset", b.iset).kvInt("iset_eff", b.isetEff).kv("iset_a", b.isetA, 3);
  j.kvBool("hot", b.boardHot).kvBool("maint", b.maintenance).kvBool("ident", b.identify);
  j.kv("alarm", boardAlarmName(b.alarm)).kv("alarm_text", b.alarmText).kv("cond", b.cond);
  j.kvInt("loop_max_us", b.loopMaxUs).kvInt("overruns", b.loopOverruns).kvInt("i2c_err", b.i2cErrors);
  j.kvInt("safe_losses", b.safeLosses).kvInt("scan_ms", b.scanCycleMs);
  j.kvBool("ir_busy", b.irBusy).kvInt("ir_ch", b.irCh);
  j.kv("latch_reset", b.latchReset == 2 ? "active" : (b.latchReset == 1 ? "pending" : ""));
  j.endObject();
  j.key("ch").beginArray();
  for (int k = 0; k < kNumCh; k++) {
    const ChStatus &c = s.ch[k];
    const ChResult &r = c.res;
    j.beginObject();
    j.kvInt("k", c.k).kvInt("g", (n.id - 1) * kNumCh + c.k);
    j.kv("st", chStateName(c.st)).kv("prog", programName(c.prog)).kv("step", stepName(c.step));
    j.kv("pause", pauseName(c.pause)).kvBool("valid", c.valid);
    j.key("v").num(c.valid ? c.v : NAN, 4);
    j.key("i").num(c.valid ? c.i : NAN, 4);
    j.key("t").num(c.valid && c.ntc == NtcStatus::Ok ? c.tCell : NAN, 1);
    j.kv("ntc", ntcName(c.ntc));
    j.key("vchk").num(c.vchk, 3).key("vbm").num(c.vbm, 4).kvBool("bm_warn", c.bmWarn);
    j.key("contact_mohm").num(c.contactMohm, 0).kvBool("contact_warn", c.contactWarn);
    j.kvBool("ov_pending", c.ovPending).kvInt("pulse_pct", c.pulseDuty);
    j.kvBool("chg", c.chgOn).kvBool("dis", c.disOn);
    bool chg = isChargeStep(c.step);
    j.key("mah").num(c.prog == Program::None ? NAN : (chg ? c.stepChgMah : c.stepDisMah), 1);
    j.key("mwh").num(c.prog == Program::None ? NAN : (chg ? c.stepChgMwh : c.stepDisMwh), 1);
    j.key("target_mah").num(c.step == Step::StorageCharge ? c.storeTargetMah : NAN, 1);
    j.kvInt("el", c.elapsedS).kvInt("step_el", c.stepElapsedS);
    j.key("cap_mah").num(r.hasCap ? r.capMah : NAN, 1);
    j.key("cap_mwh").num(r.hasCap ? r.capMwh : NAN, 1);
    j.kvInt("cap_s", r.hasCap ? r.capS : 0).kv("dis_term", termName(r.disTerm));
    j.key("chg_mah").num(r.hasChg ? r.chgMah : NAN, 1);
    j.key("chg_mwh").num(r.hasChg ? r.chgMwh : NAN, 1);
    j.kv("chg_term", termName(r.chgTerm));
    j.key("store_mah").num(r.hasStore ? r.storeMah : NAN, 1);
    j.key("ir_ohm").num(r.hasIr ? r.irOhmicMohm : NAN, 2);
    j.key("ir_dc").num(r.hasIr ? r.irDcMohm : NAN, 2);
    j.key("ir_i").num(r.hasIr ? r.irI : NAN, 3);
    j.key("ir_v0").num(r.hasIr ? r.irV0 : NAN, 4);
    j.kv("ir_mode", r.hasIr ? (r.irChargePulse ? "chg" : "dis") : "");
    j.kvInt("ir_at", r.hasIr ? r.irAtS : 0);
    j.kv("msg", c.msg);
    j.endObject();
  }
  j.endArray();
  j.endObject();
}

// ------------------------------------------------------------------ config ---
void apiConfigJson(JsonWriter &j, const Settings &s) {
  j.beginObject();
  for (const CfgField &f : kFields) j.key(f.name).num(readField(s, f), f.dec);
  j.kv("wifi_ssid", s.wifiSsid);
  j.kvBool("wifi_pass_set", s.wifiPass[0] != 0);
  j.kvBool("admin_pass_set", s.adminPass[0] != 0);
  j.key("cal_v").beginArray();
  for (int k = 0; k < kNumCh; k++) j.num(s.calV[k], 5);
  j.endArray();
  j.key("cal_i").beginArray();
  for (int k = 0; k < kNumCh; k++) j.num(s.calI[k], 5);
  j.endArray();
  j.endObject();
}

const char *apiApplyConfig(const char *body, Settings &s, bool *reboot, int *changed) {
  if (reboot) *reboot = false;
  if (changed) *changed = 0;
  if (!jr::validObject(body)) return "body is not a JSON object";
  Settings n = s;
  int cnt = 0;
  bool rb = false;
  for (const CfgField &f : kFields) {
    double v;
    if (!jr::has(body, f.name)) continue;
    if (!jr::getNum(body, f.name, v) || !writeField(n, f, v)) {
      static char err[48];
      snprintf(err, sizeof(err), "invalid value for %s", f.name);
      return err;
    }
    if (f.reboot && v != readField(s, f)) rb = true;
    cnt++;
  }
  if (jr::has(body, "wifi_ssid")) {
    if (!jr::getStr(body, "wifi_ssid", n.wifiSsid, sizeof(n.wifiSsid))) return "invalid wifi_ssid";
    if (strcmp(n.wifiSsid, s.wifiSsid) != 0) rb = true;
    cnt++;
  }
  if (jr::has(body, "wifi_pass")) {
    if (!jr::getStr(body, "wifi_pass", n.wifiPass, sizeof(n.wifiPass))) return "invalid wifi_pass";
    if (n.wifiPass[0] && strlen(n.wifiPass) < 8) return "wifi_pass must have >= 8 characters";
    rb = true;
    cnt++;
  }
  if (jr::has(body, "admin_pass")) {
    if (!jr::getStr(body, "admin_pass", n.adminPass, sizeof(n.adminPass))) return "invalid admin_pass";
    cnt++;
  }
  const char *arrays[2] = {"cal_v", "cal_i"};
  for (int a = 0; a < 2; a++) {
    if (!jr::has(body, arrays[a])) continue;
    double v[kNumCh];
    int m = jr::getNumArray(body, arrays[a], v, kNumCh);
    if (m != kNumCh) return a == 0 ? "cal_v needs 8 numbers" : "cal_i needs 8 numbers";
    for (int k = 0; k < kNumCh; k++) {
      if (!(v[k] >= 0.90 && v[k] <= 1.10)) return "calibration factors must be within 0.90..1.10";
      (a == 0 ? n.calV : n.calI)[k] = (float)v[k];
    }
    cnt++;
  }
  settingsSanitize(n);
  s = n;
  if (reboot) *reboot = rb;
  if (changed) *changed = cnt;
  return nullptr;
}

// ---------------------------------------------------------------- commands ---
const char *apiParseCommand(const char *body, Command &cmd, bool *reboot) {
  if (reboot) *reboot = false;
  if (!jr::validObject(body)) return "body is not a JSON object";
  char name[16];
  if (!jr::getStr(body, "cmd", name, sizeof(name))) return "missing \"cmd\"";
  cmd = Command();
  long ch = 0;
  bool hasCh = jr::getInt(body, "ch", ch);
  double value = 0;
  bool hasValue = jr::getNum(body, "value", value);
  cmd.value = value;
  Program p;
  if (programFromName(name, p)) {
    cmd.type = Command::Type::Start;
    cmd.prog = p;
  } else if (!strcmp(name, "stop")) {
    cmd.type = hasCh ? Command::Type::Stop : Command::Type::StopAll;
  } else if (!strcmp(name, "stopall")) {
    cmd.type = Command::Type::StopAll;
    return nullptr;
  } else if (!strcmp(name, "reset")) {
    cmd.type = hasCh ? Command::Type::Reset : Command::Type::Ack;
    if (!hasCh) return nullptr;
  } else if (!strcmp(name, "ack")) {
    cmd.type = Command::Type::Ack;
    return nullptr;
  } else if (!strcmp(name, "identify")) {
    cmd.type = Command::Type::Identify;
    return nullptr;
  } else if (!strcmp(name, "iset")) {
    if (!hasValue) return "iset needs \"value\" 0..7";
    cmd.type = Command::Type::Iset;
    return nullptr;
  } else if (!strcmp(name, "calv") || !strcmp(name, "cali")) {
    if (!hasValue) return "calibration needs \"value\" (reference)";
    cmd.type = name[3] == 'v' ? Command::Type::CalV : Command::Type::CalI;
  } else if (!strcmp(name, "caltb")) {
    if (!hasValue) return "caltb needs \"value\" (deg C)";
    cmd.type = Command::Type::CalTboard;
    return nullptr;
  } else if (!strcmp(name, "reboot")) {
    if (reboot) *reboot = true;
    cmd.type = Command::Type::Reboot;  // executed by the caller (not the controller)
    return nullptr;
  } else {
    return "unknown cmd";
  }
  if (cmd.type == Command::Type::StopAll) return nullptr;
  if (!hasCh || ch < 1 || ch > kNumCh) return "\"ch\" must be 1..8 (local channel)";
  cmd.ch = (uint8_t)ch;
  return nullptr;
}

// ----------------------------------------------------------------- history ---
void apiHistoryBegin(JsonWriter &j, uint8_t ch, uint8_t boardId, int count, uint32_t ageS) {
  j.beginObject();
  j.kvInt("ch", ch).kvInt("g", (boardId - 1) * kNumCh + ch).kvInt("interval", kHistIntervalMs / 1000);
  j.kvInt("count", count).kvInt("age_s", ageS);
  j.kv("fmt", "mv,ma pairs, oldest first");
  j.key("d").beginArray();
}

void apiHistorySamples(JsonWriter &j, const HistSample *s, int n) {
  for (int k = 0; k < n; k++) {
    if (s[k].mv == kHistNoData) {
      j.null().null();
    } else {
      j.integer(s[k].mv).integer(s[k].ma);
    }
  }
}

void apiHistoryEnd(JsonWriter &j) { j.endArray().endObject(); }

// ------------------------------------------------------------------- peers ---
void apiPeersJson(JsonWriter &j, const PeerTable &t, const NetInfo &self, uint32_t nowMs) {
  j.beginObject();
  j.key("self").beginObject().kvInt("id", self.id).kv("ip", self.ip).kv("fw", self.fw).kv("host", self.host).endObject();
  j.kvBool("dup_id", self.dupId);
  j.key("peers").beginArray();
  for (int i = 0; i < PeerTable::capacity(); i++) {
    const Peer &p = t.at(i);
    if (!p.used) continue;
    j.beginObject().kvInt("id", p.id).kv("ip", p.ip).kv("fw", p.fw).kv("age", (nowMs - p.lastSeenMs) / 1000.0, 1).endObject();
  }
  j.endArray();
  j.endObject();
}

// --------------------------------------------------------------------- CSV ---
void apiCsvHeader(Sink &s) {
  s.puts(
      "board,ch,cell,state,program,step,v,i,t_cell,cap_mah,cap_mwh,cap_s,dis_term,chg_mah,chg_mwh,chg_term,"
      "store_mah,ir_dc_mohm,ir_ohmic_mohm,ir_i_a,ir_v0,ir_mode,elapsed_s,msg\r\n");
}

static void csvNum(Sink &s, bool have, double v, int dec) {
  if (have && v == v) {
    char b[24];
    snprintf(b, sizeof(b), "%.*f", dec, v);
    s.puts(b);
  }
  s.puts(",");
}

void apiCsvRow(Sink &s, uint8_t boardId, const ChStatus &c) {
  const ChResult &r = c.res;
  char b[48];
  snprintf(b, sizeof(b), "%u,%u,%u,", (unsigned)boardId, (unsigned)c.k, (unsigned)((boardId - 1) * kNumCh + c.k));
  s.puts(b);
  s.puts(chStateName(c.st));
  s.puts(",");
  s.puts(programName(c.prog));
  s.puts(",");
  s.puts(stepName(c.step));
  s.puts(",");
  csvNum(s, c.valid, c.v, 4);
  csvNum(s, c.valid, c.i, 4);
  csvNum(s, c.valid && c.ntc == NtcStatus::Ok, c.tCell, 1);
  csvNum(s, r.hasCap, r.capMah, 1);
  csvNum(s, r.hasCap, r.capMwh, 1);
  csvNum(s, r.hasCap, r.capS, 0);
  s.puts(termName(r.disTerm));
  s.puts(",");
  csvNum(s, r.hasChg, r.chgMah, 1);
  csvNum(s, r.hasChg, r.chgMwh, 1);
  s.puts(termName(r.chgTerm));
  s.puts(",");
  csvNum(s, r.hasStore, r.storeMah, 1);
  csvNum(s, r.hasIr, r.irDcMohm, 2);
  csvNum(s, r.hasIr, r.irOhmicMohm, 2);
  csvNum(s, r.hasIr, r.irI, 3);
  csvNum(s, r.hasIr, r.irV0, 4);
  s.puts(r.hasIr ? (r.irChargePulse ? "chg" : "dis") : "");
  s.puts(",");
  csvNum(s, true, c.elapsedS, 0);
  csvField(s, c.msg);
  s.puts("\r\n");
}

// -------------------------------------------------------------------- pack ---
const char *apiParsePack(const char *body, PackCell *cells, int maxCells, int &n, int &S, int &P,
                         PackSelect &sel, float &irWeight) {
  if (!jr::validObject(body)) return "body is not a JSON object";
  long s = 0, p = 0;
  if (!jr::getInt(body, "s", s) || !jr::getInt(body, "p", p)) return "need integer \"s\" and \"p\"";
  if (s < 1 || s > kPackMaxS || p < 1 || p > kPackMaxP) return "S must be 1..32 and P 1..64";
  S = (int)s;
  P = (int)p;
  char selName[8] = "top";
  if (jr::has(body, "sel") && !jr::getStr(body, "sel", selName, sizeof(selName))) return "bad \"sel\"";
  sel = !strcmp(selName, "tight") ? PackSelect::Tight : PackSelect::Top;
  double w = 0.1;
  if (jr::has(body, "irw") && (!jr::getNum(body, "irw", w) || w < 0 || w > 10)) return "irw must be 0..10";
  irWeight = (float)w;
  static double flat[kPackMaxCells * 3];
  int m = jr::getNumArray(body, "cells", flat, kPackMaxCells * 3);
  if (m == -2) return "too many cells";
  if (m < 0 || m % 3 != 0) return "\"cells\" must be a flat array [id,cap_mah,ir_mohm, ...]";
  n = m / 3;
  if (n > maxCells) return "too many cells";
  for (int k = 0; k < n; k++) {
    cells[k].id = (int)flat[3 * k];
    cells[k].capMah = (float)flat[3 * k + 1];
    cells[k].irMohm = (float)flat[3 * k + 2];
  }
  return nullptr;
}

void apiPackJson(JsonWriter &j, const PackCell *cells, int n, const PackResult &r) {
  j.beginObject();
  j.kvBool("ok", r.ok);
  if (!r.ok) {
    j.kv("err", r.err).endObject();
    return;
  }
  j.kvInt("s", r.s).kvInt("p", r.p);
  j.kv("cap_spread_pct", r.capSpreadPct, 3).kv("ir_spread_pct", r.irSpreadPct, 2);
  j.kv("pack_cap_mah", r.packCapMah, 1).kv("pack_ir_mohm", r.packIrMohm, 2).kvInt("swaps", r.swaps);
  j.key("groups").beginArray();
  for (int g = 0; g < r.s; g++) {
    j.beginObject().kvInt("n", g + 1).kv("cap_mah", r.groupCap[g], 1).kv("ir_mohm", r.groupIr[g], 3);
    j.key("cells").beginArray();
    for (int k = 0; k < n; k++)
      if (r.group[k] == g) j.beginArray().integer(cells[k].id).num(cells[k].capMah, 1).num(cells[k].irMohm > 0 ? cells[k].irMohm : NAN, 2).endArray();
    j.endArray().endObject();
  }
  j.endArray();
  j.key("unused").beginArray();
  for (int k = 0; k < n; k++)
    if (r.group[k] < 0) j.integer(cells[k].id);
  j.endArray();
  j.endObject();
}

void apiIrTraceJson(JsonWriter &j, uint8_t boardId, uint8_t ch, const IrResult &r, const IrPoint *v, int nv,
                    const IrPoint *i, int ni) {
  j.beginObject();
  j.kvBool("valid", nv > 0 || ni > 0);
  j.kvInt("ch", ch).kvInt("g", ch ? (boardId - 1) * kNumCh + ch : 0);
  j.kvBool("ok", r.ok).kv("err", r.err ? r.err : "").kv("mode", r.chargePulse ? "chg" : "dis");
  j.kv("v0", r.v0, 4).kv("i0", r.i0, 4);
  j.kv("v_10ms", r.vOhmic, 4).kv("i_10ms", r.iOhmic, 4).kv("v_1s", r.vDc, 4).kv("i_1s", r.iDc, 4);
  j.key("r_ohmic_mohm").num(r.ok ? r.rOhmic * 1000.0 : NAN, 3);
  j.key("r_dc_mohm").num(r.ok ? r.rDc * 1000.0 : NAN, 3);
  j.key("v").beginArray();
  for (int k = 0; k < nv; k++) j.beginArray().num(v[k].t * 1000.0, 2).num(v[k].y, 4).endArray();
  j.endArray();
  j.key("i").beginArray();
  for (int k = 0; k < ni; k++) j.beginArray().num(i[k].t * 1000.0, 2).num(i[k].y, 4).endArray();
  j.endArray();
  j.endObject();
}

void apiResultJson(JsonWriter &j, const char *err) {
  j.beginObject().kvBool("ok", err == nullptr);
  if (err) j.kv("err", err);
  j.endObject();
}

}  // namespace lfp8
