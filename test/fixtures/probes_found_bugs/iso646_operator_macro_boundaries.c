/*
 * Preserve every C11 <iso646.h> alternative-token macro, including use in
 * preprocessing constant expressions, compound assignment, precedence, and
 * short-circuit evaluation.
 */
#include <assert.h>
#include <iso646.h>
#include <iso646.h>

#if (6 bitand 3) != 2
#error "bitand must expand in preprocessing expressions"
#endif
#if (4 bitor 1) != 5
#error "bitor must expand in preprocessing expressions"
#endif
#if (7 xor 3) != 4
#error "xor must expand in preprocessing expressions"
#endif
#if (compl 0) != -1
#error "compl must expand in preprocessing expressions"
#endif

#define BOTH(left, right) ((left) and (right))
#define EITHER(left, right) ((left) or (right))

static int effects;

static int next_true(void) {
  effects += 1;
  return 1;
}

static int next_false(void) {
  effects += 1;
  return 0;
}

int main(void) {
  int value = 0xF0;

  value and_eq 0xCC;
  assert(value == 0xC0);
  value or_eq 0x03;
  assert(value == 0xC3);
  value xor_eq 0x0F;
  assert(value == 0xCC);

  assert((6 bitand 3) == 2);
  assert((4 bitor 1) == 5);
  assert((7 xor 3) == 4);
  assert((compl 0) == -1);
  assert(not 0);
  assert(1 not_eq 0);

  assert((1 or (0 and 2)) == 1);
  assert((3 bitand (1 not_eq 0)) == 1);
  assert(BOTH(1, 2) == 1);
  assert(EITHER(0, 2) == 1);

  effects = 0;
  assert((next_false() and next_true()) == 0);
  assert(effects == 1);
  effects = 0;
  assert((next_true() or next_false()) == 1);
  assert(effects == 1);
  return 0;
}
