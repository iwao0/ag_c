// Aggregate ABI temporaries, parameter objects, and variadic slots must
// preserve extended alignment inherited from over-aligned members.
// Expected: exit=0
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>

struct aligned16 {
  _Alignas(16) long first;
  int second;
};

struct aligned32 {
  _Alignas(32) unsigned char prefix;
  long values[2];
  int tail;
};

union aligned64 {
  _Alignas(64) struct {
    long first;
    long second;
    long third;
  } record;
  unsigned char bytes[64];
};

_Static_assert(_Alignof(struct aligned16) == 16,
               "aligned16 type alignment");
_Static_assert(_Alignof(struct aligned32) == 32,
               "aligned32 type alignment");
_Static_assert(_Alignof(union aligned64) == 64,
               "aligned64 type alignment");

static const struct aligned16 global16 = {11, 13};
static const struct aligned32 global32 = {
    17, {19, 23}, 29};
static const union aligned64 global64 = {
    .record = {31, 37, 41}};

static int is_aligned(const void *pointer, uintptr_t alignment) {
  return (uintptr_t)pointer % alignment == 0;
}

static int check16(
    struct aligned16 value, long first, int second) {
  return is_aligned(&value, _Alignof(struct aligned16)) &&
         value.first == first && value.second == second;
}

static int check32(
    struct aligned32 value,
    unsigned char prefix, long first, long second, int tail) {
  return is_aligned(&value, _Alignof(struct aligned32)) &&
         value.prefix == prefix &&
         value.values[0] == first &&
         value.values[1] == second &&
         value.tail == tail;
}

static int check64(
    union aligned64 value,
    long first, long second, long third) {
  return is_aligned(&value, _Alignof(union aligned64)) &&
         value.record.first == first &&
         value.record.second == second &&
         value.record.third == third;
}

static struct aligned16 rotate16(
    struct aligned16 value) {
  assert(is_aligned(&value, _Alignof(struct aligned16)));
  long first = value.first;
  value.first = value.second;
  value.second = (int)first;
  return value;
}

static struct aligned32 rotate32(
    struct aligned32 value) {
  assert(is_aligned(&value, _Alignof(struct aligned32)));
  long first = value.values[0];
  value.values[0] = value.values[1];
  value.values[1] = first;
  value.prefix++;
  value.tail++;
  return value;
}

static union aligned64 rotate64(
    union aligned64 value) {
  assert(is_aligned(&value, _Alignof(union aligned64)));
  long first = value.record.first;
  value.record.first = value.record.third;
  value.record.third = first;
  return value;
}

typedef struct aligned16 aligned16_transform_t(
    struct aligned16);
typedef struct aligned32 aligned32_transform_t(
    struct aligned32);
typedef union aligned64 aligned64_transform_t(
    union aligned64);

static int check_variadic_alignment(int marker, ...) {
  va_list arguments;
  va_start(arguments, marker);
  struct aligned16 value16 =
      va_arg(arguments, struct aligned16);
  struct aligned32 value32 =
      va_arg(arguments, struct aligned32);
  union aligned64 value64 =
      va_arg(arguments, union aligned64);
  va_end(arguments);

  return marker == 43 &&
         is_aligned(&value16, _Alignof(struct aligned16)) &&
         is_aligned(&value32, _Alignof(struct aligned32)) &&
         is_aligned(&value64, _Alignof(union aligned64)) &&
         check16(value16, 11, 13) &&
         check32(value32, 17, 19, 23, 29) &&
         check64(value64, 31, 37, 41);
}

static void verify_storage_and_calls(void) {
  assert(is_aligned(&global16, _Alignof(struct aligned16)));
  assert(is_aligned(&global32, _Alignof(struct aligned32)));
  assert(is_aligned(&global64, _Alignof(union aligned64)));
  assert(check16(global16, 11, 13));
  assert(check32(global32, 17, 19, 23, 29));
  assert(check64(global64, 31, 37, 41));

  aligned16_transform_t *transform16 = rotate16;
  aligned32_transform_t *transform32 = rotate32;
  aligned64_transform_t *transform64 = rotate64;
  struct aligned16 result16 = transform16(global16);
  struct aligned32 result32 = transform32(global32);
  union aligned64 result64 = transform64(global64);
  assert(is_aligned(&result16, _Alignof(struct aligned16)));
  assert(is_aligned(&result32, _Alignof(struct aligned32)));
  assert(is_aligned(&result64, _Alignof(union aligned64)));
  assert(check16(result16, 13, 11));
  assert(check32(result32, 18, 23, 19, 30));
  assert(check64(result64, 41, 37, 31));

  assert(check_variadic_alignment(
      43, global16, global32, global64));
}

static void verify_assignments_conditionals_and_canaries(void) {
  struct aligned16 right16 = {47, 53};
  struct aligned32 right32 = {
      59, {61, 67}, 71};
  union aligned64 right64 = {
      .record = {73, 79, 83}};
  int choose_left = 0;
  int comma_evaluations = 0;

  struct aligned16 selected16 =
      choose_left ? global16 : right16;
  struct aligned32 selected32 =
      choose_left ? global32 : right32;
  union aligned64 selected64 =
      choose_left ? global64 : right64;
  assert(is_aligned(&selected16, _Alignof(struct aligned16)));
  assert(is_aligned(&selected32, _Alignof(struct aligned32)));
  assert(is_aligned(&selected64, _Alignof(union aligned64)));
  assert(check16(selected16, 47, 53));
  assert(check32(selected32, 59, 61, 67, 71));
  assert(check64(selected64, 73, 79, 83));

  struct {
    unsigned char before;
    struct aligned32 value;
    unsigned char after;
  } box32 = {.before = 0x5a, .after = 0xa5};
  struct {
    unsigned char before;
    union aligned64 value;
    unsigned char after;
  } box64 = {.before = 0x3c, .after = 0xc3};

  struct aligned32 assignment32 =
      (box32.value = rotate32(global32));
  union aligned64 assignment64 =
      (box64.value = rotate64(global64));
  assert(is_aligned(&box32.value, _Alignof(struct aligned32)));
  assert(is_aligned(&box64.value, _Alignof(union aligned64)));
  assert(is_aligned(&assignment32, _Alignof(struct aligned32)));
  assert(is_aligned(&assignment64, _Alignof(union aligned64)));
  assert(box32.before == 0x5a && box32.after == 0xa5);
  assert(box64.before == 0x3c && box64.after == 0xc3);
  assert(check32(assignment32, 18, 23, 19, 30));
  assert(check64(assignment64, 41, 37, 31));

  struct aligned16 comma16 =
      (comma_evaluations++, global16);
  struct aligned32 comma32 =
      (comma_evaluations++, right32);
  union aligned64 comma64 =
      (comma_evaluations++, global64);
  assert(is_aligned(&comma16, _Alignof(struct aligned16)));
  assert(is_aligned(&comma32, _Alignof(struct aligned32)));
  assert(is_aligned(&comma64, _Alignof(union aligned64)));
  assert(check16(comma16, 11, 13));
  assert(check32(comma32, 59, 61, 67, 71));
  assert(check64(comma64, 31, 37, 41));
  assert(comma_evaluations == 3);
}

int main(void) {
  verify_storage_and_calls();
  verify_assignments_conditionals_and_canaries();
  return 0;
}
