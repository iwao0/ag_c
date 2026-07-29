/*
 * Integer and enum compound literals retain their exact scalar type while
 * following the same per-block-execution identity rule across recursion.
 */
#include <assert.h>

#define FRAME_COUNT 16

enum Toggle {
  TOGGLE_OFF,
  TOGGLE_ON
};

static _Bool *active_bool[FRAME_COUNT];
static signed char *active_signed_char[FRAME_COUNT];
static unsigned short *active_unsigned_short[FRAME_COUNT];
static long long *active_long_long[FRAME_COUNT];
static unsigned long long *active_unsigned_long_long[FRAME_COUNT];
static enum Toggle *active_toggle[FRAME_COUNT];
static int initializer_effects;

static _Bool next_bool(int round) {
  initializer_effects++;
  return round != 0;
}

static signed char next_signed_char(int depth, int round) {
  initializer_effects++;
  return (signed char)(depth * 2 + round + 1);
}

static unsigned short next_unsigned_short(int depth, int round) {
  initializer_effects++;
  return (unsigned short)(60000 + depth * 2 + round);
}

static long long next_long_long(int depth, int round) {
  initializer_effects++;
  return -5000000000LL - (long long)(depth * 16 + round);
}

static unsigned long long next_unsigned_long_long(
    int depth, int round) {
  initializer_effects++;
  return 0xf000000000000000ULL +
         (unsigned long long)(depth * 16 + round);
}

static enum Toggle next_toggle(int round) {
  initializer_effects++;
  return round == 0 ? TOGGLE_OFF : TOGGLE_ON;
}

static void check_values(
    const _Bool *bool_value,
    const signed char *signed_char_value,
    const unsigned short *unsigned_short_value,
    const long long *long_long_value,
    const unsigned long long *unsigned_long_long_value,
    const enum Toggle *toggle_value,
    int depth) {
  assert(bool_value != 0);
  assert(signed_char_value != 0);
  assert(unsigned_short_value != 0);
  assert(long_long_value != 0);
  assert(unsigned_long_long_value != 0);
  assert(toggle_value != 0);
  assert(*bool_value == 1);
  assert(*signed_char_value == (signed char)(depth * 2 + 2));
  assert(*unsigned_short_value ==
         (unsigned short)(60000 + depth * 2 + 1));
  assert(*long_long_value ==
         -5000000000LL - (long long)(depth * 16 + 1));
  assert(*unsigned_long_long_value ==
         0xf000000000000000ULL +
             (unsigned long long)(depth * 16 + 1));
  assert(*toggle_value == TOGGLE_ON);
}

static void visit_frame(int depth) {
  int round = 0;
  _Bool *first_bool = 0;
  signed char *first_signed_char = 0;
  unsigned short *first_unsigned_short = 0;
  long long *first_long_long = 0;
  unsigned long long *first_unsigned_long_long = 0;
  enum Toggle *first_toggle = 0;
  _Bool *bool_value = 0;
  signed char *signed_char_value = 0;
  unsigned short *unsigned_short_value = 0;
  long long *long_long_value = 0;
  unsigned long long *unsigned_long_long_value = 0;
  enum Toggle *toggle_value = 0;

repeat_literals:
  bool_value =
      &(_Bool){next_bool(round)};
  signed_char_value =
      &(signed char){next_signed_char(depth, round)};
  unsigned_short_value =
      &(unsigned short){next_unsigned_short(depth, round)};
  long_long_value =
      &(long long){next_long_long(depth, round)};
  unsigned_long_long_value =
      &(unsigned long long){
          next_unsigned_long_long(depth, round)};
  toggle_value =
      &(enum Toggle){next_toggle(round)};
  if (round == 0) {
    first_bool = bool_value;
    first_signed_char = signed_char_value;
    first_unsigned_short = unsigned_short_value;
    first_long_long = long_long_value;
    first_unsigned_long_long = unsigned_long_long_value;
    first_toggle = toggle_value;
    *bool_value = 1;
    *signed_char_value = -1;
    *unsigned_short_value = 0;
    *long_long_value = 0;
    *unsigned_long_long_value = 0;
    *toggle_value = TOGGLE_ON;
    round = 1;
    goto repeat_literals;
  }

  assert(bool_value == first_bool);
  assert(signed_char_value == first_signed_char);
  assert(unsigned_short_value == first_unsigned_short);
  assert(long_long_value == first_long_long);
  assert(unsigned_long_long_value == first_unsigned_long_long);
  assert(toggle_value == first_toggle);
  check_values(
      bool_value, signed_char_value, unsigned_short_value,
      long_long_value, unsigned_long_long_value, toggle_value,
      depth);

  for (int ancestor = 0; ancestor < depth; ancestor++) {
    assert(bool_value != active_bool[ancestor]);
    assert(signed_char_value != active_signed_char[ancestor]);
    assert(unsigned_short_value != active_unsigned_short[ancestor]);
    assert(long_long_value != active_long_long[ancestor]);
    assert(unsigned_long_long_value !=
           active_unsigned_long_long[ancestor]);
    assert(toggle_value != active_toggle[ancestor]);
    check_values(
        active_bool[ancestor],
        active_signed_char[ancestor],
        active_unsigned_short[ancestor],
        active_long_long[ancestor],
        active_unsigned_long_long[ancestor],
        active_toggle[ancestor],
        ancestor);
  }
  active_bool[depth] = bool_value;
  active_signed_char[depth] = signed_char_value;
  active_unsigned_short[depth] = unsigned_short_value;
  active_long_long[depth] = long_long_value;
  active_unsigned_long_long[depth] = unsigned_long_long_value;
  active_toggle[depth] = toggle_value;

  if (depth + 1 < FRAME_COUNT)
    visit_frame(depth + 1);

  check_values(
      bool_value, signed_char_value, unsigned_short_value,
      long_long_value, unsigned_long_long_value, toggle_value,
      depth);
  for (int ancestor = 0; ancestor < depth; ancestor++)
    check_values(
        active_bool[ancestor],
        active_signed_char[ancestor],
        active_unsigned_short[ancestor],
        active_long_long[ancestor],
        active_unsigned_long_long[ancestor],
        active_toggle[ancestor],
        ancestor);
  active_bool[depth] = 0;
  active_signed_char[depth] = 0;
  active_unsigned_short[depth] = 0;
  active_long_long[depth] = 0;
  active_unsigned_long_long[depth] = 0;
  active_toggle[depth] = 0;
}

int main(void) {
  visit_frame(0);
  assert(initializer_effects == FRAME_COUNT * 2 * 6);
  for (int depth = 0; depth < FRAME_COUNT; depth++) {
    assert(active_bool[depth] == 0);
    assert(active_signed_char[depth] == 0);
    assert(active_unsigned_short[depth] == 0);
    assert(active_long_long[depth] == 0);
    assert(active_unsigned_long_long[depth] == 0);
    assert(active_toggle[depth] == 0);
  }
  return 0;
}
