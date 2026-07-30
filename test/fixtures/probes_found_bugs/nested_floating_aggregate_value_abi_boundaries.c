// Nested homogeneous floating aggregates and mixed real-width aggregates
// preserve every component across calls, returns, and variadic access.
// Expected: exit=0
#include <assert.h>
#include <stdarg.h>

struct float_pair {
  float first;
  float second;
};

struct nested_float_hfa {
  struct float_pair top;
  struct float_pair bottom;
};

struct double_leaf {
  double value;
};

struct nested_double_hfa {
  struct double_leaf first;
  struct double_leaf second;
  struct double_leaf third;
};

struct mixed_real_values {
  int tag;
  float narrow;
  double regular;
  long double extended;
  double values[2];
  long tail;
};

struct real_pair {
  double first;
  double second;
};

union real_choice {
  struct real_pair pair;
  long double extended;
  unsigned long long words[2];
};

typedef struct nested_float_hfa float_hfa_callback_t(
    struct nested_float_hfa, float);
typedef struct nested_double_hfa double_hfa_callback_t(
    struct nested_double_hfa, double);
typedef struct mixed_real_values mixed_real_callback_t(
    struct mixed_real_values, int);
typedef union real_choice real_choice_callback_t(
    union real_choice, int);

static struct nested_float_hfa make_float_hfa(float base) {
  struct nested_float_hfa value = {
      .top = {base, base + 1.0f},
      .bottom = {base + 2.0f, base + 3.0f},
  };
  return value;
}

static int float_hfa_is(struct nested_float_hfa value,
                        float base) {
  return value.top.first == base &&
         value.top.second == base + 1.0f &&
         value.bottom.first == base + 2.0f &&
         value.bottom.second == base + 3.0f;
}

static struct nested_double_hfa make_double_hfa(
    double base) {
  struct nested_double_hfa value = {
      .first = {base},
      .second = {base + 1.0},
      .third = {base + 2.0},
  };
  return value;
}

static int double_hfa_is(struct nested_double_hfa value,
                         double base) {
  return value.first.value == base &&
         value.second.value == base + 1.0 &&
         value.third.value == base + 2.0;
}

static struct mixed_real_values make_mixed(int seed) {
  struct mixed_real_values value = {
      .tag = 100 + seed,
      .narrow = 10.5f + (float)seed,
      .regular = 20.25 + (double)seed,
      .extended = 30.75L + (long double)seed,
      .values = {
          40.125 + (double)seed,
          50.625 + (double)seed,
      },
      .tail = 200 + seed,
  };
  return value;
}

static int mixed_is(struct mixed_real_values value,
                    int seed) {
  return value.tag == 100 + seed &&
         value.narrow == 10.5f + (float)seed &&
         value.regular == 20.25 + (double)seed &&
         value.extended == 30.75L + (long double)seed &&
         value.values[0] == 40.125 + (double)seed &&
         value.values[1] == 50.625 + (double)seed &&
         value.tail == 200 + seed;
}

static union real_choice make_choice(double base) {
  union real_choice value = {
      .pair = {base, base + 1.0},
  };
  return value;
}

static int choice_is(union real_choice value,
                     double base) {
  return value.pair.first == base &&
         value.pair.second == base + 1.0;
}

static struct nested_float_hfa shift_float_hfa(
    struct nested_float_hfa value, float amount) {
  value.top.first += amount;
  value.top.second += amount;
  value.bottom.first += amount;
  value.bottom.second += amount;
  return value;
}

static struct nested_double_hfa shift_double_hfa(
    struct nested_double_hfa value, double amount) {
  value.first.value += amount;
  value.second.value += amount;
  value.third.value += amount;
  return value;
}

static struct mixed_real_values advance_mixed(
    struct mixed_real_values value, int amount) {
  value.tag += amount;
  value.narrow += (float)amount;
  value.regular += (double)amount;
  value.extended += (long double)amount;
  value.values[0] += (double)amount;
  value.values[1] += (double)amount;
  value.tail += amount;
  return value;
}

static union real_choice shift_choice(
    union real_choice value, int amount) {
  value.pair.first += (double)amount;
  value.pair.second += (double)amount;
  return value;
}

static int check_variadic_real_aggregates(int marker, ...) {
  va_list arguments;
  va_start(arguments, marker);
  struct nested_float_hfa float_hfa =
      va_arg(arguments, struct nested_float_hfa);
  struct nested_double_hfa double_hfa =
      va_arg(arguments, struct nested_double_hfa);
  struct mixed_real_values mixed =
      va_arg(arguments, struct mixed_real_values);
  union real_choice choice =
      va_arg(arguments, union real_choice);
  va_end(arguments);
  return marker == 104 &&
         float_hfa_is(float_hfa, 4.5f) &&
         double_hfa_is(double_hfa, 5.25) &&
         mixed_is(mixed, 6) &&
         choice_is(choice, 7.75);
}

