// Cross-TU variadic calls must preserve over-aligned aggregate values and
// advance the argument area using the same ABI layout in both translation
// units.
// Expected with overaligned_variadic_value_abi_xtu_other.c: exit=0.
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

int check_overaligned_variadic_values(int marker, ...);
typedef int overaligned_variadic_checker_t(int marker, ...);

static int is_aligned_in_variadic_main(const void *pointer,
                                       uintptr_t alignment) {
  return (uintptr_t)pointer % alignment == 0;
}

int main(void) {
  struct aligned_variadic32 first = {11, 13};
  float _Complex narrow = 1.5f + 2.5f * I;
  double _Complex wide = 3.25 + 4.75 * I;
  union aligned_variadic64 middle = {
      .record = {17, 19, 23}};
  struct aligned_variadic32 last = {29, 31};
  int sentinel = 37;

  if (!is_aligned_in_variadic_main(
          &first, _Alignof(struct aligned_variadic32)) ||
      !is_aligned_in_variadic_main(
          &middle, _Alignof(union aligned_variadic64)) ||
      !is_aligned_in_variadic_main(
          &last, _Alignof(struct aligned_variadic32))) {
    return 1;
  }

  if (check_overaligned_variadic_values(
          91, 7, narrow, first, 2.5, wide,
          middle, &sentinel, last) != 42) {
    return 2;
  }

  overaligned_variadic_checker_t *checker =
      check_overaligned_variadic_values;
  if (checker(92, 9, narrow, first, 3.5, wide,
              middle, &sentinel, last) != 42) {
    return 3;
  }
  return 0;
}
