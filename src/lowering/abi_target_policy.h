#ifndef AG_IR_ABI_TARGET_POLICY_H
#define AG_IR_ABI_TARGET_POLICY_H

#include <stddef.h>

struct ag_target_info_t;

typedef struct {
  size_t complex_result_piece_count;
} ir_abi_target_policy_t;

const ir_abi_target_policy_t *ir_abi_target_policy_for(
    const struct ag_target_info_t *target);

#endif
