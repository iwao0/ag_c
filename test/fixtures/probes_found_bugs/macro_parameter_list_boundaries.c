/*
 * C11 6.10.3 function-like macro parameter-list boundaries.
 * Valid empty, named, comment-separated, large, and variadic parameter lists
 * must remain accepted while malformed lists are rejected by compile-fail
 * fixtures.
 */
#include <assert.h>

#define ZERO() 7
#define ID(value) (value)
#define ADD(left, right) ((left) + (right))
#define COMMENTED(left /* separator */, right) ((left) + (right))
#define NINE(a, b, c, d, e, f, g, h, i) \
  ((a) + (b) + (c) + (d) + (e) + (f) + (g) + (h) + (i))
#define NAMED_VARIADIC(base, ...) ((base) + NINE(__VA_ARGS__))
#define ONLY_VARIADIC(...) NINE(__VA_ARGS__)
#define EMPTY_VARIADIC(...) 42
#define OBJECT_PAREN (40 + 2)

int main(void) {
  assert(ZERO() == 7);
  assert(ID(42) == 42);
  assert(ADD(20, 22) == 42);
  assert(COMMENTED(19, 23) == 42);
  assert(NINE(1, 2, 3, 4, 5, 6, 7, 8, 6) == 42);
  assert(NAMED_VARIADIC(0, 1, 2, 3, 4, 5, 6, 7, 8, 6) == 42);
  assert(ONLY_VARIADIC(1, 2, 3, 4, 5, 6, 7, 8, 6) == 42);
  assert(EMPTY_VARIADIC() == 42);
  assert(OBJECT_PAREN == 42);
  return 0;
}
