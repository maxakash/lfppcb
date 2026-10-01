// Test runner: ./unit_tests [--json-out DIR] [filter]
#include <stdlib.h>

#include <string>
#include <vector>

#include "test_framework.h"

namespace tf {
int g_checks = 0;
int g_failures = 0;
const char *g_jsonOutDir = nullptr;

static std::vector<TestCase> &registry() {
  static std::vector<TestCase> r;
  return r;
}

void registerTest(const char *name, const char *file, TestFn fn) { registry().push_back({name, file, fn}); }

void writeArtifact(const char *name, const char *content) {
  if (!g_jsonOutDir) return;
  std::string path = std::string(g_jsonOutDir) + "/" + name;
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) {
    printf("    cannot write %s\n", path.c_str());
    g_failures++;
    return;
  }
  fputs(content, f);
  fclose(f);
}
}  // namespace tf

int main(int argc, char **argv) {
  const char *filter = nullptr;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--json-out") && i + 1 < argc) tf::g_jsonOutDir = argv[++i];
    else filter = argv[i];
  }
  int passed = 0, failed = 0;
  for (const tf::TestCase &t : tf::registry()) {
    if (filter && !strstr(t.name, filter)) continue;
    int before = tf::g_failures;
    printf("[ RUN  ] %s\n", t.name);
    fflush(stdout);
    t.fn();
    if (tf::g_failures == before) {
      passed++;
      printf("[  OK  ] %s\n", t.name);
    } else {
      failed++;
      printf("[ FAIL ] %s (%s)\n", t.name, t.file);
    }
  }
  printf("\n%d tests passed, %d failed (%d checks, %d failed checks)\n", passed, failed, tf::g_checks,
         tf::g_failures);
  return failed ? 1 : 0;
}
