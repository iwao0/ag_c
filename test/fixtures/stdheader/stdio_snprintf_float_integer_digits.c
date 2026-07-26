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
  check("%#.0f", 12.0, "12.");
  check("%.0f", 123456.0, "123456");
  check("%.2f", 123456.25, "123456.25");
  check("%.0f", 99.5, "100");
  check("%#.0f", -12.0, "-12.");
  return 0;
}
