// Aggregates containing const subobjects are not modifiable lvalues, but they
// remain valid values for initialization, calls, returns, and variadic access.
// Expected: exit=0
#include <assert.h>
#include <stdarg.h>

typedef const int immutable_int;

struct direct_const {
  const int key;
  int payload;
};

struct inner_const {
  const long key;
  int values[2];
};

struct nested_const {
  struct inner_const rows[2];
  immutable_int numbers[3];
  int *const pointer;
  const char label[3];
  long tail;
};

struct const_pair {
  const int key;
  int payload;
};

union const_choice {
  struct const_pair record;
  const double real;
  long words[2];
};

typedef struct direct_const direct_const_callback_t(
    struct direct_const, int);
typedef struct nested_const nested_const_callback_t(
    struct nested_const, int);
typedef union const_choice const_choice_callback_t(
    union const_choice, int);

static int anchors[2] = {700, 800};

static struct direct_const make_direct(int seed) {
  struct direct_const value = {
      .key = 100 + seed,
      .payload = -200 - seed,
  };
  return value;
}

static int direct_is(struct direct_const value,
                     int seed) {
  return value.key == 100 + seed &&
         value.payload == -200 - seed;
}

static struct nested_const make_nested(int seed) {
  struct nested_const value = {
      .rows = {
          {
              .key = 1000 + seed,
              .values = {10 + seed, 20 + seed},
          },
          {
              .key = 2000 + seed,
              .values = {30 + seed, 40 + seed},
          },
      },
      .numbers = {50 + seed, 60 + seed, 70 + seed},
      .pointer = &anchors[seed & 1],
      .label = {
          (char)('A' + seed),
          (char)('a' + seed),
          '\0',
      },
      .tail = 3000 + seed,
  };
  return value;
}

static int nested_is(struct nested_const value,
                     int seed) {
  return value.rows[0].key == 1000 + seed &&
         value.rows[0].values[0] == 10 + seed &&
         value.rows[0].values[1] == 20 + seed &&
         value.rows[1].key == 2000 + seed &&
         value.rows[1].values[0] == 30 + seed &&
         value.rows[1].values[1] == 40 + seed &&
         value.numbers[0] == 50 + seed &&
         value.numbers[1] == 60 + seed &&
         value.numbers[2] == 70 + seed &&
         value.pointer == &anchors[seed & 1] &&
         *value.pointer == anchors[seed & 1] &&
         value.label[0] == (char)('A' + seed) &&
         value.label[1] == (char)('a' + seed) &&
         value.label[2] == '\0' &&
         value.tail == 3000 + seed;
}

static union const_choice make_choice(int seed) {
  union const_choice value = {
      .record = {
          .key = 400 + seed,
          .payload = -500 - seed,
      },
  };
  return value;
}

static int choice_is(union const_choice value,
                     int seed) {
  return value.record.key == 400 + seed &&
         value.record.payload == -500 - seed;
}

static struct direct_const roundtrip_direct(
    struct direct_const value, int marker) {
  assert(marker == 101);
  return value;
}

static struct nested_const roundtrip_nested(
    struct nested_const value, int marker) {
  assert(marker == 102);
  return value;
}

static union const_choice roundtrip_choice(
    union const_choice value, int marker) {
  assert(marker == 103);
  return value;
}

static int check_variadic_const_values(int marker, ...) {
  va_list arguments;
  va_start(arguments, marker);
  struct direct_const direct =
      va_arg(arguments, struct direct_const);
  struct nested_const nested =
      va_arg(arguments, struct nested_const);
  union const_choice choice =
      va_arg(arguments, union const_choice);
  va_end(arguments);
  return marker == 104 &&
         direct_is(direct, 4) &&
         nested_is(nested, 5) &&
         choice_is(choice, 6);
}

static int direct_selections;
static int nested_selections;
static int choice_selections;

static struct direct_const *select_direct(
    struct direct_const *values, int index) {
  direct_selections++;
  return &values[index];
}

static struct nested_const *select_nested(
    struct nested_const *values, int index) {
  nested_selections++;
  return &values[index];
}

static union const_choice *select_choice(
    union const_choice *values, int index) {
  choice_selections++;
  return &values[index];
}

int main(void) {
  direct_const_callback_t *direct_callback =
      roundtrip_direct;
  nested_const_callback_t *nested_callback =
      roundtrip_nested;
  const_choice_callback_t *choice_callback =
      roundtrip_choice;
  struct direct_const direct =
      direct_callback(make_direct(1), 101);
  struct {
    unsigned long before;
    struct nested_const value;
    unsigned long after;
  } guarded_nested = {
      .before = 0x11223344UL,
      .value = nested_callback(make_nested(2), 102),
      .after = 0x55667788UL,
  };
  struct {
    unsigned long before;
    union const_choice value;
    unsigned long after;
  } guarded_choice = {
      .before = 0x99aabbccUL,
      .value = choice_callback(make_choice(3), 103),
      .after = 0xddeeff00UL,
  };
  struct direct_const direct_sources[2] = {
      make_direct(7),
      make_direct(8),
  };
  struct nested_const nested_sources[2] = {
      make_nested(9),
      make_nested(10),
  };
  union const_choice choice_sources[2] = {
      make_choice(11),
      make_choice(12),
  };

  assert(direct_is(direct, 1));
  assert(guarded_nested.before == 0x11223344UL);
  assert(guarded_nested.after == 0x55667788UL);
  assert(nested_is(guarded_nested.value, 2));
  assert(guarded_choice.before == 0x99aabbccUL);
  assert(guarded_choice.after == 0xddeeff00UL);
  assert(choice_is(guarded_choice.value, 3));

  assert(check_variadic_const_values(
      104, make_direct(4), make_nested(5), make_choice(6)));

  assert(direct_is(
      1 ? *select_direct(direct_sources, 0)
        : *select_direct(direct_sources, 1),
      7));
  assert(direct_selections == 1);
  assert(nested_is(
      0 ? *select_nested(nested_sources, 0)
        : *select_nested(nested_sources, 1),
      10));
  assert(nested_selections == 1);
  assert(choice_is(
      1 ? *select_choice(choice_sources, 0)
        : *select_choice(choice_sources, 1),
      11));
  assert(choice_selections == 1);

  assert(direct_is(
      (direct_selections++, direct_sources[1]), 8));
  assert(direct_selections == 2);
  assert(nested_is(
      (nested_selections++, nested_sources[0]), 9));
  assert(nested_selections == 2);
  assert(choice_is(
      (choice_selections++, choice_sources[1]), 12));
  assert(choice_selections == 2);

  {
    struct direct_const initialized = direct_sources[0];
    struct nested_const nested_initialized =
        nested_sources[1];
    union const_choice choice_initialized =
        choice_sources[0];
    assert(direct_is(initialized, 7));
    assert(nested_is(nested_initialized, 10));
    assert(choice_is(choice_initialized, 11));
  }
  return 0;
}
