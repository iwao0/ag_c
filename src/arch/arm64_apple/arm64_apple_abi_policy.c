#include "arm64_apple_abi_policy.h"

#include "../../lowering/abi_target_policy_internal.h"

static const ir_abi_target_policy_t policy = {
    .complex_result_piece_count = 2,
    .direct_aggregate_size_limit = 8,
    .parameter_aggregate_direct_size_limit = 16,
    .variadic_aggregate_piece_type = IR_TY_I64,
    .variadic_aggregate_piece_size = 8,
};

const ir_abi_target_policy_t *arm64_apple_abi_policy(void) {
  return &policy;
}
