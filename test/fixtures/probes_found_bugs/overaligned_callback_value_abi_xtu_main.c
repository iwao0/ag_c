// Cross-TU direct and nested callback calls must preserve the ABI and storage
// alignment of aggregate values with extended alignment.
// Expected: exit=0
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

struct aligned_callback_value transform_aligned_value_in_other(
    struct aligned_callback_value value, int delta);
struct aligned_callback_value dispatch_aligned_value_in_other(
    aligned_callback_transform_t *transform,
    struct aligned_callback_value value, int delta);
aligned_callback_transform_t *select_aligned_transform_in_other(void);

static int is_aligned_in_main(const void *pointer, uintptr_t alignment) {
  return (uintptr_t)pointer % alignment == 0;
}

static struct aligned_callback_value transform_aligned_value_in_main(
    struct aligned_callback_value value, int delta) {
  if (!is_aligned_in_main(&value, _Alignof(struct aligned_callback_value))) {
    value.tag = -1001;
    return value;
  }
  value.first += (unsigned long long)delta;
  value.second += (unsigned long long)(delta * 2);
  value.tag += delta * 3;
  return value;
}

static int check_value(const struct aligned_callback_value *value,
                       unsigned long long first,
                       unsigned long long second, int tag) {
  return is_aligned_in_main(value, _Alignof(struct aligned_callback_value)) &&
         value->first == first && value->second == second &&
         value->tag == tag;
}

int main(void) {
  struct {
    unsigned char before;
    struct aligned_callback_value value;
    unsigned char after;
  } box = {.before = 0x5a,
           .value = {11, 13, 17},
           .after = 0xa5};

  if (!check_value(&box.value, 11, 13, 17) ||
      box.before != 0x5a || box.after != 0xa5) {
    return 1;
  }

  struct aligned_callback_value direct =
      transform_aligned_value_in_other(box.value, 5);
  if (!check_value(&direct, 16, 28, 32)) {
    return 2;
  }

  aligned_callback_dispatch_t *dispatch =
      dispatch_aligned_value_in_other;
  struct aligned_callback_value callback_result =
      dispatch(transform_aligned_value_in_main, box.value, 7);
  if (!check_value(&callback_result, 18, 27, 38)) {
    return 3;
  }

  aligned_callback_transform_t *selected =
      select_aligned_transform_in_other();
  struct aligned_callback_value selected_result = selected(box.value, 9);
  if (!check_value(&selected_result, 20, 40, 44)) {
    return 4;
  }

  box.value = dispatch(selected, box.value, 11);
  if (!check_value(&box.value, 22, 46, 50) ||
      box.before != 0x5a || box.after != 0xa5) {
    return 5;
  }

  return 0;
}
