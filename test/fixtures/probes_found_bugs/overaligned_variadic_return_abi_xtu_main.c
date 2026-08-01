// A cross-TU variadic function with an over-aligned aggregate result must keep
// its hidden return area independent from the variadic argument area.
// Expected with overaligned_variadic_return_abi_xtu_other.c: exit=0.
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

union aligned_variadic_return64 build_overaligned_variadic_result(
    int marker, ...);
typedef union aligned_variadic_return64
    overaligned_variadic_result_builder_t(int marker, ...);

static int is_aligned_in_variadic_return_main(
    const void *pointer, uintptr_t alignment) {
  return (uintptr_t)pointer % alignment == 0;
}

static int check_result(const union aligned_variadic_return64 *value,
                        unsigned long long first,
                        unsigned long long second, int tag) {
  return is_aligned_in_variadic_return_main(
             value, _Alignof(union aligned_variadic_return64)) &&
         value->record.first == first &&
         value->record.second == second && value->record.tag == tag;
}

int main(void) {
  struct aligned_variadic_return_arg32 input = {11, 13};
  int sentinel = 37;
  struct {
    unsigned char before;
    union aligned_variadic_return64 value;
    unsigned char after;
  } box = {.before = 0x5a, .after = 0xa5};

  box.value = build_overaligned_variadic_result(
      51, input, 7, 2.5, &sentinel);
  if (!check_result(&box.value, 18, 18, 88) ||
      box.before != 0x5a || box.after != 0xa5) {
    return 1;
  }

  overaligned_variadic_result_builder_t *builder =
      build_overaligned_variadic_result;
  union aligned_variadic_return64 indirect =
      builder(52, input, 3, 4.5, &sentinel);
  if (!check_result(&indirect, 14, 22, 89) ||
      box.before != 0x5a || box.after != 0xa5) {
    return 2;
  }
  return 0;
}
