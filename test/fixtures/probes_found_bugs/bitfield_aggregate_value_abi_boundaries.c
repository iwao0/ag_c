// Bit-field allocation units and signed extraction remain intact when
// aggregates cross direct, indirect, return, and variadic ABI boundaries.
// Expected: exit=0
#include <assert.h>
#include <stdarg.h>

struct small_bits {
  unsigned int low : 3;
  signed int delta : 6;
  unsigned int high : 9;
  signed int edge : 5;
};

struct crossing_bits {
  unsigned char prefix[3];
  unsigned int head : 5;
  unsigned int middle : 6;
  unsigned int wide : 26;
  unsigned char tail;
};

struct large_bits {
  long before;
  signed int left : 7;
  unsigned int middle : 19;
  unsigned int : 0;
  signed int right : 9;
  unsigned int flag : 1;
  long after;
};

struct choice_fields {
  signed int negative : 8;
  unsigned int positive : 10;
};

union small_choice {
  struct choice_fields fields;
  unsigned int raw;
};

union large_choice {
  struct large_bits fields;
  unsigned char bytes[sizeof(struct large_bits)];
};

static const struct small_bits global_small = {
    .low = 5, .delta = -17, .high = 257, .edge = -7};
static const struct crossing_bits global_crossing = {
    .prefix = {11, 22, 33},
    .head = 17,
    .middle = 45,
    .wide = 0x1234567,
    .tail = 44};
static const struct large_bits global_large = {
    .before = 101,
    .left = -31,
    .middle = 0x45678,
    .right = -27,
    .flag = 1,
    .after = 103};
static const union small_choice global_small_choice = {
    .fields = {.negative = -51, .positive = 777}};
static const union large_choice global_large_choice = {
    .fields = {
        .before = 107,
        .left = -19,
        .middle = 0x12345,
        .right = -43,
        .flag = 0,
        .after = 109}};

static int check_small(
    struct small_bits value,
    int low, int delta, int high, int edge) {
  return value.low == low && value.delta == delta &&
         value.high == high && value.edge == edge;
}

static int check_crossing(
    struct crossing_bits value,
    unsigned char first, unsigned char second,
    unsigned char third, int head,
    int middle, int wide,
    unsigned char tail) {
  return value.prefix[0] == first &&
         value.prefix[1] == second &&
         value.prefix[2] == third &&
         value.head == head &&
         value.middle == middle &&
         value.wide == wide &&
         value.tail == tail;
}

static int check_large(
    struct large_bits value,
    long before, int left, int middle,
    int right, int flag, long after) {
  return value.before == before &&
         value.left == left &&
         value.middle == middle &&
         value.right == right &&
         value.flag == flag &&
         value.after == after;
}

static int check_small_choice(
    union small_choice value,
    int negative, int positive) {
  return value.fields.negative == negative &&
         value.fields.positive == positive;
}

static int check_large_choice(
    union large_choice value,
    long before, int left, int middle,
    int right, int flag, long after) {
  return check_large(
      value.fields, before, left, middle,
      right, flag, after);
}

static struct small_bits rotate_small(
    struct small_bits value) {
  unsigned int low = value.low;
  int delta = value.delta;
  value.low = (unsigned int)(value.edge + 8);
  value.delta = -delta;
  value.edge = (int)low - 8;
  return value;
}

static struct crossing_bits copy_crossing(
    struct crossing_bits value) {
  return value;
}

static struct large_bits rotate_large(
    struct large_bits value) {
  long before = value.before;
  int left = value.left;
  value.before = value.after;
  value.left = value.right;
  value.right = left;
  value.after = before;
  return value;
}

static union small_choice copy_small_choice(
    union small_choice value) {
  return value;
}

static union large_choice rotate_large_choice(
    union large_choice value) {
  value.fields = rotate_large(value.fields);
  return value;
}

typedef struct small_bits small_transform_t(
    struct small_bits);
typedef struct crossing_bits crossing_transform_t(
    struct crossing_bits);
typedef struct large_bits large_transform_t(
    struct large_bits);
typedef union small_choice small_choice_transform_t(
    union small_choice);
typedef union large_choice large_choice_transform_t(
    union large_choice);

static int check_variadic_bitfields(int marker, ...) {
  va_list arguments;
  va_start(arguments, marker);
  struct small_bits small =
      va_arg(arguments, struct small_bits);
  struct crossing_bits crossing =
      va_arg(arguments, struct crossing_bits);
  struct large_bits large =
      va_arg(arguments, struct large_bits);
  union small_choice small_choice =
      va_arg(arguments, union small_choice);
  union large_choice large_choice =
      va_arg(arguments, union large_choice);
  va_end(arguments);

  return marker == 211 &&
         check_small(small, 5, -17, 257, -7) &&
         check_crossing(
             crossing, 11, 22, 33, 17, 45,
             0x1234567, 44) &&
         check_large(
             large, 101, -31, 0x45678, -27, 1, 103) &&
         check_small_choice(small_choice, -51, 777) &&
         check_large_choice(
             large_choice,
             107, -19, 0x12345, -43, 0, 109);
}

