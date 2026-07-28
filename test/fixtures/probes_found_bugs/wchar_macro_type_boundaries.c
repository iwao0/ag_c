/*
 * Preserve the C11 wchar.h macro types, values, constant-expression use, and
 * compatibility with the corresponding stdint.h limit macros.
 */
#include <assert.h>
#include <wchar.h>
#include <stdint.h>
#include <wchar.h>

#if WCHAR_MIN != (-2147483647 - 1) || WCHAR_MAX != 2147483647
#error "unexpected target wchar_t limits"
#endif

_Static_assert(_Generic(WEOF, wint_t: 1, default: 0),
               "WEOF must have type wint_t");
_Static_assert(_Generic(WCHAR_MIN, int: 1, default: 0),
               "WCHAR_MIN target type");
_Static_assert(_Generic(WCHAR_MAX, int: 1, default: 0),
               "WCHAR_MAX target type");
_Static_assert(WEOF == (wint_t)-1, "WEOF target value");
_Static_assert(WCHAR_MIN < 0 && WCHAR_MAX > 0, "wchar_t signed range");

enum {
  wchar_zero_offset = WCHAR_MIN - WCHAR_MIN,
  wchar_full_range_is_wide = WCHAR_MAX > 65535,
};

static wchar_t wide_limits[] = {
    (wchar_t)WCHAR_MIN,
    (wchar_t)0,
    (wchar_t)WCHAR_MAX,
};
static wint_t wide_end = WEOF;

int main(void) {
  assert(wchar_zero_offset == 0);
  assert(wchar_full_range_is_wide == 1);
  assert(wide_limits[0] == (wchar_t)WCHAR_MIN);
  assert(wide_limits[1] == (wchar_t)0);
  assert(wide_limits[2] == (wchar_t)WCHAR_MAX);
  assert(wide_end == WEOF);
  assert(wide_end != (wint_t)L'\0');
  assert(wide_end != (wint_t)L'A');
  return 0;
}
