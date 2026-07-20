#ifndef AG_IR_ABI_TARGET_POLICY_H
#define AG_IR_ABI_TARGET_POLICY_H

#include <stddef.h>

#include "../ir/ir.h"

struct ag_target_info_t;

typedef struct ir_abi_target_policy_t ir_abi_target_policy_t;

const ir_abi_target_policy_t *ir_abi_target_policy_for(
    const struct ag_target_info_t *target);
size_t ir_abi_policy_complex_result_piece_count(
    const ir_abi_target_policy_t *policy);
int ir_abi_policy_direct_aggregate_type(
    const ir_abi_target_policy_t *policy, int source_size,
    ir_type_t *out_type);
int ir_abi_policy_parameter_aggregate_is_indirect(
    const ir_abi_target_policy_t *policy, int source_size);
size_t ir_abi_policy_variadic_aggregate_piece_count(
    const ir_abi_target_policy_t *policy, int source_size);
int ir_abi_policy_variadic_aggregate_piece(
    const ir_abi_target_policy_t *policy, size_t piece_index,
    ir_type_t *out_type, int *out_byte_offset);

#endif
