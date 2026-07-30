// Aggregate values containing boolean, narrow integer, and enum members must
// preserve normalization, signedness, and every subobject across ABI paths.
// Expected: exit=0
#include <assert.h>
#include <stdarg.h>

enum unsigned_state {
  UNSIGNED_STATE_IDLE = 0,
  UNSIGNED_STATE_BUSY = 60000
};

enum signed_direction {
  SIGNED_DIRECTION_BACKWARD = -300,
  SIGNED_DIRECTION_STILL = 0,
  SIGNED_DIRECTION_FORWARD = 300
};

struct compact_integer_members {
  _Bool flags[2];
  signed char delta;
  unsigned char level;
  short offset;
  unsigned short span;
  enum unsigned_state state;
  enum signed_direction direction;
};

struct wide_integer_members {
  _Bool flags[3];
  signed char deltas[3];
  unsigned char levels[3];
  short offsets[3];
  unsigned short spans[3];
  enum unsigned_state states[2];
  enum signed_direction directions[2];
  long long sentinel;
};

struct integer_choice_fields {
  enum signed_direction direction;
  unsigned short span;
  _Bool flags[2];
  signed char delta;
  unsigned char level;
};

union integer_choice {
  struct integer_choice_fields fields;
  unsigned long long words[2];
};

typedef struct compact_integer_members compact_callback_t(
    struct compact_integer_members, int);
typedef struct wide_integer_members wide_callback_t(
    struct wide_integer_members, int);
typedef union integer_choice choice_callback_t(
    union integer_choice, int);

static struct compact_integer_members make_compact(int seed) {
  struct compact_integer_members value;
  value.flags[0] = seed;
  value.flags[1] = 0;
  value.delta = (signed char)(-20 - seed);
  value.level = (unsigned char)(220 + seed);
  value.offset = (short)(-2000 - seed);
  value.span = (unsigned short)(50000 + seed);
  value.state = UNSIGNED_STATE_BUSY;
  value.direction = SIGNED_DIRECTION_BACKWARD;
  return value;
}

static int compact_is(struct compact_integer_members value,
                      int seed) {
  return value.flags[0] == 1 &&
         value.flags[1] == 0 &&
         value.delta == (signed char)(-20 - seed) &&
         value.level == (unsigned char)(220 + seed) &&
         value.offset == (short)(-2000 - seed) &&
         value.span == (unsigned short)(50000 + seed) &&
         value.state == UNSIGNED_STATE_BUSY &&
         value.direction == SIGNED_DIRECTION_BACKWARD;
}

static struct wide_integer_members make_wide(int seed) {
  struct wide_integer_members value;
  int index;
  value.flags[0] = 0;
  value.flags[1] = seed;
  value.flags[2] = -seed;
  for (index = 0; index < 3; index++) {
    value.deltas[index] =
        (signed char)(-30 - seed - index);
    value.levels[index] =
        (unsigned char)(230 + seed + index);
    value.offsets[index] =
        (short)(-30000 + seed + index);
    value.spans[index] =
        (unsigned short)(60000 - seed - index);
  }
  value.states[0] = UNSIGNED_STATE_IDLE;
  value.states[1] = UNSIGNED_STATE_BUSY;
  value.directions[0] = SIGNED_DIRECTION_BACKWARD;
  value.directions[1] = SIGNED_DIRECTION_FORWARD;
  value.sentinel = 0x1122334455667700LL + seed;
  return value;
}

static int wide_is(struct wide_integer_members value,
                   int seed) {
  int index;
  if (value.flags[0] != 0 ||
      value.flags[1] != 1 ||
      value.flags[2] != 1) {
    return 0;
  }
  for (index = 0; index < 3; index++) {
    if (value.deltas[index] !=
            (signed char)(-30 - seed - index) ||
        value.levels[index] !=
            (unsigned char)(230 + seed + index) ||
        value.offsets[index] !=
            (short)(-30000 + seed + index) ||
        value.spans[index] !=
            (unsigned short)(60000 - seed - index)) {
      return 0;
    }
  }
  return value.states[0] == UNSIGNED_STATE_IDLE &&
         value.states[1] == UNSIGNED_STATE_BUSY &&
         value.directions[0] == SIGNED_DIRECTION_BACKWARD &&
         value.directions[1] == SIGNED_DIRECTION_FORWARD &&
         value.sentinel == 0x1122334455667700LL + seed;
}

static union integer_choice make_choice(int seed) {
  union integer_choice value;
  value.fields.direction = SIGNED_DIRECTION_FORWARD;
  value.fields.span = (unsigned short)(55000 + seed);
  value.fields.flags[0] = 0;
  value.fields.flags[1] = seed;
  value.fields.delta = (signed char)(-60 - seed);
  value.fields.level = (unsigned char)(190 + seed);
  return value;
}

