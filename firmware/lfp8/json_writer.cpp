#include "json_writer.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace lfp8 {

void Sink::puts(const char *s) { write(s, strlen(s)); }

void BufSink::write(const char *s, size_t n) {
  if (cap_ == 0) {
    overflow_ = true;
    return;
  }
  size_t room = cap_ - 1 - len_;
  if (n > room) {
    n = room;
    overflow_ = true;
  }
  memcpy(buf_ + len_, s, n);
  len_ += n;
  buf_[len_] = 0;
}

void JsonWriter::raw(const char *s) { s_.write(s, strlen(s)); }

void JsonWriter::sep() {
  if (afterKey_) {
    afterKey_ = false;
    return;
  }
  if (depth_ > 0) {
    if (!first_[depth_ - 1]) s_.write(",", 1);
    first_[depth_ - 1] = false;
  }
}

JsonWriter &JsonWriter::beginObject() {
  sep();
  s_.write("{", 1);
  if (depth_ < kMaxDepth) first_[depth_] = true;
  depth_++;
  return *this;
}

JsonWriter &JsonWriter::endObject() {
  s_.write("}", 1);
  if (depth_ > 0) depth_--;
  return *this;
}

JsonWriter &JsonWriter::beginArray() {
  sep();
  s_.write("[", 1);
  if (depth_ < kMaxDepth) first_[depth_] = true;
  depth_++;
  return *this;
}

JsonWriter &JsonWriter::endArray() {
  s_.write("]", 1);
  if (depth_ > 0) depth_--;
  return *this;
}

JsonWriter &JsonWriter::key(const char *k) {
  str(k);
  s_.write(":", 1);
  afterKey_ = true;
  return *this;
}

JsonWriter &JsonWriter::str(const char *v) {
  sep();
  s_.write("\"", 1);
  if (v) {
    const char *run = v;
    for (const char *p = v; *p; p++) {
      unsigned char c = (unsigned char)*p;
      const char *esc = nullptr;
      char ubuf[8];
      switch (c) {
        case '"': esc = "\\\""; break;
        case '\\': esc = "\\\\"; break;
        case '\n': esc = "\\n"; break;
        case '\r': esc = "\\r"; break;
        case '\t': esc = "\\t"; break;
        case '\b': esc = "\\b"; break;
        case '\f': esc = "\\f"; break;
        default:
          if (c < 0x20) {
            snprintf(ubuf, sizeof(ubuf), "\\u%04x", c);
            esc = ubuf;
          }
      }
      if (esc) {
        if (p > run) s_.write(run, (size_t)(p - run));
        raw(esc);
        run = p + 1;
      }
    }
    if (*run) raw(run);
  }
  s_.write("\"", 1);
  return *this;
}

JsonWriter &JsonWriter::num(double v, int decimals) {
  if (isnan(v) || isinf(v)) return null();
  sep();
  char b[40];
  if (decimals < 0) decimals = 0;
  if (decimals > 9) decimals = 9;
  int n = snprintf(b, sizeof(b), "%.*f", decimals, v);
  if (n <= 0 || n >= (int)sizeof(b)) {
    raw("null");
    return *this;
  }
  // "-0", "-0.000" -> "0", "0.000"
  bool allZero = true;
  for (const char *p = b; *p; p++)
    if (*p >= '1' && *p <= '9') allZero = false;
  raw(allZero && b[0] == '-' ? b + 1 : b);
  return *this;
}

JsonWriter &JsonWriter::integer(long long v) {
  sep();
  char b[24];
  snprintf(b, sizeof(b), "%lld", v);
  raw(b);
  return *this;
}

JsonWriter &JsonWriter::uinteger(unsigned long long v) {
  sep();
  char b[24];
  snprintf(b, sizeof(b), "%llu", v);
  raw(b);
  return *this;
}

JsonWriter &JsonWriter::boolean(bool v) {
  sep();
  raw(v ? "true" : "false");
  return *this;
}

JsonWriter &JsonWriter::null() {
  sep();
  raw("null");
  return *this;
}

void csvField(Sink &s, const char *v) {
  if (!v) v = "";
  bool quote = strpbrk(v, ",\"\r\n") != nullptr;
  if (!quote) {
    s.puts(v);
    return;
  }
  s.write("\"", 1);
  for (const char *p = v; *p; p++) {
    if (*p == '"') s.write("\"\"", 2);
    else s.write(p, 1);
  }
  s.write("\"", 1);
}

}  // namespace lfp8
