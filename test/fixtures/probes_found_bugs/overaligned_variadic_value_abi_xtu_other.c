// Definition TU for cross-TU over-aligned variadic aggregate ABI coverage.
#include <stdarg.h>
#include <stdint.h>
#include <complex.h>

#ifndef AG_C_OVERALIGNED_VARIADIC_VALUE_ABI_XTU_TYPES
#define AG_C_OVERALIGNED_VARIADIC_VALUE_ABI_XTU_TYPES
struct aligned_variadic32 {
  _Alignas(32) unsigned long long first;
  int second;
};

union aligned_variadic64 {
  _Alignas(64) struct {
    unsigned long long first;
    unsigned long long second;
    int tag;
  } record;
  unsigned char bytes[64];
};
#endif

_Static_assert(_Alignof(struct aligned_variadic32) == 32,
               "aligned variadic32 alignment");
_Static_assert(sizeof(struct aligned_variadic32) == 32,
               "aligned variadic32 size");
_Static_assert(_Alignof(union aligned_variadic64) == 64,
               "aligned variadic64 alignment");
_Static_assert(sizeof(union aligned_variadic64) == 64,
               "aligned variadic64 size");

static int is_aligned_in_variadic_other(const void *pointer,
                                        uintptr_t alignment) {
  return (uintptr_t)pointer % alignment == 0;
}

int check_overaligned_variadic_values(int marker, ...) {
  va_list arguments;
  va_list copy;
  va_list tail_copy;
  va_start(arguments, marker);
  va_copy(copy, arguments);

  int leading = va_arg(arguments, int);
  float _Complex narrow = va_arg(arguments, float _Complex);
  struct aligned_variadic32 first =
      va_arg(arguments, struct aligned_variadic32);
  double floating = va_arg(arguments, double);
  double _Complex wide = va_arg(arguments, double _Complex);
  va_copy(tail_copy, arguments);
  union aligned_variadic64 middle =
      va_arg(arguments, union aligned_variadic64);
  int *pointer = va_arg(arguments, int *);
  struct aligned_variadic32 last =
      va_arg(arguments, struct aligned_variadic32);

  int copy_leading = va_arg(copy, int);
  float _Complex copy_narrow = va_arg(copy, float _Complex);
  struct aligned_variadic32 copy_first =
      va_arg(copy, struct aligned_variadic32);
  double copy_floating = va_arg(copy, double);
  double _Complex copy_wide = va_arg(copy, double _Complex);
  union aligned_variadic64 copy_middle =
      va_arg(copy, union aligned_variadic64);
  int *copy_pointer = va_arg(copy, int *);
  struct aligned_variadic32 copy_last =
      va_arg(copy, struct aligned_variadic32);

  union aligned_variadic64 tail_middle =
      va_arg(tail_copy, union aligned_variadic64);
  int *tail_pointer = va_arg(tail_copy, int *);
  struct aligned_variadic32 tail_last =
      va_arg(tail_copy, struct aligned_variadic32);

  va_end(tail_copy);
  va_end(copy);
  va_end(arguments);

  int expected_leading = marker == 91 ? 7 : 9;
  double expected_floating = marker == 91 ? 2.5 : 3.5;
  if ((marker != 91 && marker != 92) ||
      leading != expected_leading || floating != expected_floating ||
      !is_aligned_in_variadic_other(
          &first, _Alignof(struct aligned_variadic32)) ||
      !is_aligned_in_variadic_other(
          &middle, _Alignof(union aligned_variadic64)) ||
      !is_aligned_in_variadic_other(
          &last, _Alignof(struct aligned_variadic32)) ||
      narrow != 1.5f + 2.5f * I || wide != 3.25 + 4.75 * I ||
      first.first != 11 || first.second != 13 ||
      middle.record.first != 17 || middle.record.second != 19 ||
      middle.record.tag != 23 || pointer == 0 || *pointer != 37 ||
      last.first != 29 || last.second != 31 ||
      copy_leading != expected_leading ||
      copy_floating != expected_floating ||
      copy_narrow != 1.5f + 2.5f * I ||
      copy_wide != 3.25 + 4.75 * I ||
      !is_aligned_in_variadic_other(
          &copy_first, _Alignof(struct aligned_variadic32)) ||
      !is_aligned_in_variadic_other(
          &copy_middle, _Alignof(union aligned_variadic64)) ||
      !is_aligned_in_variadic_other(
          &copy_last, _Alignof(struct aligned_variadic32)) ||
      copy_first.first != 11 || copy_first.second != 13 ||
      copy_middle.record.first != 17 || copy_middle.record.second != 19 ||
      copy_middle.record.tag != 23 || copy_pointer == 0 ||
      *copy_pointer != 37 || copy_last.first != 29 || copy_last.second != 31 ||
      !is_aligned_in_variadic_other(
          &tail_middle, _Alignof(union aligned_variadic64)) ||
      !is_aligned_in_variadic_other(
          &tail_last, _Alignof(struct aligned_variadic32)) ||
      tail_middle.record.first != 17 || tail_middle.record.second != 19 ||
      tail_middle.record.tag != 23 || tail_pointer == 0 ||
      *tail_pointer != 37 || tail_last.first != 29 || tail_last.second != 31) {
    return 0;
  }
  return 42;
}
