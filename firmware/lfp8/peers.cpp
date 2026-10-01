#include "peers.h"

#include <stdio.h>
#include <string.h>

#include "json_reader.h"
#include "json_writer.h"

namespace lfp8 {

size_t formatHello(char *buf, size_t cap, uint8_t id, const char *ip, const char *fw) {
  BufSink s(buf, cap);
  JsonWriter j(s);
  j.beginObject().kv("t", "hello").kvInt("id", id).kv("ip", ip).kv("fw", fw).endObject();
  return s.overflow() ? 0 : s.len();
}

bool validIpv4(const char *s) {
  int parts = 0;
  const char *p = s;
  while (parts < 4) {
    if (*p < '0' || *p > '9') return false;
    int v = 0, digits = 0;
    while (*p >= '0' && *p <= '9') {
      v = v * 10 + (*p - '0');
      if (++digits > 3) return false;
      p++;
    }
    if (v > 255) return false;
    parts++;
    if (parts < 4) {
      if (*p != '.') return false;
      p++;
    }
  }
  return *p == 0;
}

bool parseHello(const char *msg, uint8_t &id, char *ip, size_t ipLen, char *fw, size_t fwLen) {
  if (!jr::validObject(msg)) return false;
  char t[8];
  if (!jr::getStr(msg, "t", t, sizeof(t)) || strcmp(t, "hello") != 0) return false;
  long v;
  if (!jr::getInt(msg, "id", v) || v < 1 || v > 32) return false;
  if (!jr::getStr(msg, "ip", ip, ipLen) || !validIpv4(ip)) return false;
  if (!jr::getStr(msg, "fw", fw, fwLen)) {
    strncpy(fw, "?", fwLen);
  }
  id = (uint8_t)v;
  return true;
}

void PeerTable::seen(uint8_t id, const char *ip, const char *fw, uint32_t nowMs) {
  int slot = -1, freeSlot = -1, oldest = 0;
  for (int i = 0; i < kMaxPeers; i++) {
    if (peers_[i].used && strcmp(peers_[i].ip, ip) == 0) {
      slot = i;
      break;
    }
    if (!peers_[i].used && freeSlot < 0) freeSlot = i;
    if (nowMs - peers_[i].lastSeenMs > nowMs - peers_[oldest].lastSeenMs) oldest = i;
  }
  if (slot < 0) slot = freeSlot >= 0 ? freeSlot : oldest;
  Peer &p = peers_[slot];
  p.used = true;
  p.id = id;
  strncpy(p.ip, ip, sizeof(p.ip) - 1);
  p.ip[sizeof(p.ip) - 1] = 0;
  strncpy(p.fw, fw, sizeof(p.fw) - 1);
  p.fw[sizeof(p.fw) - 1] = 0;
  p.lastSeenMs = nowMs;
}

void PeerTable::expire(uint32_t nowMs) {
  for (int i = 0; i < kMaxPeers; i++)
    if (peers_[i].used && nowMs - peers_[i].lastSeenMs > kPeerTimeoutMs) peers_[i].used = false;
}

int PeerTable::count() const {
  int n = 0;
  for (int i = 0; i < kMaxPeers; i++) n += peers_[i].used ? 1 : 0;
  return n;
}

bool PeerTable::duplicateId(uint8_t selfId, const char *selfIp) const {
  for (int i = 0; i < kMaxPeers; i++)
    if (peers_[i].used && peers_[i].id == selfId && strcmp(peers_[i].ip, selfIp) != 0) return true;
  return false;
}

}  // namespace lfp8
