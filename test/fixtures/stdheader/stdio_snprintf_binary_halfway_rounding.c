// Decimal formatting must round the original binary64 value, without first
// rounding value * 10^precision to another binary64 halfway value.
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void check(const char *format, double value, const char *expected) {
  char buf[16];
  int length = snprintf(buf, sizeof(buf), format, value);
  assert(length == (int)strlen(expected));
  assert(strcmp(buf, expected) == 0);
}

int main(void) {
  check("%.1f", 0.05, "0.1");
  check("%.1f", 0.15, "0.1");
  check("%.1f", -0.05, "-0.1");
  check("%.1f", -0.15, "-0.1");

  check("%.1f", 0.25, "0.2");
  check("%.1f", 0.75, "0.8");
  check("%.1f", 1.25, "1.2");
  check("%.1f", 1.75, "1.8");
  return 0;
}
