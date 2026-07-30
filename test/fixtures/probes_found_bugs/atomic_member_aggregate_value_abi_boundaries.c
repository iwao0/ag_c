// A non-atomic aggregate containing atomic scalar and aggregate subobjects
// remains a normal aggregate value across calls, returns, and variadic access.
// Expected: exit=0
#include <assert.h>
#include <stdarg.h>

struct bytes3 {
  unsigned char values[3];
};

struct compact_atomic_members {
  _Atomic(int) count;
  _Atomic(unsigned short) code;
};

struct wide_atomic_members {
  int marker;
  _Atomic(int) counts[2];
  _Atomic(struct bytes3) packets[2];
  _Atomic(unsigned long long) token;
  _Atomic(double) real;
  long tail;
};

struct atomic_choice_fields {
  _Atomic(int) count;
  _Atomic(struct bytes3) packet;
};

union atomic_member_choice {
  struct atomic_choice_fields fields;
  unsigned char reserve[16];
};

typedef struct compact_atomic_members compact_atomic_callback_t(
    struct compact_atomic_members, int);
typedef struct wide_atomic_members wide_atomic_callback_t(
    struct wide_atomic_members, int);
typedef union atomic_member_choice atomic_choice_callback_t(
    union atomic_member_choice, int);

static struct compact_atomic_members make_compact(int seed) {
  struct compact_atomic_members value = {
      .count = 100 + seed,
      .code = (unsigned short)(50000 + seed),
  };
  return value;
}

static int compact_is(struct compact_atomic_members value,
                      int seed) {
  int count = value.count;
  unsigned short code = value.code;
  return count == 100 + seed &&
         code == (unsigned short)(50000 + seed);
}

static struct wide_atomic_members make_wide(int seed) {
  struct wide_atomic_members value = {
      .marker = 200 + seed,
      .counts = {300 + seed, 400 + seed},
      .packets = {
          (struct bytes3){{
              (unsigned char)(10 + seed),
              (unsigned char)(20 + seed),
              (unsigned char)(30 + seed)}},
          (struct bytes3){{
              (unsigned char)(40 + seed),
              (unsigned char)(50 + seed),
              (unsigned char)(60 + seed)}},
      },
      .token = 0x1122334455667700ULL +
               (unsigned long long)seed,
      .real = 70.5 + (double)seed,
      .tail = 500 + seed,
  };
  return value;
}

static int wide_is(struct wide_atomic_members value,
                   int seed) {
  int first_count = value.counts[0];
  int second_count = value.counts[1];
  struct bytes3 first_packet = value.packets[0];
  struct bytes3 second_packet = value.packets[1];
  unsigned long long token = value.token;
  double real = value.real;
  return value.marker == 200 + seed &&
         first_count == 300 + seed &&
         second_count == 400 + seed &&
         first_packet.values[0] ==
             (unsigned char)(10 + seed) &&
         first_packet.values[1] ==
             (unsigned char)(20 + seed) &&
         first_packet.values[2] ==
             (unsigned char)(30 + seed) &&
         second_packet.values[0] ==
             (unsigned char)(40 + seed) &&
         second_packet.values[1] ==
             (unsigned char)(50 + seed) &&
         second_packet.values[2] ==
             (unsigned char)(60 + seed) &&
         token == 0x1122334455667700ULL +
                      (unsigned long long)seed &&
         real == 70.5 + (double)seed &&
         value.tail == 500 + seed;
}

static union atomic_member_choice make_choice(int seed) {
  union atomic_member_choice value = {
      .fields = {
          .count = 600 + seed,
          .packet = (struct bytes3){{
               (unsigned char)(70 + seed),
               (unsigned char)(80 + seed),
               (unsigned char)(90 + seed)}},
      },
  };
  return value;
}

static int choice_is(union atomic_member_choice value,
                     int seed) {
  int count = value.fields.count;
  struct bytes3 packet = value.fields.packet;
  return count == 600 + seed &&
         packet.values[0] == (unsigned char)(70 + seed) &&
         packet.values[1] == (unsigned char)(80 + seed) &&
         packet.values[2] == (unsigned char)(90 + seed);
}

static struct compact_atomic_members advance_compact(
    struct compact_atomic_members value, int amount) {
  int count = value.count;
  unsigned short code = value.code;
  value.count = count + amount;
  value.code = (unsigned short)(code + amount);
  return value;
}

static struct wide_atomic_members advance_wide(
    struct wide_atomic_members value, int amount) {
  int first_count = value.counts[0];
  int second_count = value.counts[1];
  struct bytes3 first_packet = value.packets[0];
  struct bytes3 second_packet = value.packets[1];
  unsigned long long token = value.token;
  double real = value.real;
  int index;
  value.marker += amount;
  value.counts[0] = first_count + amount;
  value.counts[1] = second_count + amount;
  for (index = 0; index < 3; index++) {
    first_packet.values[index] =
        (unsigned char)(first_packet.values[index] + amount);
    second_packet.values[index] =
        (unsigned char)(second_packet.values[index] + amount);
  }
  value.packets[0] = first_packet;
  value.packets[1] = second_packet;
  value.token = token + (unsigned long long)amount;
  value.real = real + (double)amount;
  value.tail += amount;
  return value;
}

static union atomic_member_choice advance_choice(
    union atomic_member_choice value, int amount) {
  int count = value.fields.count;
  struct bytes3 packet = value.fields.packet;
  int index;
  value.fields.count = count + amount;
  for (index = 0; index < 3; index++) {
    packet.values[index] =
        (unsigned char)(packet.values[index] + amount);
  }
  value.fields.packet = packet;
  return value;
}

static int check_variadic_atomic_members(int marker, ...) {
  va_list arguments;
  va_start(arguments, marker);
  struct compact_atomic_members compact =
      va_arg(arguments, struct compact_atomic_members);
  struct wide_atomic_members wide =
      va_arg(arguments, struct wide_atomic_members);
  union atomic_member_choice choice =
      va_arg(arguments, union atomic_member_choice);
  va_end(arguments);
  return marker == 104 &&
         compact_is(compact, 4) &&
         wide_is(wide, 5) &&
         choice_is(choice, 6);
}

static struct compact_atomic_members compact_sources[2];
static struct wide_atomic_members wide_sources[2];
static union atomic_member_choice choice_sources[2];
static int compact_selections;
static int wide_selections;
static int choice_selections;

static struct compact_atomic_members *select_compact(int index) {
  compact_selections++;
  return &compact_sources[index];
}

static struct wide_atomic_members *select_wide(int index) {
  wide_selections++;
  return &wide_sources[index];
}

static union atomic_member_choice *select_choice(int index) {
  choice_selections++;
  return &choice_sources[index];
}

int main(void) {
  compact_atomic_callback_t *compact_callback =
      advance_compact;
  wide_atomic_callback_t *wide_callback = advance_wide;
  atomic_choice_callback_t *choice_callback = advance_choice;
  struct compact_atomic_members compact =
      compact_callback(make_compact(1), 2);
  struct {
    unsigned long before;
    struct wide_atomic_members value;
    unsigned long after;
  } guarded_wide = {
      .before = 0x11223344UL,
      .value = {0},
      .after = 0x55667788UL,
  };
  struct {
    unsigned long before;
    union atomic_member_choice value;
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

  assert(check_variadic_atomic_members(
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
