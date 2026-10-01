// Minimal self-contained unit-test framework (no external downloads).
#pragma once
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace tf {

typedef void (*TestFn)();
struct TestCase {
  const char *name;
  const char *file;
  TestFn fn;
};

void registerTest(const char *name, const char *file, TestFn fn);
extern int g_checks;
extern int g_failures;
// Directory where JSON test artefacts are written (for the python check).
extern const char *g_jsonOutDir;
void writeArtifact(const char *name, const char *content);

struct Registrar {
  Registrar(const char *name, const char *file, TestFn fn) { registerTest(name, file, fn); }
};

}  // namespace tf

#define TEST(name)                                                  \
  static void name();                                               \
  static tf::Registrar tf_reg_##name(#name, __FILE__, name);        \
  static void name()

#define CHECK(cond)                                                                 \
  do {                                                                              \
    tf::g_checks++;                                                                 \
    if (!(cond)) {                                                                  \
      tf::g_failures++;                                                             \
      printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                    \
    }                                                                               \
  } while (0)

#define CHECK_MSG(cond, ...)                                                        \
  do {                                                                              \
    tf::g_checks++;                                                                 \
    if (!(cond)) {                                                                  \
      tf::g_failures++;                                                             \
      printf("    FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);                  \
      printf(__VA_ARGS__);                                                          \
      printf("\n");                                                                 \
    }                                                                               \
  } while (0)

#define CHECK_NEAR(a, b, tol)                                                       \
  do {                                                                              \
    tf::g_checks++;                                                                 \
    double tf_a = (double)(a), tf_b = (double)(b), tf_t = (double)(tol);            \
    if (!(fabs(tf_a - tf_b) <= tf_t)) {                                             \
      tf::g_failures++;                                                             \
      printf("    FAIL %s:%d: %s = %.6g, expected %s = %.6g +- %.3g\n", __FILE__,     \
             __LINE__, #a, tf_a, #b, tf_b, tf_t);                                   \
    }                                                                               \
  } while (0)

#define CHECK_EQ(a, b)                                                              \
  do {                                                                              \
    tf::g_checks++;                                                                 \
    long long tf_a = (long long)(a), tf_b = (long long)(b);                         \
    if (tf_a != tf_b) {                                                             \
      tf::g_failures++;                                                             \
      printf("    FAIL %s:%d: %s = %lld, expected %lld\n", __FILE__, __LINE__, #a,  \
             tf_a, tf_b);                                                           \
    }                                                                               \
  } while (0)

#define CHECK_STREQ(a, b)                                                           \
  do {                                                                              \
    tf::g_checks++;                                                                 \
    const char *tf_a = (a), *tf_b = (b);                                            \
    if (!tf_a || !tf_b || strcmp(tf_a, tf_b) != 0) {                                \
      tf::g_failures++;                                                             \
      printf("    FAIL %s:%d: %s = \"%s\", expected \"%s\"\n", __FILE__, __LINE__,  \
             #a, tf_a ? tf_a : "(null)", tf_b ? tf_b : "(null)");                   \
    }                                                                               \
  } while (0)
