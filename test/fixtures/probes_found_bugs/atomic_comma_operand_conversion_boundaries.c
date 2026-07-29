/*
 * A comma expression evaluates its left operand once, then applies the
 * lvalue conversion to an atomic right operand.  The resulting value has the
 * corresponding non-atomic type, and selecting the atomic object is evaluated
 * exactly once.
 */
#include <assert.h>

#define TYPE_IS(expression, type) \
  _Generic((expression), type: 1, default: 0)

struct Pair {
  int first;
  long second;
};

static int values[3] = {3, 5, 7};
static _Atomic int atomic_integer = 11;
static _Atomic(unsigned long long) atomic_wide = 0xfedcba9876543210ULL;
static _Atomic(int *) atomic_pointer = values + 1;
static _Atomic(struct Pair) atomic_pair = (struct Pair){13, 17};

static int left_effects;
static int selections;

static void note(void) {
  left_effects++;
}

static _Atomic int *select_integer(void) {
  selections++;
  return &atomic_integer;
}

static _Atomic(unsigned long long) *select_wide(void) {
  selections++;
  return &atomic_wide;
}

static _Atomic(int *) *select_pointer(void) {
  selections++;
  return &atomic_pointer;
}

static _Atomic(struct Pair) *select_pair(void) {
  selections++;
  return &atomic_pair;
}

_Static_assert(TYPE_IS(((void)0, atomic_integer), int),
               "comma converts an atomic integer lvalue");
_Static_assert(TYPE_IS(((void)0, atomic_wide), unsigned long long),
               "comma converts a wide atomic integer lvalue");
_Static_assert(TYPE_IS(((void)0, atomic_pointer), int *),
               "comma converts an atomic pointer lvalue");
_Static_assert(TYPE_IS(((void)0, atomic_pair), struct Pair),
               "comma converts an atomic aggregate lvalue");

int main(void) {
  assert(left_effects == 0);
  assert(selections == 0);

  int integer = (note(), *select_integer());
  unsigned long long wide = (note(), *select_wide());
  int *pointer = (note(), *select_pointer());
  struct Pair pair = (note(), *select_pair());

  assert(integer == 11);
  assert(wide == 0xfedcba9876543210ULL);
  assert(pointer == values + 1);
  assert(*pointer == 5);
  assert(pair.first == 13);
  assert(pair.second == 17);
  assert(left_effects == 4);
  assert(selections == 4);

  assert(TYPE_IS((note(), atomic_integer), int));
  assert(TYPE_IS((note(), atomic_wide), unsigned long long));
  assert(TYPE_IS((note(), atomic_pointer), int *));
  assert(TYPE_IS((note(), atomic_pair), struct Pair));
  assert(left_effects == 4);
  assert(selections == 4);
  return 0;
}
