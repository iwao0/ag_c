/*
 * A byte pointer may be advanced to an aligned subobject, converted back to
 * the subobject type, and used as a compound-assignment lvalue.  The address
 * expression is evaluated once, the RHS uses the usual arithmetic
 * conversions, and the result is converted back to the lvalue type.
 */

#include <limits.h>

static int offset_calls;
static int left_calls;
static int right_calls;

static unsigned long long next_offset(void) {
  ++offset_calls;
  return 2ULL * (unsigned long long)sizeof(unsigned int);
}

static unsigned int next_left(void) {
  ++left_calls;
  return 5U;
}

static unsigned long long next_right(void) {
  ++right_calls;
  return 12ULL;
}

int main(void) {
  unsigned int words[4] = {10U, 100U, 200U, 300U};
  unsigned char *bytes = (unsigned char *)&words;
  unsigned long long wide_base = 1ULL << 33;
  unsigned long long offset =
      wide_base + (unsigned long long)sizeof(unsigned int) - wide_base;
  unsigned int a = 5U;
  unsigned long long b = 12ULL;
  unsigned int *second = (unsigned int *)(bytes + offset);
  unsigned int compound_result;

  _Static_assert(sizeof(unsigned int) == 4, "target unsigned int width");
  _Static_assert(UINT_MAX == 4294967295U, "target unsigned int range");

  if (second != &words[1]) return 1;
  compound_result = (*(unsigned int *)(bytes + offset) += a - b);
  if (compound_result != 93U || words[1] != 93U) return 2;
  if (words[0] != 10U || words[2] != 200U || words[3] != 300U) return 3;

  offset_calls = 0;
  left_calls = 0;
  right_calls = 0;
  compound_result =
      (*(unsigned int *)(bytes + next_offset()) +=
       next_left() - next_right());
  if (compound_result != 193U || words[2] != 193U) return 4;
  if (offset_calls != 1 || left_calls != 1 || right_calls != 1) return 5;
  if (words[0] != 10U || words[1] != 93U || words[3] != 300U) return 6;

  offset = 3ULL * (unsigned long long)sizeof(unsigned int);
  compound_result = (*(unsigned int *)(bytes + offset) *= 3ULL);
  if (compound_result != 900U || words[3] != 900U) return 7;

  offset = 0;
  compound_result = (*(unsigned int *)(bytes + offset) -= 17ULL);
  if (compound_result != UINT_MAX - 6U || words[0] != UINT_MAX - 6U)
    return 8;
  if (words[1] != 93U || words[2] != 193U || words[3] != 900U) return 9;
  return 0;
}
