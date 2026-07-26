#include <assert.h>
#include <stdio.h>
#include <string.h>

static void check(const char *format, double value, const char *expected) {
  char buf[32];
  int length = snprintf(buf, sizeof(buf), format, value);
  assert(length == (int)strlen(expected));
  assert(strcmp(buf, expected) == 0);
}

int main(void) {
  check("%.1e", 9.95, "9.9e+00");
  check("%.1e", 99.5, "1.0e+02");
  check("%.1e", 985.0, "9.8e+02");
  check("%.1e", 995.0, "1.0e+03");

  check("%.2g", 9.95, "9.9");
  check("%.2g", 99.5, "1e+02");
  check("%.2g", -99.5, "-1e+02");
  check("%.2g", 985.0, "9.8e+02");
  check("%.2g", 995.0, "1e+03");
  check("%.4g", 9999.5, "1e+04");
  return 0;
}
