// Definition TU for over-aligned va_list forwarding coverage.
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

static int is_aligned_in_forwarding_other(
    const void *pointer, uintptr_t alignment) {
  return (uintptr_t)pointer % alignment == 0;
}

int read_overaligned_forwarded_arguments(int marker, va_list arguments) {
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

  int expected_leading = marker == 91 ? 7 : 9;
  double expected_floating = marker == 91 ? 2.5 : 3.5;
  long long expected_trailing = marker == 91 ? 43LL : 47LL;
  if ((marker != 91 && marker != 92) ||
      leading != expected_leading || floating != expected_floating ||
      trailing != expected_trailing ||
      !is_aligned_in_forwarding_other(
          &first, _Alignof(struct aligned_forwarded32)) ||
      !is_aligned_in_forwarding_other(
          &middle, _Alignof(union aligned_forwarded64)) ||
      !is_aligned_in_forwarding_other(
          &last, _Alignof(struct aligned_forwarded32)) ||
      first.first != 11 || first.second != 13 ||
      middle.record.first != 17 || middle.record.second != 19 ||
      middle.record.floating != 4.5 || pointer == 0 || *pointer != 23 ||
      last.first != 29 || last.second != 31) {
    return 0;
  }
  return 42;
}

int read_overaligned_forwarded_tail(int marker, va_list arguments) {
  union aligned_forwarded64 middle =
      va_arg(arguments, union aligned_forwarded64);
  int *pointer = va_arg(arguments, int *);
  struct aligned_forwarded32 last =
      va_arg(arguments, struct aligned_forwarded32);
  long long trailing = va_arg(arguments, long long);

  long long expected_trailing = marker == 101 ? 53LL : 61LL;
  if ((marker != 101 && marker != 102) ||
      trailing != expected_trailing ||
      !is_aligned_in_forwarding_other(
          &middle, _Alignof(union aligned_forwarded64)) ||
      !is_aligned_in_forwarding_other(
          &last, _Alignof(struct aligned_forwarded32)) ||
      middle.record.first != 17 || middle.record.second != 19 ||
      middle.record.floating != 4.5 || pointer == 0 || *pointer != 23 ||
      last.first != 29 || last.second != 31) {
    return 0;
  }
  return 42;
}
