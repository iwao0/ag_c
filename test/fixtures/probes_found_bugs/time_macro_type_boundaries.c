/*
 * Preserve the C11 time.h macro types and constant-expression behavior.
 * CLOCKS_PER_SEC has type clock_t even when clock_t differs by target.
 */
#include <assert.h>
#include <time.h>
#include <time.h>

_Static_assert(
    _Generic(CLOCKS_PER_SEC, clock_t: 1, default: 0),
    "CLOCKS_PER_SEC must have type clock_t");
_Static_assert(CLOCKS_PER_SEC == (clock_t)1000000,
               "CLOCKS_PER_SEC target value");
_Static_assert(_Generic(TIME_UTC, int: 1, default: 0),
               "TIME_UTC must be an int constant");
_Static_assert(TIME_UTC > 0, "TIME_UTC must be positive");

enum {
  utc_base_constant = TIME_UTC,
};

static clock_t clock_constants[] = {
    (clock_t)0,
    CLOCKS_PER_SEC,
    (clock_t)(2 * CLOCKS_PER_SEC),
};

int main(void) {
  clock_t elapsed = clock_constants[2] - clock_constants[1];

  assert(utc_base_constant == TIME_UTC);
  assert(clock_constants[0] == (clock_t)0);
  assert(elapsed == CLOCKS_PER_SEC);
  assert(elapsed / CLOCKS_PER_SEC == (clock_t)1);
  return 0;
}
