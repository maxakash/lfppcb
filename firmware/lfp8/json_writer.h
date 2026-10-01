// Tiny streaming JSON emitter (no heap). Writes into a Sink, so the same
// code can fill a fixed buffer, a std::string (tests) or an HTTP chunk.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace lfp8 {

class Sink {
 public:
  virtual ~Sink() {}
  virtual void write(const char *s, size_t n) = 0;
  void puts(const char *s);
};

// Fixed-size buffer sink. Always NUL-terminated; overflow() reports truncation.
class BufSink : public Sink {
 public:
  BufSink(char *buf, size_t cap) : buf_(buf), cap_(cap) {
    if (cap_) buf_[0] = 0;
  }
  void write(const char *s, size_t n) override;
  size_t len() const { return len_; }
  bool overflow() const { return overflow_; }
  const char *c_str() const { return buf_; }
  void clear() {
    len_ = 0;
    overflow_ = false;
    if (cap_) buf_[0] = 0;
  }

 private:
  char *buf_;
  size_t cap_;
  size_t len_ = 0;
  bool overflow_ = false;
};

class JsonWriter {
 public:
  explicit JsonWriter(Sink &s) : s_(s) {}
  JsonWriter &beginObject();
  JsonWriter &endObject();
  JsonWriter &beginArray();
  JsonWriter &endArray();
  JsonWriter &key(const char *k);
  JsonWriter &str(const char *v);
  JsonWriter &num(double v, int decimals);  // NaN/Inf -> null
  JsonWriter &integer(long long v);
  JsonWriter &uinteger(unsigned long long v);
  JsonWriter &boolean(bool v);
  JsonWriter &null();
  // key + value helpers
  JsonWriter &kv(const char *k, const char *v) { return key(k).str(v); }
  JsonWriter &kv(const char *k, double v, int decimals) { return key(k).num(v, decimals); }
  JsonWriter &kvInt(const char *k, long long v) { return key(k).integer(v); }
  JsonWriter &kvBool(const char *k, bool v) { return key(k).boolean(v); }
  int depth() const { return depth_; }

 private:
  void sep();
  void raw(const char *s);
  Sink &s_;
  static constexpr int kMaxDepth = 16;
  bool first_[kMaxDepth] = {true};
  int depth_ = 0;
  bool afterKey_ = false;
};

// Escape a string for CSV (quotes if needed) into a sink.
void csvField(Sink &s, const char *v);

}  // namespace lfp8
