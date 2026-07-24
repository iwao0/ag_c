#include "abi_target_policy_internal.h"

#include "../arch/arm64_apple/arm64_apple_abi_policy.h"
#include "../arch/wasm32/wasm32_abi_policy.h"
#include "../target_info.h"

#include <limits.h>

const ir_abi_target_policy_t *ir_abi_target_policy_for(
    const ag_target_info_t *target) {
  switch (ag_target_info_call_abi(target)) {
    case AG_TARGET_CALL_ABI_AAPCS64:
      return arm64_apple_abi_policy();
    case AG_TARGET_CALL_ABI_WASM32:
      return wasm32_abi_policy();
    case AG_TARGET_CALL_ABI_INVALID:
      return NULL;
  }
  return NULL;
}

size_t ir_abi_policy_complex_result_piece_count(
    const ir_abi_target_policy_t *policy) {
  return policy ? policy->complex_result_piece_count : 0;
}

int ir_abi_policy_direct_aggregate_type(
    const ir_abi_target_policy_t *policy, int source_size,
    ir_type_t *out_type) {
  if (out_type) *out_type = IR_TY_VOID;
  if (!policy || !out_type || source_size <= 0 ||
      source_size > policy->direct_aggregate_size_limit)
    return 0;
  switch (source_size) {
    case 1: *out_type = IR_TY_I8; return 1;
    case 2: *out_type = IR_TY_I16; return 1;
    case 4: *out_type = IR_TY_I32; return 1;
    case 8: *out_type = IR_TY_I64; return 1;
    default: return 0;
  }
}

size_t ir_abi_policy_aggregate_result_piece_count(
    const ir_abi_target_policy_t *policy, int source_size) {
  ir_type_t direct_type = IR_TY_VOID;
  if (ir_abi_policy_direct_aggregate_type(
          policy, source_size, &direct_type))
    return 1;
  if (!policy || source_size <= 0 ||
      source_size > policy->register_aggregate_result_size_limit ||
      policy->register_aggregate_result_piece_type == IR_TY_VOID ||
      policy->register_aggregate_result_piece_size <= 0)
    return 0;
  size_t size = (size_t)source_size;
  size_t piece_size =
      (size_t)policy->register_aggregate_result_piece_size;
  return size / piece_size + (size % piece_size != 0);
}

int ir_abi_policy_aggregate_result_piece(
    const ir_abi_target_policy_t *policy, int source_size,
    size_t piece_index, ir_type_t *out_type,
    int *out_byte_offset) {
  if (!policy || !out_type || !out_byte_offset) return 0;
  size_t piece_count =
      ir_abi_policy_aggregate_result_piece_count(policy, source_size);
  if (piece_index >= piece_count) return 0;

  ir_type_t direct_type = IR_TY_VOID;
  if (piece_count == 1 &&
      ir_abi_policy_direct_aggregate_type(
          policy, source_size, &direct_type)) {
    *out_type = direct_type;
    *out_byte_offset = 0;
    return 1;
  }

  size_t piece_size =
      (size_t)policy->register_aggregate_result_piece_size;
  size_t byte_offset = piece_index * piece_size;
  size_t remaining = (size_t)source_size - byte_offset;
  size_t stored_size = remaining < piece_size
                           ? remaining : piece_size;
  switch (stored_size) {
    case 1: *out_type = IR_TY_I8; break;
    case 2: *out_type = IR_TY_I16; break;
    case 4: *out_type = IR_TY_I32; break;
    default:
      *out_type = policy->register_aggregate_result_piece_type;
      break;
  }
  if (byte_offset > (size_t)INT_MAX) return 0;
  *out_byte_offset = (int)byte_offset;
  return 1;
}

int ir_abi_policy_parameter_aggregate_is_indirect(
    const ir_abi_target_policy_t *policy, int source_size) {
  return policy && source_size > 0 &&
         source_size > policy->parameter_aggregate_direct_size_limit;
}

size_t ir_abi_policy_variadic_aggregate_piece_count(
    const ir_abi_target_policy_t *policy, int source_size) {
  if (!policy || source_size <= 0 ||
      policy->variadic_aggregate_piece_size <= 0)
    return 0;
  size_t size = (size_t)source_size;
  size_t piece_size = (size_t)policy->variadic_aggregate_piece_size;
  return size / piece_size + (size % piece_size != 0);
}

int ir_abi_policy_variadic_aggregate_piece(
    const ir_abi_target_policy_t *policy, size_t piece_index,
    ir_type_t *out_type, int *out_byte_offset) {
  if (!policy || !out_type || !out_byte_offset ||
      policy->variadic_aggregate_piece_type == IR_TY_VOID ||
      policy->variadic_aggregate_piece_size <= 0 ||
      piece_index > (size_t)INT_MAX /
                        (size_t)policy->variadic_aggregate_piece_size)
    return 0;
  *out_type = policy->variadic_aggregate_piece_type;
  *out_byte_offset =
      (int)piece_index * policy->variadic_aggregate_piece_size;
  return 1;
}