static void verify_direct_and_indirect_calls(void) {
  assert(check_small(global_small, 5, -17, 257, -7));
  assert(check_crossing(
      global_crossing, 11, 22, 33, 17, 45,
      0x1234567, 44));
  assert(check_large(
      global_large, 101, -31, 0x45678, -27, 1, 103));
  assert(check_small_choice(global_small_choice, -51, 777));
  assert(check_large_choice(
      global_large_choice,
      107, -19, 0x12345, -43, 0, 109));

  small_transform_t *small_transform = rotate_small;
  crossing_transform_t *crossing_transform = copy_crossing;
  large_transform_t *large_transform = rotate_large;
  small_choice_transform_t *small_choice_transform =
      copy_small_choice;
  large_choice_transform_t *large_choice_transform =
      rotate_large_choice;

  struct small_bits small_result =
      small_transform(global_small);
  struct crossing_bits crossing_result =
      crossing_transform(global_crossing);
  struct large_bits large_result =
      large_transform(global_large);
  union small_choice small_choice_result =
      small_choice_transform(global_small_choice);
  union large_choice large_choice_result =
      large_choice_transform(global_large_choice);

  assert(check_small(small_result, 1, 17, 257, -3));
  assert(check_crossing(
      crossing_result, 11, 22, 33, 17, 45,
      0x1234567, 44));
  assert(check_large(
      large_result, 103, -27, 0x45678, -31, 1, 101));
  assert(check_small_choice(
      small_choice_result, -51, 777));
  assert(check_large_choice(
      large_choice_result,
      109, -43, 0x12345, -19, 0, 107));

  assert(check_variadic_bitfields(
      211, global_small, global_crossing, global_large,
      global_small_choice, global_large_choice));
}

static void verify_assignment_conditionals_and_canaries(void) {
  struct small_bits small_right = {
      .low = 2, .delta = 13, .high = 301, .edge = -11};
  struct crossing_bits crossing_right = {
      .prefix = {55, 66, 77},
      .head = 19,
      .middle = 47,
      .wide = 0x2abcdef,
      .tail = 88};
  struct large_bits large_right = {
      .before = 113,
      .left = -23,
      .middle = 0x23456,
      .right = -47,
      .flag = 1,
      .after = 127};
  union small_choice small_choice_right = {
      .fields = {.negative = -63, .positive = 511}};
  union large_choice large_choice_right = {
      .fields = {
          .before = 131,
          .left = -29,
          .middle = 0x34567,
          .right = -53,
          .flag = 1,
          .after = 137}};
  int choose_left = 0;
  int comma_evaluations = 0;

  struct small_bits selected_small =
      choose_left ? global_small : small_right;
  struct crossing_bits selected_crossing =
      choose_left ? global_crossing : crossing_right;
  struct large_bits selected_large =
      choose_left ? global_large : large_right;
  union small_choice selected_small_choice =
      choose_left ? global_small_choice : small_choice_right;
  union large_choice selected_large_choice =
      choose_left ? global_large_choice : large_choice_right;
  assert(check_small(selected_small, 2, 13, 301, -11));
  assert(check_crossing(
      selected_crossing, 55, 66, 77, 19, 47,
      0x2abcdef, 88));
  assert(check_large(
      selected_large,
      113, -23, 0x23456, -47, 1, 127));
  assert(check_small_choice(
      selected_small_choice, -63, 511));
  assert(check_large_choice(
      selected_large_choice,
      131, -29, 0x34567, -53, 1, 137));

  struct {
    unsigned char before;
    struct large_bits value;
    unsigned char after;
  } large_box = {.before = 0x5a, .after = 0xa5};
  struct {
    unsigned char before;
    union large_choice value;
    unsigned char after;
  } choice_box = {.before = 0x3c, .after = 0xc3};

  struct large_bits large_assignment =
      (large_box.value = rotate_large(global_large));
  union large_choice choice_assignment =
      (choice_box.value =
           rotate_large_choice(global_large_choice));
  assert(large_box.before == 0x5a);
  assert(large_box.after == 0xa5);
  assert(choice_box.before == 0x3c);
  assert(choice_box.after == 0xc3);
  assert(check_large(
      large_assignment, 103, -27, 0x45678, -31, 1, 101));
  assert(check_large_choice(
      choice_assignment,
      109, -43, 0x12345, -19, 0, 107));

  struct small_bits comma_small =
      (comma_evaluations++, global_small);
  struct crossing_bits comma_crossing =
      (comma_evaluations++, crossing_right);
  struct large_bits comma_large =
      (comma_evaluations++, global_large);
  union small_choice comma_small_choice =
      (comma_evaluations++, small_choice_right);
  union large_choice comma_large_choice =
      (comma_evaluations++, global_large_choice);
  assert(check_small(comma_small, 5, -17, 257, -7));
  assert(check_crossing(
      comma_crossing, 55, 66, 77, 19, 47,
      0x2abcdef, 88));
  assert(check_large(
      comma_large, 101, -31, 0x45678, -27, 1, 103));
  assert(check_small_choice(
      comma_small_choice, -63, 511));
  assert(check_large_choice(
      comma_large_choice,
      107, -19, 0x12345, -43, 0, 109));
  assert(comma_evaluations == 5);
}

int main(void) {
  verify_direct_and_indirect_calls();
  verify_assignment_conditionals_and_canaries();
  return 0;
}
