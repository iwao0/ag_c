#include "target_info.h"

static const ag_target_info_t standard_target = {
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
};

ag_target_info_t ag_target_info_host(void) {
  return standard_target;
}

ag_target_info_t ag_target_info_wasm32(void) {
  ag_target_info_t target = standard_target;
  target.pointer_size = 4;
  target.pointer_alignment = 4;
  return target;
}

int ag_target_info_pointer_size(const ag_target_info_t *target) {
  return target && target->pointer_size > 0
             ? target->pointer_size
             : standard_target.pointer_size;
}

int ag_target_info_pointer_alignment(const ag_target_info_t *target) {
  return target && target->pointer_alignment > 0
             ? target->pointer_alignment
             : ag_target_info_pointer_size(target);
}

int ag_target_info_scalar_size(
    const ag_target_info_t *target, ag_target_scalar_kind_t kind) {
  if (kind < 0 || kind >= AG_TARGET_SCALAR_COUNT) return 0;
  if (target && target->scalar[kind].size > 0)
    return target->scalar[kind].size;
  return standard_target.scalar[kind].size;
}

int ag_target_info_scalar_alignment(
    const ag_target_info_t *target, ag_target_scalar_kind_t kind) {
  if (kind < 0 || kind >= AG_TARGET_SCALAR_COUNT) return 1;
  if (target && target->scalar[kind].alignment > 0)
    return target->scalar[kind].alignment;
  return standard_target.scalar[kind].alignment;
}

int ag_target_info_equal(
    const ag_target_info_t *lhs, const ag_target_info_t *rhs) {
  if (ag_target_info_pointer_size(lhs) != ag_target_info_pointer_size(rhs) ||
      ag_target_info_pointer_alignment(lhs) !=
          ag_target_info_pointer_alignment(rhs))
    return 0;
  for (int kind = 0; kind < AG_TARGET_SCALAR_COUNT; ++kind) {
    if (ag_target_info_scalar_size(lhs, (ag_target_scalar_kind_t)kind) !=
            ag_target_info_scalar_size(rhs, (ag_target_scalar_kind_t)kind) ||
        ag_target_info_scalar_alignment(lhs, (ag_target_scalar_kind_t)kind) !=
            ag_target_info_scalar_alignment(rhs,
                                            (ag_target_scalar_kind_t)kind))
      return 0;
  }
  return 1;
}
