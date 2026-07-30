// A non-volatile aggregate containing volatile subobjects remains a normal
// aggregate value across assignments, calls, returns, and variadic access.
// Expected: exit=0
#include <assert.h>
#include <stdarg.h>

typedef volatile unsigned short volatile_ushort;

struct compact_volatile_members {
  volatile int count;
  volatile_ushort code;
};

struct volatile_row {
  volatile signed char tag;
  volatile double real;
};

struct wide_volatile_members {
  int marker;
  struct volatile_row rows[2];
  volatile int samples[3];
  int *volatile pointer;
  volatile unsigned long long token;
  long tail;
};

struct volatile_choice_fields {
  volatile int count;
  volatile unsigned char bytes[3];
};

union volatile_member_choice {
  struct volatile_choice_fields fields;
  unsigned char reserve[16];
};

typedef struct compact_volatile_members compact_volatile_callback_t(
    struct compact_volatile_members, int);
typedef struct wide_volatile_members wide_volatile_callback_t(
    struct wide_volatile_members, int);
typedef union volatile_member_choice volatile_choice_callback_t(
    union volatile_member_choice, int);

static int anchors[2] = {700, 800};

static struct compact_volatile_members make_compact(int seed) {
  struct compact_volatile_members value = {
      .count = 100 + seed,
      .code = (unsigned short)(50000 + seed),
  };
  return value;
}

static int compact_is(struct compact_volatile_members value,
                      int seed) {
  int count = value.count;
  unsigned short code = value.code;
  return count == 100 + seed &&
         code == (unsigned short)(50000 + seed);
}

static struct wide_volatile_members make_wide(int seed) {
  struct wide_volatile_members value = {
      .marker = 200 + seed,
      .rows = {
          {
              .tag = (signed char)(10 + seed),
              .real = 20.5 + (double)seed,
          },
          {
              .tag = (signed char)(30 + seed),
              .real = 40.5 + (double)seed,
          },
      },
      .samples = {
          300 + seed,
          400 + seed,
          500 + seed,
      },
      .pointer = &anchors[seed & 1],
      .token = 0x1122334455667700ULL +
               (unsigned long long)seed,
      .tail = 600 + seed,
  };
  return value;
}

static int wide_is(struct wide_volatile_members value,
                   int seed) {
  signed char first_tag = value.rows[0].tag;
  double first_real = value.rows[0].real;
  signed char second_tag = value.rows[1].tag;
  double second_real = value.rows[1].real;
  int first_sample = value.samples[0];
  int second_sample = value.samples[1];
  int third_sample = value.samples[2];
  int *pointer = value.pointer;
  unsigned long long token = value.token;
  return value.marker == 200 + seed &&
         first_tag == (signed char)(10 + seed) &&
         first_real == 20.5 + (double)seed &&
         second_tag == (signed char)(30 + seed) &&
         second_real == 40.5 + (double)seed &&
         first_sample == 300 + seed &&
         second_sample == 400 + seed &&
         third_sample == 500 + seed &&
         pointer == &anchors[seed & 1] &&
         *pointer == anchors[seed & 1] &&
         token == 0x1122334455667700ULL +
                      (unsigned long long)seed &&
         value.tail == 600 + seed;
}

static union volatile_member_choice make_choice(int seed) {
  union volatile_member_choice value = {
      .fields = {
          .count = 700 + seed,
          .bytes = {
              (unsigned char)(50 + seed),
              (unsigned char)(60 + seed),
              (unsigned char)(70 + seed),
          },
      },
  };
  return value;
}

static int choice_is(union volatile_member_choice value,
                     int seed) {
  int count = value.fields.count;
  unsigned char first = value.fields.bytes[0];
  unsigned char second = value.fields.bytes[1];
  unsigned char third = value.fields.bytes[2];
  return count == 700 + seed &&
         first == (unsigned char)(50 + seed) &&
         second == (unsigned char)(60 + seed) &&
         third == (unsigned char)(70 + seed);
}

static struct compact_volatile_members advance_compact(
    struct compact_volatile_members value, int amount) {
  int count = value.count;
  unsigned short code = value.code;
  value.count = count + amount;
  value.code = (unsigned short)(code + amount);
  return value;
}

