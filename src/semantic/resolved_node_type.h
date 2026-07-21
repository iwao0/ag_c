#ifndef SEMANTIC_RESOLVED_NODE_TYPE_H
#define SEMANTIC_RESOLVED_NODE_TYPE_H

#include "../tokenizer/token.h"
#include "../type_system/type_ids.h"
#include "../type_system/type_shape.h"
#include "resolution_state_access.h"

psx_qual_type_t ps_node_qual_type(
    const psx_resolution_store_t *store, const node_t *node);
int ps_node_type_shape(
    const psx_resolution_store_t *store, const node_t *node,
    psx_type_shape_t *shape);
int ps_node_bind_qual_type(
    psx_resolution_store_t *store, node_t *node,
    psx_qual_type_t qual_type);
void ps_node_clear_type(psx_resolution_store_t *store, node_t *node);
void ps_node_clear_expr_type_state(
    psx_resolution_store_t *store, node_t *node);
void ps_node_set_bitfield_info(
    psx_resolution_store_t *store, node_t *node,
    int bit_width, int bit_offset, int bit_is_signed);
void ps_node_set_scalar_ptr_member_lvalue(
    psx_resolution_store_t *store, node_t *node, int enabled);
void ps_node_set_subscript_uses_base_address(
    psx_resolution_store_t *store, node_t *node, int enabled);
int ps_node_conversion_value_is_unsigned(
    const psx_resolution_store_t *store, node_t *n);
int ps_node_shift_operation_is_unsigned(
    const psx_resolution_store_t *store, node_t *n);
tk_float_kind_t ps_node_value_fp_kind(
    const psx_resolution_store_t *store, node_t *node);
int ps_node_value_is_complex(
    const psx_resolution_store_t *store, node_t *node);
int ps_node_value_is_void(
    const psx_resolution_store_t *store, node_t *node);
int ps_node_bitfield_info(
    const psx_resolution_store_t *store, node_t *node,
    int *bit_width, int *bit_offset, int *bit_is_signed);
int ps_node_value_is_pointer_like(
    const psx_resolution_store_t *store, node_t *node);
int ps_node_is_unsigned_type(
    const psx_resolution_store_t *store, node_t *node);
int ps_node_deref_decays_to_address(
    const psx_resolution_store_t *store, node_t *node);
#endif
