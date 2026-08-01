// va_copy cursors taken at the start and after partial traversal, then
// forwarded by value to another translation unit, must read the same
// over-aligned argument sequence as the original cursor.
// Expected with overaligned_va_list_forwarding_xtu_other.c: exit=0.
#include <stdarg.h>
#include <stdint.h>

#ifndef AG_C_OVERALIGNED_VA_LIST_FORWARDING_XTU_TYPES
#define AG_C_OVERALIGNED_VA_LIST_FORWARDING_XTU_TYPES
struct aligned_forwarded32 {
  _Alignas(32) unsigned long long first;
  int second;
};

union aligned_forwarded64 {
  _Alignas(64) struct {
    unsigned long long first;
    unsigned long long second;
    double floating;
  } record;
  unsigned char bytes[64];
};
#endif

_Static_assert(_Alignof(struct aligned_forwarded32) == 32,
               "forwarded32 alignment");
_Static_assert(sizeof(struct aligned_forwarded32) == 32,
               "forwarded32 size");
_Static_assert(_Alignof(union aligned_forwarded64) == 64,
               "forwarded64 alignment");
_Static_assert(sizeof(union aligned_forwarded64) == 64,
               "forwarded64 size");

int read_overaligned_forwarded_arguments(int marker, va_list arguments);
int read_overaligned_forwarded_tail(int marker, va_list arguments);
typedef int overaligned_va_list_reader_t(int marker, va_list arguments);
typedef int overaligned_va_list_forwarder_t(int marker, ...);

static int is_aligned_in_forwarding_main(
    const void *pointer, uintptr_t alignment) {
  return (uintptr_t)pointer % alignment == 0;
}

static int check_overaligned_va_list_forwarding(int marker, ...) {
  va_list arguments;
  va_list forwarded;
  va_start(arguments, marker);
  va_copy(forwarded, arguments);

  int forwarded_result;
  if (marker == 91) {
    forwarded_result =
        read_overaligned_forwarded_arguments(marker, forwarded);
  } else {
    overaligned_va_list_reader_t *reader =
        read_overaligned_forwarded_arguments;
    forwarded_result = reader(marker, forwarded);
  }
  va_end(forwarded);

  int leading = va_arg(arguments, int);
  struct aligned_forwarded32 first =
      va_arg(arguments, struct aligned_forwarded32);
  double floating = va_arg(arguments, double);
  union aligned_forwarded64 middle =
      va_arg(arguments, union aligned_forwarded64);
  int *pointer = va_arg(arguments, int *);
  struct aligned_forwarded32 last =
      va_arg(arguments, struct aligned_forwarded32);
  long long trailing = va_arg(arguments, long long);
  va_end(arguments);

  int expected_leading = marker == 91 ? 7 : 9;
  double expected_floating = marker == 91 ? 2.5 : 3.5;
  long long expected_trailing = marker == 91 ? 43LL : 47LL;
  if ((marker != 91 && marker != 92) || forwarded_result != 42 ||
      leading != expected_leading || floating != expected_floating ||
      trailing != expected_trailing ||
      !is_aligned_in_forwarding_main(
          &first, _Alignof(struct aligned_forwarded32)) ||
      !is_aligned_in_forwarding_main(
          &middle, _Alignof(union aligned_forwarded64)) ||
      !is_aligned_in_forwarding_main(
          &last, _Alignof(struct aligned_forwarded32)) ||
      first.first != 11 || first.second != 13 ||
      middle.record.first != 17 || middle.record.second != 19 ||
      middle.record.floating != 4.5 || pointer == 0 || *pointer != 23 ||
      last.first != 29 || last.second != 31) {
    return 0;
  }
  return 42;
}

static int check_overaligned_partial_va_list_forwarding(int marker, ...) {
  va_list arguments;
  va_list forwarded;
  va_start(arguments, marker);

  int leading = va_arg(arguments, int);
  struct aligned_forwarded32 first =
      va_arg(arguments, struct aligned_forwarded32);
  double floating = va_arg(arguments, double);
  va_copy(forwarded, arguments);

  int forwarded_result;
  if (marker == 101) {
    forwarded_result = read_overaligned_forwarded_tail(marker, forwarded);
  } else {
    overaligned_va_list_reader_t *reader = read_overaligned_forwarded_tail;
    forwarded_result = reader(marker, forwarded);
  }
  va_end(forwarded);

  union aligned_forwarded64 middle =
      va_arg(arguments, union aligned_forwarded64);
  int *pointer = va_arg(arguments, int *);
  struct aligned_forwarded32 last =
      va_arg(arguments, struct aligned_forwarded32);
  long long trailing = va_arg(arguments, long long);
  va_end(arguments);

  int expected_leading = marker == 101 ? 37 : 39;
  double expected_floating = marker == 101 ? 6.5 : 7.5;
  long long expected_trailing = marker == 101 ? 53LL : 61LL;
  if ((marker != 101 && marker != 102) || forwarded_result != 42 ||
      leading != expected_leading || floating != expected_floating ||
      trailing != expected_trailing ||
      !is_aligned_in_forwarding_main(
          &first, _Alignof(struct aligned_forwarded32)) ||
      !is_aligned_in_forwarding_main(
          &middle, _Alignof(union aligned_forwarded64)) ||
      !is_aligned_in_forwarding_main(
          &last, _Alignof(struct aligned_forwarded32)) ||
      first.first != 11 || first.second != 13 ||
      middle.record.first != 17 || middle.record.second != 19 ||
      middle.record.floating != 4.5 || pointer == 0 || *pointer != 23 ||
      last.first != 29 || last.second != 31) {
    return 0;
  }
  return 42;
}

int main(void) {
  struct aligned_forwarded32 first = {11, 13};
  union aligned_forwarded64 middle = {
      .record = {17, 19, 4.5}};
  struct aligned_forwarded32 last = {29, 31};
  int sentinel = 23;

  if (check_overaligned_va_list_forwarding(
          91, 7, first, 2.5, middle, &sentinel, last, 43LL) != 42) {
    return 1;
  }

  overaligned_va_list_forwarder_t *forwarder =
      check_overaligned_va_list_forwarding;
  if (forwarder(
          92, 9, first, 3.5, middle, &sentinel, last, 47LL) != 42) {
    return 2;
  }

  if (check_overaligned_partial_va_list_forwarding(
          101, 37, first, 6.5, middle, &sentinel, last, 53LL) != 42) {
    return 3;
  }

  forwarder = check_overaligned_partial_va_list_forwarding;
  if (forwarder(
          102, 39, first, 7.5, middle, &sentinel, last, 61LL) != 42) {
    return 4;
  }
  return 0;
}
