#ifndef PARSER_NODE_TYPE_PUBLIC_H
#define PARSER_NODE_TYPE_PUBLIC_H

#include "../tokenizer/token.h"
#include "node_fwd.h"

typedef struct psx_type_t psx_type_t;

psx_type_t *ps_node_get_type(node_t *node);
int ps_node_is_pointer(node_t *n);
int ps_node_deref_size(node_t *n);
int ps_node_type_size(node_t *n);
int ps_node_storage_type_size(node_t *n);
int ps_node_integer_promotion_is_unsigned(node_t *n);
int ps_node_conversion_value_is_unsigned(node_t *n);
int ps_node_i64_widen_source_is_unsigned(node_t *n);
int ps_node_shift_operation_is_unsigned(node_t *n);
tk_float_kind_t ps_node_value_fp_kind(node_t *node);
int ps_node_value_is_complex(node_t *node);
int ps_node_value_is_void(node_t *node);
int ps_node_usual_arith_operands_is_unsigned(node_t *lhs, node_t *rhs);
int ps_node_usual_arith_is_unsigned(node_t *n);
int ps_node_atomic_pointer_info(node_t *ptr_arg, int *width, int *is_unsigned);
int ps_node_cast_i64_extension_info(node_t *node, int *target_size,
                                     int *widen_zext_i64, int *needs_i64_extend);
int ps_node_bitfield_info(node_t *node, int *bit_width, int *bit_offset,
                           int *bit_is_signed);
int ps_node_value_is_pointer_like(node_t *node);
int ps_node_aggregate_value_size(node_t *node);
int ps_node_is_unsigned_type(node_t *node);
int ps_node_deref_decays_to_address(node_t *node);

#endif
