// NVS persistence (Preferences): settings blob + last results per channel.
#if defined(ARDUINO)
#include <Preferences.h>
#include <string.h>

#include "app_esp32.h"

namespace lfp8 {
namespace app {

namespace {
const char *kNs = "lfp8";
constexpr uint32_t kResMagic = 0x52534C54u;  // "RSLT"

struct ResultsBlob {
  uint32_t magic;
  uint32_t size;
  ChResult r[kNumCh];
  uint32_t crc;
};

uint32_t crc32(const void *data, size_t len) {
  const uint8_t *p = static_cast<const uint8_t *>(data);
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    c ^= p[i];
    for (int b = 0; b < 8; b++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}
}  // namespace

bool nvsLoadSettings(Settings &s) {
  Preferences p;
  if (!p.begin(kNs, true)) return false;
  bool ok = false;
  if (p.getBytesLength("cfg") == sizeof(Settings)) {
    Settings t;
    p.getBytes("cfg", &t, sizeof(t));
    if (t.magic == kSettingsMagic && t.version == kSettingsVersion && t.crc == settingsCrc(t)) {
      s = t;
      ok = true;
    }
  }
  p.end();
  return ok;
}

bool nvsSaveSettings(const Settings &in) {
  Settings s = in;
  s.crc = settingsCrc(s);
  Preferences p;
  if (!p.begin(kNs, false)) return false;
  bool ok = p.putBytes("cfg", &s, sizeof(s)) == sizeof(s);
  p.end();
  return ok;
}

bool nvsLoadResults(ChResult r[kNumCh]) {
  Preferences p;
  if (!p.begin(kNs, true)) return false;
  bool ok = false;
  static ResultsBlob b;
  if (p.getBytesLength("res") == sizeof(b)) {
    p.getBytes("res", &b, sizeof(b));
    if (b.magic == kResMagic && b.size == sizeof(b) && b.crc == crc32(b.r, sizeof(b.r))) {
      memcpy(r, b.r, sizeof(b.r));
      ok = true;
    }
  }
  p.end();
  return ok;
}

bool nvsSaveResults(const ChResult r[kNumCh]) {
  static ResultsBlob b;
  b.magic = kResMagic;
  b.size = sizeof(b);
  memcpy(b.r, r, sizeof(b.r));
  b.crc = crc32(b.r, sizeof(b.r));
  Preferences p;
  if (!p.begin(kNs, false)) return false;
  bool ok = p.putBytes("res", &b, sizeof(b)) == sizeof(b);
  p.end();
  return ok;
}

}  // namespace app
}  // namespace lfp8
#endif  // ARDUINO