static struct wide_volatile_members advance_wide(
    struct wide_volatile_members value, int amount) {
  signed char first_tag = value.rows[0].tag;
  double first_real = value.rows[0].real;
  signed char second_tag = value.rows[1].tag;
  double second_real = value.rows[1].real;
  int first_sample = value.samples[0];
  int second_sample = value.samples[1];
  int third_sample = value.samples[2];
  int *pointer = value.pointer;
  unsigned long long token = value.token;
  value.marker += amount;
  value.rows[0].tag = (signed char)(first_tag + amount);
  value.rows[0].real = first_real + (double)amount;
  value.rows[1].tag = (signed char)(second_tag + amount);
  value.rows[1].real = second_real + (double)amount;
  value.samples[0] = first_sample + amount;
  value.samples[1] = second_sample + amount;
  value.samples[2] = third_sample + amount;
  value.pointer =
      (amount & 1)
          ? (pointer == &anchors[0] ? &anchors[1] : &anchors[0])
          : pointer;
  value.token = token + (unsigned long long)amount;
  value.tail += amount;
  return value;
}

static union volatile_member_choice advance_choice(
    union volatile_member_choice value, int amount) {
  int count = value.fields.count;
  unsigned char first = value.fields.bytes[0];
  unsigned char second = value.fields.bytes[1];
  unsigned char third = value.fields.bytes[2];
  value.fields.count = count + amount;
  value.fields.bytes[0] = (unsigned char)(first + amount);
  value.fields.bytes[1] = (unsigned char)(second + amount);
  value.fields.bytes[2] = (unsigned char)(third + amount);
  return value;
}

static int check_variadic_volatile_members(int marker, ...) {
  va_list arguments;
  va_start(arguments, marker);
  struct compact_volatile_members compact =
      va_arg(arguments, struct compact_volatile_members);
  struct wide_volatile_members wide =
      va_arg(arguments, struct wide_volatile_members);
  union volatile_member_choice choice =
      va_arg(arguments, union volatile_member_choice);
  va_end(arguments);
  return marker == 104 &&
         compact_is(compact, 4) &&
         wide_is(wide, 5) &&
         choice_is(choice, 6);
}

static struct compact_volatile_members compact_sources[2];
static struct wide_volatile_members wide_sources[2];
static union volatile_member_choice choice_sources[2];
static int compact_selections;
static int wide_selections;
static int choice_selections;

static struct compact_volatile_members *select_compact(int index) {
  compact_selections++;
  return &compact_sources[index];
}

static struct wide_volatile_members *select_wide(int index) {
  wide_selections++;
  return &wide_sources[index];
}

static union volatile_member_choice *select_choice(int index) {
  choice_selections++;
  return &choice_sources[index];
}

int main(void) {
  compact_volatile_callback_t *compact_callback =
      advance_compact;
  wide_volatile_callback_t *wide_callback = advance_wide;
  volatile_choice_callback_t *choice_callback = advance_choice;
  struct compact_volatile_members compact =
      compact_callback(make_compact(1), 2);
  struct {
    unsigned long before;
    struct wide_volatile_members value;
    unsigned long after;
  } guarded_wide = {
      .before = 0x11223344UL,
      .value = {0},
      .after = 0x55667788UL,
  };
  struct {
    unsigned long before;
    union volatile_member_choice value;
    unsigned long after;
  } guarded_choice = {
      .before = 0x99aabbccUL,
      .value = {0},
      .after = 0xddeeff00UL,
  };

  assert(compact_is(compact, 3));
  guarded_wide.value = wide_callback(make_wide(2), 3);
  assert(guarded_wide.before == 0x11223344UL);
  assert(guarded_wide.after == 0x55667788UL);
  assert(wide_is(guarded_wide.value, 5));
  guarded_choice.value =
      choice_callback(make_choice(3), 4);
  assert(guarded_choice.before == 0x99aabbccUL);
  assert(guarded_choice.after == 0xddeeff00UL);
  assert(choice_is(guarded_choice.value, 7));

  assert(check_variadic_volatile_members(
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
