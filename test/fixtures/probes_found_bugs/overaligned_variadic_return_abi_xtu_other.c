// Definition TU for the over-aligned variadic aggregate return ABI fixture.
#include <stdarg.h>
#include <stdint.h>

#ifndef AG_C_OVERALIGNED_VARIADIC_RETURN_ABI_XTU_TYPES
#define AG_C_OVERALIGNED_VARIADIC_RETURN_ABI_XTU_TYPES
struct aligned_variadic_return_arg32 {
  _Alignas(32) unsigned long long first;
  int second;
};

union aligned_variadic_return64 {
  _Alignas(64) struct {
    unsigned long long first;
    unsigned long long second;
    int tag;
  } record;
  unsigned char bytes[64];
};
#endif

_Static_assert(_Alignof(struct aligned_variadic_return_arg32) == 32,
               "aligned variadic return argument alignment");
_Static_assert(sizeof(struct aligned_variadic_return_arg32) == 32,
               "aligned variadic return argument size");
_Static_assert(_Alignof(union aligned_variadic_return64) == 64,
               "aligned variadic return result alignment");
_Static_assert(sizeof(union aligned_variadic_return64) == 64,
               "aligned variadic return result size");

static int is_aligned_in_variadic_return_other(
    const void *pointer, uintptr_t alignment) {
  return (uintptr_t)pointer % alignment == 0;
}

union aligned_variadic_return64 build_overaligned_variadic_result(
    int marker, ...) {
  va_list arguments;
  va_start(arguments, marker);
  struct aligned_variadic_return_arg32 input =
      va_arg(arguments, struct aligned_variadic_return_arg32);
  int integer = va_arg(arguments, int);
  double floating = va_arg(arguments, double);
  int *pointer = va_arg(arguments, int *);
  va_end(arguments);

  union aligned_variadic_return64 result = {
      .record = {
          input.first + (unsigned long long)integer,
          (unsigned long long)input.second +
              (unsigned long long)(floating * 2.0),
          pointer != 0 ? marker + *pointer : -1}};
  if (!is_aligned_in_variadic_return_other(
          &input, _Alignof(struct aligned_variadic_return_arg32)) ||
      !is_aligned_in_variadic_return_other(
          &result, _Alignof(union aligned_variadic_return64))) {
    result.record.tag = -2;
  }
  return result;
}
