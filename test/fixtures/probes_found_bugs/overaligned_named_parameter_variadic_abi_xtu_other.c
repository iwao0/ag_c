// Definition TU for a variadic ABI boundary with an over-aligned last named
// parameter.
#include <stdarg.h>
#include <stdint.h>

#ifndef AG_C_OVERALIGNED_NAMED_PARAMETER_VARIADIC_ABI_XTU_TYPES
#define AG_C_OVERALIGNED_NAMED_PARAMETER_VARIADIC_ABI_XTU_TYPES
struct aligned_named_variadic32 {
  _Alignas(32) unsigned long long first;
  int second;
};

union aligned_named_variadic64 {
  _Alignas(64) struct {
    unsigned long long first;
    double floating;
    int tag;
  } record;
  unsigned char bytes[64];
};
#endif

_Static_assert(_Alignof(struct aligned_named_variadic32) == 32,
               "named variadic32 alignment");
_Static_assert(sizeof(struct aligned_named_variadic32) == 32,
               "named variadic32 size");
_Static_assert(_Alignof(union aligned_named_variadic64) == 64,
               "named variadic64 alignment");
_Static_assert(sizeof(union aligned_named_variadic64) == 64,
               "named variadic64 size");

static int is_aligned_in_named_variadic_other(
    const void *pointer, uintptr_t alignment) {
  return (uintptr_t)pointer % alignment == 0;
}

int check_overaligned_named_parameter_variadic(
    int marker, struct aligned_named_variadic32 first_named,
    union aligned_named_variadic64 last_named, ...) {
  va_list arguments;
  va_start(arguments, last_named);
  int leading = va_arg(arguments, int);
  double floating = va_arg(arguments, double);
  struct aligned_named_variadic32 unnamed32 =
      va_arg(arguments, struct aligned_named_variadic32);
  int *pointer = va_arg(arguments, int *);
  union aligned_named_variadic64 unnamed64 =
      va_arg(arguments, union aligned_named_variadic64);
  long long trailing = va_arg(arguments, long long);
  va_end(arguments);

  int expected_leading = marker == 91 ? 43 : 53;
  double expected_floating = marker == 91 ? 4.5 : 5.5;
  long long expected_trailing = marker == 91 ? 47LL : 59LL;
  if ((marker != 91 && marker != 92) ||
      !is_aligned_in_named_variadic_other(
          &first_named, _Alignof(struct aligned_named_variadic32)) ||
      !is_aligned_in_named_variadic_other(
          &last_named, _Alignof(union aligned_named_variadic64)) ||
      !is_aligned_in_named_variadic_other(
          &unnamed32, _Alignof(struct aligned_named_variadic32)) ||
      !is_aligned_in_named_variadic_other(
          &unnamed64, _Alignof(union aligned_named_variadic64)) ||
      first_named.first != 11 || first_named.second != 13 ||
      last_named.record.first != 17 || last_named.record.floating != 2.5 ||
      last_named.record.tag != 19 || leading != expected_leading ||
      floating != expected_floating || unnamed32.first != 23 ||
      unnamed32.second != 29 || pointer == 0 || *pointer != 41 ||
      unnamed64.record.first != 31 || unnamed64.record.floating != 3.5 ||
      unnamed64.record.tag != 37 || trailing != expected_trailing) {
    return 0;
  }
  return 42;
}
