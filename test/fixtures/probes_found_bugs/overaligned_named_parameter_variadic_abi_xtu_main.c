// A variadic function whose last named parameter is an over-aligned
// aggregate must begin reading unnamed arguments at the same ABI position in
// both translation units.
// Expected with overaligned_named_parameter_variadic_abi_xtu_other.c: exit=0.
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

int check_overaligned_named_parameter_variadic(
    int marker, struct aligned_named_variadic32 first_named,
    union aligned_named_variadic64 last_named, ...);
typedef int overaligned_named_parameter_variadic_checker_t(
    int marker, struct aligned_named_variadic32 first_named,
    union aligned_named_variadic64 last_named, ...);

static int is_aligned_in_named_variadic_main(
    const void *pointer, uintptr_t alignment) {
  return (uintptr_t)pointer % alignment == 0;
}

int main(void) {
  struct aligned_named_variadic32 first_named = {11, 13};
  union aligned_named_variadic64 last_named = {
      .record = {17, 2.5, 19}};
  struct aligned_named_variadic32 unnamed32 = {23, 29};
  union aligned_named_variadic64 unnamed64 = {
      .record = {31, 3.5, 37}};
  int sentinel = 41;

  if (!is_aligned_in_named_variadic_main(
          &first_named, _Alignof(struct aligned_named_variadic32)) ||
      !is_aligned_in_named_variadic_main(
          &last_named, _Alignof(union aligned_named_variadic64)) ||
      !is_aligned_in_named_variadic_main(
          &unnamed32, _Alignof(struct aligned_named_variadic32)) ||
      !is_aligned_in_named_variadic_main(
          &unnamed64, _Alignof(union aligned_named_variadic64))) {
    return 1;
  }

  if (check_overaligned_named_parameter_variadic(
          91, first_named, last_named,
          43, 4.5, unnamed32, &sentinel, unnamed64, 47LL) != 42) {
    return 2;
  }

  overaligned_named_parameter_variadic_checker_t *checker =
      check_overaligned_named_parameter_variadic;
  if (checker(92, first_named, last_named,
              53, 5.5, unnamed32, &sentinel, unnamed64, 59LL) != 42) {
    return 3;
  }
  return 0;
}
