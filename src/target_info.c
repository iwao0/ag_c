#include "target_info.h"

#include <stddef.h>

static const ag_target_info_t standard_target = {
    .data_layout = {
        .pointer_size = 8,
        .pointer_alignment = 8,
        .scalar = {
            [AG_TARGET_SCALAR_CHAR] = {1, 1},
            [AG_TARGET_SCALAR_SHORT] = {2, 2},
            [AG_TARGET_SCALAR_INT] = {4, 4},
            [AG_TARGET_SCALAR_LONG] = {8, 8},
            [AG_TARGET_SCALAR_LONG_LONG] = {8, 8},
            [AG_TARGET_SCALAR_FLOAT] = {4, 4},
            [AG_TARGET_SCALAR_DOUBLE] = {8, 8},
            [AG_TARGET_SCALAR_LONG_DOUBLE] = {8, 8},
            [AG_TARGET_SCALAR_FLOAT_COMPLEX] = {8, 8},
            [AG_TARGET_SCALAR_DOUBLE_COMPLEX] = {16, 8},
            [AG_TARGET_SCALAR_LONG_DOUBLE_COMPLEX] = {16, 8},
        },
    },
    .call_abi = AG_TARGET_CALL_ABI_AAPCS64,
};

ag_target_info_t ag_target_info_host(void) {
  return standard_target;
}

ag_target_info_t ag_target_info_wasm32(void) {
  ag_target_info_t target = standard_target;
  target.data_layout.pointer_size = 4;
  target.data_layout.pointer_alignment = 4;
  target.call_abi = AG_TARGET_CALL_ABI_WASM32;
  return target;
}

int ag_data_layout_is_valid(const ag_data_layout_t *layout) {
  if (!layout || layout->pointer_size <= 0 ||
      layout->pointer_alignment <= 0)
    return 0;
  for (int kind = 0; kind < AG_TARGET_SCALAR_COUNT; kind++) {
    if (layout->scalar[kind].size <= 0 ||
        layout->scalar[kind].alignment <= 0)
      return 0;
  }
  return 1;
}

int ag_target_info_is_valid(const ag_target_info_t *target) {
  return target && ag_data_layout_is_valid(&target->data_layout) &&
         (target->call_abi == AG_TARGET_CALL_ABI_AAPCS64 ||
          target->call_abi == AG_TARGET_CALL_ABI_WASM32);
}

const ag_data_layout_t *ag_target_info_data_layout(
    const ag_target_info_t *target) {
  return target && ag_data_layout_is_valid(&target->data_layout)
             ? &target->data_layout : NULL;
}

int ag_data_layout_pointer_size(const ag_data_layout_t *layout) {
  return layout && layout->pointer_size > 0 ? layout->pointer_size : 0;
}

int ag_data_layout_pointer_alignment(const ag_data_layout_t *layout) {
  return layout && layout->pointer_alignment > 0
             ? layout->pointer_alignment : 0;
}

int ag_data_layout_scalar_size(
    const ag_data_layout_t *layout, ag_target_scalar_kind_t kind) {
  if (kind < 0 || kind >= AG_TARGET_SCALAR_COUNT) return 0;
  return layout && layout->scalar[kind].size > 0
             ? layout->scalar[kind].size : 0;
}

int ag_data_layout_scalar_alignment(
    const ag_data_layout_t *layout, ag_target_scalar_kind_t kind) {
  if (kind < 0 || kind >= AG_TARGET_SCALAR_COUNT) return 0;
  return layout && layout->scalar[kind].alignment > 0
             ? layout->scalar[kind].alignment : 0;
}

int ag_data_layout_equal(
    const ag_data_layout_t *lhs, const ag_data_layout_t *rhs) {
  if (!ag_data_layout_is_valid(lhs) || !ag_data_layout_is_valid(rhs) ||
      ag_data_layout_pointer_size(lhs) !=
          ag_data_layout_pointer_size(rhs) ||
      ag_data_layout_pointer_alignment(lhs) !=
          ag_data_layout_pointer_alignment(rhs))
    return 0;
  for (int kind = 0; kind < AG_TARGET_SCALAR_COUNT; ++kind) {
    if (ag_data_layout_scalar_size(lhs, (ag_target_scalar_kind_t)kind) !=
            ag_data_layout_scalar_size(rhs,
                                       (ag_target_scalar_kind_t)kind) ||
        ag_data_layout_scalar_alignment(lhs,
                                        (ag_target_scalar_kind_t)kind) !=
            ag_data_layout_scalar_alignment(
                rhs, (ag_target_scalar_kind_t)kind))
      return 0;
  }
  return 1;
}

ag_target_call_abi_t ag_target_info_call_abi(
    const ag_target_info_t *target) {
  return target &&
                 (target->call_abi == AG_TARGET_CALL_ABI_AAPCS64 ||
                  target->call_abi == AG_TARGET_CALL_ABI_WASM32)
             ? target->call_abi : AG_TARGET_CALL_ABI_INVALID;
}

int ag_target_info_equal(
    const ag_target_info_t *lhs, const ag_target_info_t *rhs) {
  if (!ag_target_info_is_valid(lhs) || !ag_target_info_is_valid(rhs))
    return 0;
  return ag_target_info_call_abi(lhs) == ag_target_info_call_abi(rhs) &&
         ag_data_layout_equal(&lhs->data_layout, &rhs->data_layout);
}
