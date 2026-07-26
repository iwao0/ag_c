// snprintf must report the untruncated length while respecting every buffer
// boundary, including the standard size-zero and null-buffer query forms.
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

static int measured(const char *format, ...) {
  va_list args;
  va_start(args, format);
  int length = vsnprintf(NULL, 0, format, args);
  va_end(args);
  return length;
}

int main(void) {
  char untouched = 'Q';
  if (snprintf(&untouched, 0, "%d:%s", 123, "abc") != 7) return 1;
  if (untouched != 'Q') return 2;
  if (snprintf(NULL, 0, "%#x/%.*s", 0x2a, 3, "abcdef") != 8) return 3;
  if (measured("%+06d/%s", 42, "xy") != 9) return 4;

  char one[3] = {'A', 'B', 'C'};
  if (snprintf(one, 1, "xyz") != 3) return 5;
  if (one[0] != '\0' || one[1] != 'B' || one[2] != 'C') return 6;

  char embedded[6] = {'?', '?', '?', '?', '?', '?'};
  if (snprintf(embedded, sizeof(embedded), "A%cB", '\0') != 3) return 7;
  if (embedded[0] != 'A' || embedded[1] != '\0' ||
      embedded[2] != 'B' || embedded[3] != '\0' ||
      embedded[4] != '?' || embedded[5] != '?') return 8;

  char truncated[4] = {'?', '?', '?', '?'};
  int count = -1;
  if (snprintf(truncated, sizeof(truncated), "abcd%nEF", &count) != 6) return 9;
  if (count != 4) return 10;
  if (truncated[0] != 'a' || truncated[1] != 'b' ||
      truncated[2] != 'c' || truncated[3] != '\0') return 11;
  return 0;
}
