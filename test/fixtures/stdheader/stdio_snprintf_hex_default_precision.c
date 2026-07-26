// An omitted %a precision must retain every significant hexadecimal digit
// without padding an exact binary64 value to 13 fractional digits.
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void check(const char *format, double value, const char *expected) {
  char buf[64];
  int length = snprintf(buf, sizeof(buf), format, value);
  assert(length == (int)strlen(expected));
  assert(strcmp(buf, expected) == 0);
}

int main(void) {
  check("%a", 1.0, "0x1p+0");
  check("%a", 1.5, "0x1.8p+0");
  check("%a", 3.25, "0x1.ap+1");
  check("%a", 0.5, "0x1p-1");
  check("%a", -1.5, "-0x1.8p+0");
  check("%a", 0.0, "0x0p+0");
  check("%a", -0.0, "-0x0p+0");
  check("%#a", 1.0, "0x1.p+0");
  check("%A", 3.25, "0X1.AP+1");
  check("%a", 0x1.0000000000001p+0, "0x1.0000000000001p+0");
  check("%a", 0x1p-1022, "0x1p-1022");

  check("%.14a", 1.5, "0x1.80000000000000p+0");
  check("%.16a", 0x1.0000000000001p+0,
        "0x1.0000000000001000p+0");
  check("%.18A", 3.25, "0X1.A00000000000000000P+1");
  check("%#.14a", 0.0, "0x0.00000000000000p+0");
  return 0;
}
