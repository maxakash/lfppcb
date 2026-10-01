#include "json_reader.h"

#include <stdlib.h>
#include <string.h>

namespace lfp8 {
namespace jr {

namespace {
constexpr int kMaxDepth = 16;

const char *ws(const char *p) {
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  return p;
}

// Skip a string starting at '"'; returns pointer after the closing quote or nullptr.
const char *skipString(const char *p) {
  if (*p != '"') return nullptr;
  p++;
  while (*p && *p != '"') {
    if ((unsigned char)*p < 0x20) return nullptr;
    if (*p == '\\') {
      p++;
      if (!*p) return nullptr;
      if (*p == 'u') {
        for (int i = 1; i <= 4; i++)
          if (!strchr("0123456789abcdefABCDEF", p[i]) || !p[i]) return nullptr;
        p += 4;
      } else if (!strchr("\"\\/bfnrt", *p)) {
        return nullptr;
      }
    }
    p++;
  }
  return *p == '"' ? p + 1 : nullptr;
}

const char *skipNumber(const char *p) {
  const char *s = p;
  if (*p == '-') p++;
  if (*p == '0') {
    p++;
  } else if (*p >= '1' && *p <= '9') {
    while (*p >= '0' && *p <= '9') p++;
  } else {
    return nullptr;
  }
  if (*p == '.') {
    p++;
    if (!(*p >= '0' && *p <= '9')) return nullptr;
    while (*p >= '0' && *p <= '9') p++;
  }
  if (*p == 'e' || *p == 'E') {
    p++;
    if (*p == '+' || *p == '-') p++;
    if (!(*p >= '0' && *p <= '9')) return nullptr;
    while (*p >= '0' && *p <= '9') p++;
  }
  return p > s ? p : nullptr;
}

const char *skipValue(const char *p, int depth);

const char *skipContainer(const char *p, int depth, char open, char close) {
  if (depth > kMaxDepth || *p != open) return nullptr;
  p = ws(p + 1);
  if (*p == close) return p + 1;
  for (;;) {
    if (open == '{') {
      p = skipString(p);
      if (!p) return nullptr;
      p = ws(p);
      if (*p != ':') return nullptr;
      p = ws(p + 1);
    }
    p = skipValue(p, depth + 1);
    if (!p) return nullptr;
    p = ws(p);
    if (*p == ',') {
      p = ws(p + 1);
      continue;
    }
    if (*p == close) return p + 1;
    return nullptr;
  }
}

const char *skipValue(const char *p, int depth) {
  p = ws(p);
  switch (*p) {
    case '"': return skipString(p);
    case '{': return skipContainer(p, depth, '{', '}');
    case '[': return skipContainer(p, depth, '[', ']');
    case 't': return strncmp(p, "true", 4) == 0 ? p + 4 : nullptr;
    case 'f': return strncmp(p, "false", 5) == 0 ? p + 5 : nullptr;
    case 'n': return strncmp(p, "null", 4) == 0 ? p + 4 : nullptr;
    default: return skipNumber(p);
  }
}

bool keyEquals(const char *q, const char *key) {
  // q points at the opening quote of a key; compares raw (keys are ASCII).
  q++;
  size_t n = strlen(key);
  return strncmp(q, key, n) == 0 && q[n] == '"';
}

// Find the value for a top-level key. Returns pointer to the value or nullptr.
const char *find(const char *json, const char *key) {
  if (!json) return nullptr;
  const char *p = ws(json);
  if (*p != '{') return nullptr;
  p = ws(p + 1);
  if (*p == '}') return nullptr;
  for (;;) {
    if (*p != '"') return nullptr;
    const char *k = p;
    p = skipString(p);
    if (!p) return nullptr;
    p = ws(p);
    if (*p != ':') return nullptr;
    p = ws(p + 1);
    if (keyEquals(k, key)) return p;
    p = skipValue(p, 1);
    if (!p) return nullptr;
    p = ws(p);
    if (*p == ',') {
      p = ws(p + 1);
      continue;
    }
    return nullptr;
  }
}
}  // namespace

bool validObject(const char *json) {
  if (!json) return false;
  const char *p = ws(json);
  if (*p != '{') return false;
  p = skipValue(p, 0);
  if (!p) return false;
  return *ws(p) == 0;
}

bool has(const char *json, const char *key) { return find(json, key) != nullptr; }

bool getNum(const char *json, const char *key, double &out) {
  const char *p = find(json, key);
  if (!p) return false;
  const char *e = skipNumber(p);
  if (!e) return false;
  out = strtod(p, nullptr);
  return true;
}

bool getInt(const char *json, const char *key, long &out) {
  double d;
  if (!getNum(json, key, d)) return false;
  if (d < -2147483648.0 || d > 2147483647.0) return false;
  out = (long)d;
  return (double)out == d;
}

bool getBool(const char *json, const char *key, bool &out) {
  const char *p = find(json, key);
  if (!p) return false;
  if (strncmp(p, "true", 4) == 0) out = true;
  else if (strncmp(p, "false", 5) == 0) out = false;
  else if (*p == '0' || *p == '1') out = (*p == '1');
  else return false;
  return true;
}

bool getStr(const char *json, const char *key, char *out, size_t outLen) {
  const char *p = find(json, key);
  if (!p || *p != '"' || outLen == 0) return false;
  if (!skipString(p)) return false;
  p++;
  size_t n = 0;
  while (*p && *p != '"') {
    char c = *p;
    if (c == '\\') {
      p++;
      switch (*p) {
        case 'n': c = '\n'; break;
        case 'r': c = '\r'; break;
        case 't': c = '\t'; break;
        case 'b': c = '\b'; break;
        case 'f': c = '\f'; break;
        case 'u': {
          char h[5] = {p[1], p[2], p[3], p[4], 0};
          long v = strtol(h, nullptr, 16);
          c = (v > 0 && v < 0x80) ? (char)v : '?';
          p += 4;
          break;
        }
        default: c = *p; break;  // \" \\ \/
      }
    }
    if (n + 1 >= outLen) return false;  // does not fit
    out[n++] = c;
    p++;
  }
  out[n] = 0;
  return true;
}

int getNumArray(const char *json, const char *key, double *out, int maxN) {
  const char *p = find(json, key);
  if (!p || *p != '[') return -1;
  if (!skipValue(p, 1)) return -1;
  p = ws(p + 1);
  int n = 0;
  if (*p == ']') return 0;
  for (;;) {
    const char *e = skipNumber(p);
    if (!e) return -1;
    if (n < maxN) out[n] = strtod(p, nullptr);
    n++;
    p = ws(e);
    if (*p == ',') {
      p = ws(p + 1);
      continue;
    }
    if (*p == ']') break;
    return -1;
  }
  return n > maxN ? -2 : n;  // -2: more elements than fit
}

}  // namespace jr
}  // namespace lfp8
