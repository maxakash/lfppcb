// Multi-board discovery (INTERFACE.md §9): UDP broadcast on port 45454 every
// 2 s: {"t":"hello","id":<id>,"ip":"x.x.x.x","fw":"x.y.z"}
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "lfp8_config.h"

namespace lfp8 {

struct Peer {
  uint8_t id = 0;
  char ip[16] = {0};
  char fw[16] = {0};
  uint32_t lastSeenMs = 0;
  bool used = false;
};

// Format the hello datagram. Returns its length (0 on overflow).
size_t formatHello(char *buf, size_t cap, uint8_t id, const char *ip, const char *fw);
// Parse a hello datagram (validates JSON, id 1..32, dotted-quad IP).
bool parseHello(const char *msg, uint8_t &id, char *ip, size_t ipLen, char *fw, size_t fwLen);
bool validIpv4(const char *s);

class PeerTable {
 public:
  // Insert/refresh a peer (keyed by IP). The caller filters its own datagrams.
  void seen(uint8_t id, const char *ip, const char *fw, uint32_t nowMs);
  void expire(uint32_t nowMs);
  int count() const;
  const Peer &at(int i) const { return peers_[i]; }
  static constexpr int capacity() { return kMaxPeers; }
  // True if another board announces our id from a different IP.
  bool duplicateId(uint8_t selfId, const char *selfIp) const;

 private:
  Peer peers_[kMaxPeers];
};

}  // namespace lfp8