static int choice_is(union integer_choice value,
                     int seed) {
  return value.fields.direction == SIGNED_DIRECTION_FORWARD &&
         value.fields.span == (unsigned short)(55000 + seed) &&
         value.fields.flags[0] == 0 &&
         value.fields.flags[1] == 1 &&
         value.fields.delta == (signed char)(-60 - seed) &&
         value.fields.level == (unsigned char)(190 + seed);
}

static struct compact_integer_members roundtrip_compact(
    struct compact_integer_members value, int marker) {
  assert(marker == 101);
  return value;
}

static struct wide_integer_members roundtrip_wide(
    struct wide_integer_members value, int marker) {
  assert(marker == 102);
  return value;
}

static union integer_choice roundtrip_choice(
    union integer_choice value, int marker) {
  assert(marker == 103);
  return value;
}

static int check_variadic_integer_members(int marker, ...) {
  va_list arguments;
  struct compact_integer_members compact;
  struct wide_integer_members wide;
  union integer_choice choice;
  va_start(arguments, marker);
  compact = va_arg(arguments, struct compact_integer_members);
  wide = va_arg(arguments, struct wide_integer_members);
  choice = va_arg(arguments, union integer_choice);
  va_end(arguments);
  return marker == 104 &&
         compact_is(compact, 4) &&
         wide_is(wide, 5) &&
         choice_is(choice, 6);
}

static struct compact_integer_members compact_sources[2];
static struct wide_integer_members wide_sources[2];
static union integer_choice choice_sources[2];
static int compact_selections;
static int wide_selections;
static int choice_selections;

static struct compact_integer_members *select_compact(int index) {
  compact_selections++;
  return &compact_sources[index];
}

static struct wide_integer_members *select_wide(int index) {
  wide_selections++;
  return &wide_sources[index];
}

static union integer_choice *select_choice(int index) {
  choice_selections++;
  return &choice_sources[index];
}

int main(void) {
  compact_callback_t *compact_callback = roundtrip_compact;
  wide_callback_t *wide_callback = roundtrip_wide;
  choice_callback_t *choice_callback = roundtrip_choice;
  struct compact_integer_members compact =
      compact_callback(make_compact(1), 101);
  struct {
    unsigned long before;
    struct wide_integer_members value;
    unsigned long after;
  } guarded_wide = {
      .before = 0x11223344UL,
      .value = {0},
      .after = 0x55667788UL,
  };
  struct {
    unsigned long before;
    union integer_choice value;
    unsigned long after;
  } guarded_choice = {
      .before = 0x99aabbccUL,
      .value = {0},
      .after = 0xddeeff00UL,
  };

  assert(compact_is(compact, 1));
  guarded_wide.value =
      wide_callback(make_wide(2), 102);
  assert(guarded_wide.before == 0x11223344UL);
  assert(guarded_wide.after == 0x55667788UL);
  assert(wide_is(guarded_wide.value, 2));
  guarded_choice.value =
      choice_callback(make_choice(3), 103);
  assert(guarded_choice.before == 0x99aabbccUL);
  assert(guarded_choice.after == 0xddeeff00UL);
  assert(choice_is(guarded_choice.value, 3));

  assert(check_variadic_integer_members(
      104, make_compact(4), make_wide(5), make_choice(6)));

  compact_sources[0] = make_compact(7);
  compact_sources[1] = make_compact(8);
  wide_sources[0] = make_wide(9);
  wide_sources[1] = make_wide(10);
  choice_sources[0] = make_choice(11);
  choice_sources[1] = make_choice(12);

  assert(compact_is(
      1 ? *select_compact(0) : *select_compact(1), 7));
  assert(compact_selections == 1);
  assert(wide_is(
      0 ? *select_wide(0) : *select_wide(1), 10));
  assert(wide_selections == 1);
  assert(choice_is(
      1 ? *select_choice(0) : *select_choice(1), 11));
  assert(choice_selections == 1);

  assert(compact_is(
      (compact_selections++, compact_sources[1]), 8));
  assert(compact_selections == 2);
  assert(wide_is(
      (wide_selections++, wide_sources[0]), 9));
  assert(wide_selections == 2);
  assert(choice_is(
      (choice_selections++, choice_sources[1]), 12));
  assert(choice_selections == 2);

  compact = make_compact(13);
  assert(compact_is((compact = make_compact(14)), 14));
  guarded_wide.value = make_wide(15);
  assert(wide_is(
      (guarded_wide.value = make_wide(16)), 16));
  guarded_choice.value = make_choice(17);
  assert(choice_is(
      (guarded_choice.value = make_choice(18)), 18));
  assert(guarded_wide.before == 0x11223344UL);
  assert(guarded_wide.after == 0x55667788UL);
  assert(guarded_choice.before == 0x99aabbccUL);
  assert(guarded_choice.after == 0xddeeff00UL);
  return 0;
}