static struct nested_float_hfa float_sources[2];
static struct nested_double_hfa double_sources[2];
static struct mixed_real_values mixed_sources[2];
static union real_choice choice_sources[2];
static int float_selections;
static int double_selections;
static int mixed_selections;
static int choice_selections;

static struct nested_float_hfa *select_float_hfa(int index) {
  float_selections++;
  return &float_sources[index];
}

static struct nested_double_hfa *select_double_hfa(int index) {
  double_selections++;
  return &double_sources[index];
}

static struct mixed_real_values *select_mixed(int index) {
  mixed_selections++;
  return &mixed_sources[index];
}

static union real_choice *select_choice(int index) {
  choice_selections++;
  return &choice_sources[index];
}

int main(void) {
  float_hfa_callback_t *float_callback = shift_float_hfa;
  double_hfa_callback_t *double_callback = shift_double_hfa;
  mixed_real_callback_t *mixed_callback = advance_mixed;
  real_choice_callback_t *choice_callback = shift_choice;
  struct nested_float_hfa float_result =
      float_callback(make_float_hfa(1.5f), 2.0f);
  struct nested_double_hfa double_result =
      double_callback(make_double_hfa(2.25), 3.0);
  struct {
    unsigned long before;
    struct mixed_real_values value;
    unsigned long after;
  } guarded_mixed = {
      .before = 0x11223344UL,
      .value = {0},
      .after = 0x55667788UL,
  };
  struct {
    unsigned long before;
    union real_choice value;
    unsigned long after;
  } guarded_choice = {
      .before = 0x99aabbccUL,
      .value = {0},
      .after = 0xddeeff00UL,
  };

  assert(float_hfa_is(float_result, 3.5f));
  assert(double_hfa_is(double_result, 5.25));
  guarded_mixed.value = mixed_callback(make_mixed(3), 4);
  assert(guarded_mixed.before == 0x11223344UL);
  assert(guarded_mixed.after == 0x55667788UL);
  assert(mixed_is(guarded_mixed.value, 7));
  guarded_choice.value =
      choice_callback(make_choice(4.75), 5);
  assert(guarded_choice.before == 0x99aabbccUL);
  assert(guarded_choice.after == 0xddeeff00UL);
  assert(choice_is(guarded_choice.value, 9.75));

  assert(check_variadic_real_aggregates(
      104, make_float_hfa(4.5f), make_double_hfa(5.25),
      make_mixed(6), make_choice(7.75)));

  float_sources[0] = make_float_hfa(10.5f);
  float_sources[1] = make_float_hfa(20.5f);
  double_sources[0] = make_double_hfa(30.25);
  double_sources[1] = make_double_hfa(40.25);
  mixed_sources[0] = make_mixed(8);
  mixed_sources[1] = make_mixed(9);
  choice_sources[0] = make_choice(50.75);
  choice_sources[1] = make_choice(60.75);

  assert(float_hfa_is(
      1 ? *select_float_hfa(0) : *select_float_hfa(1),
      10.5f));
  assert(float_selections == 1);
  assert(double_hfa_is(
      0 ? *select_double_hfa(0) : *select_double_hfa(1),
      40.25));
  assert(double_selections == 1);
  assert(mixed_is(
      1 ? *select_mixed(0) : *select_mixed(1), 8));
  assert(mixed_selections == 1);
  assert(choice_is(
      0 ? *select_choice(0) : *select_choice(1), 60.75));
  assert(choice_selections == 1);

  assert(float_hfa_is(
      (float_selections++, float_sources[1]), 20.5f));
  assert(float_selections == 2);
  assert(double_hfa_is(
      (double_selections++, double_sources[0]), 30.25));
  assert(double_selections == 2);
  assert(mixed_is(
      (mixed_selections++, mixed_sources[1]), 9));
  assert(mixed_selections == 2);
  assert(choice_is(
      (choice_selections++, choice_sources[0]), 50.75));
  assert(choice_selections == 2);

  float_result = make_float_hfa(70.5f);
  assert(float_hfa_is(
      (float_result = make_float_hfa(80.5f)), 80.5f));
  double_result = make_double_hfa(90.25);
  assert(double_hfa_is(
      (double_result = make_double_hfa(100.25)), 100.25));
  guarded_mixed.value = make_mixed(10);
  assert(mixed_is(
      (guarded_mixed.value = make_mixed(11)), 11));
  guarded_choice.value = make_choice(110.75);
  assert(choice_is(
      (guarded_choice.value = make_choice(120.75)), 120.75));
  assert(guarded_mixed.before == 0x11223344UL);
  assert(guarded_mixed.after == 0x55667788UL);
  assert(guarded_choice.before == 0x99aabbccUL);
  assert(guarded_choice.after == 0xddeeff00UL);
  return 0;
}
