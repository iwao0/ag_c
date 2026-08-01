// Cross-TU half of the over-aligned aggregate callback ABI fixture.
#include <stdint.h>

#ifndef AG_C_OVERALIGNED_CALLBACK_VALUE_ABI_XTU_TYPES
#define AG_C_OVERALIGNED_CALLBACK_VALUE_ABI_XTU_TYPES
struct aligned_callback_value {
  _Alignas(64) unsigned long long first;
  unsigned long long second;
  int tag;
};

typedef struct aligned_callback_value aligned_callback_transform_t(
    struct aligned_callback_value value, int delta);
typedef struct aligned_callback_value aligned_callback_dispatch_t(
    aligned_callback_transform_t *transform,
    struct aligned_callback_value value, int delta);
#endif

_Static_assert(_Alignof(struct aligned_callback_value) == 64,
               "callback value alignment");
_Static_assert(sizeof(struct aligned_callback_value) == 64,
               "callback value size");

static int is_aligned_in_other(const void *pointer, uintptr_t alignment) {
  return (uintptr_t)pointer % alignment == 0;
}

struct aligned_callback_value transform_aligned_value_in_other(
    struct aligned_callback_value value, int delta) {
  if (!is_aligned_in_other(&value, _Alignof(struct aligned_callback_value))) {
    value.tag = -2001;
    return value;
  }
  value.first += (unsigned long long)delta;
  value.second += (unsigned long long)(delta * 3);
  value.tag += delta * 3;
  return value;
}

struct aligned_callback_value dispatch_aligned_value_in_other(
    aligned_callback_transform_t *transform,
    struct aligned_callback_value value, int delta) {
  if (!is_aligned_in_other(&value, _Alignof(struct aligned_callback_value))) {
    value.tag = -2002;
    return value;
  }

  struct aligned_callback_value result = transform(value, delta);
  if (!is_aligned_in_other(&result, _Alignof(struct aligned_callback_value))) {
    result.tag = -2003;
  }
  return result;
}

aligned_callback_transform_t *select_aligned_transform_in_other(void) {
  return transform_aligned_value_in_other;
}
