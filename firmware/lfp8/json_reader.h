// Minimal JSON reader for flat request bodies: looks up keys of the
// top-level object. Nested values are skipped correctly (strings with
// escapes, nested objects/arrays). No heap.
#pragma once
#include <stddef.h>

namespace lfp8 {
namespace jr {

// True if the text is a syntactically valid JSON object.
bool validObject(const char *json);
bool has(const char *json, const char *key);
bool getNum(const char *json, const char *key, double &out);
bool getInt(const char *json, const char *key, long &out);
bool getBool(const char *json, const char *key, bool &out);
// Copies a string value (unescaped; \uXXXX outside ASCII becomes '?').
bool getStr(const char *json, const char *key, char *out, size_t outLen);
// Array of numbers; returns the number of elements (<= maxN), -1 if absent or
// malformed, -2 if it has more than maxN elements.
int getNumArray(const char *json, const char *key, double *out, int maxN);

}  // namespace jr
}  // namespace lfp8
