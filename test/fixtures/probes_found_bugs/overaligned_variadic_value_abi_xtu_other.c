// Definition TU for cross-TU over-aligned variadic aggregate ABI coverage.
#include <stdarg.h>
#include <stdint.h>

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
  va_start(arguments, marker);
  int leading = va_arg(arguments, int);
  struct aligned_variadic32 first =
      va_arg(arguments, struct aligned_variadic32);
  double floating = va_arg(arguments, double);
  union aligned_variadic64 middle =
      va_arg(arguments, union aligned_variadic64);
  int *pointer = va_arg(arguments, int *);
  struct aligned_variadic32 last =
      va_arg(arguments, struct aligned_variadic32);
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
      first.first != 11 || first.second != 13 ||
      middle.record.first != 17 || middle.record.second != 19 ||
      middle.record.tag != 23 || pointer == 0 || *pointer != 37 ||
      last.first != 29 || last.second != 31) {
    return 0;
  }
  return 42;
}
