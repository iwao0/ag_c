#ifndef AG_IR_ABI_TARGET_POLICY_INTERNAL_H
#define AG_IR_ABI_TARGET_POLICY_INTERNAL_H

#include "abi_target_policy.h"

struct ir_abi_target_policy_t {
  size_t complex_result_piece_count;
  int direct_aggregate_size_limit;
  int parameter_aggregate_direct_size_limit;
  ir_type_t variadic_aggregate_piece_type;
  int variadic_aggregate_piece_size;
};

#endif
