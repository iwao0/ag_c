#ifndef SEMANTIC_RESOLVED_NODE_TYPE_H
#define SEMANTIC_RESOLVED_NODE_TYPE_H

#include <stddef.h>

#include "../tokenizer/token.h"
#include "../type_system/type_ids.h"
#include "../parser/node_fwd.h"

typedef struct arena_context_t arena_context_t;
typedef struct psx_type_t psx_type_t;
typedef struct psx_node_resolution_state_t psx_node_resolution_state_t;
typedef struct psx_lvar_usage_region_t psx_lvar_usage_region_t;
typedef struct lvar_t lvar_t;

const psx_type_t *ps_node_get_type(const node_t *node);
psx_qual_type_t ps_node_qual_type(const node_t *node);
psx_node_resolution_state_t *ps_node_resolution_state(node_t *node);
const psx_node_resolution_state_t *ps_node_resolution_state_const(
    const node_t *node);
int ps_node_has_resolution_state(const node_t *node);
size_t psx_resolution_node_storage_size(const node_t *node);
void *psx_resolution_node_alloc_in(
    arena_context_t *arena_context, size_t node_size);
void ps_node_bind_type(node_t *node, const psx_type_t *type);
void ps_node_bind_qual_type(
    node_t *node, const psx_type_t *canonical_type,
    psx_qual_type_t qual_type);
int ps_node_prepare_resolution_state_in(
    arena_context_t *arena_context, node_t *node);
int ps_node_prepare_resolution_state_for_size_in(
    arena_context_t *arena_context, node_t *node, size_t node_size);
int ps_node_copy_resolution_state_in(
    arena_context_t *arena_context, node_t *destination,
    const node_t *source);
void ps_node_clear_type(node_t *node);
void ps_node_clear_expr_type_state(node_t *node);
void ps_node_set_qual_type_identity(
    node_t *node, psx_qual_type_t qual_type);
void ps_node_set_bitfield_info(
    node_t *node, int bit_width, int bit_offset, int bit_is_signed);
void ps_node_set_scalar_ptr_member_lvalue(node_t *node, int enabled);
void ps_node_set_subscript_uses_base_address(node_t *node, int enabled);
int ps_node_conversion_value_is_unsigned(node_t *n);
int ps_node_shift_operation_is_unsigned(node_t *n);
tk_float_kind_t ps_node_value_fp_kind(node_t *node);
int ps_node_value_is_complex(node_t *node);
int ps_node_value_is_void(node_t *node);
int ps_node_bitfield_info(node_t *node, int *bit_width, int *bit_offset,
                           int *bit_is_signed);
int ps_node_value_is_pointer_like(node_t *node);
int ps_node_is_unsigned_type(node_t *node);
int ps_node_deref_decays_to_address(node_t *node);
psx_lvar_usage_region_t *ps_node_lvar_usage_region(
    const node_t *node);
void ps_node_set_lvar_usage_region(
    node_t *node, psx_lvar_usage_region_t *region);
lvar_t *ps_node_lvar_usage_symbol(const node_t *node);
int ps_node_records_lvar_usage(const node_t *node);
void ps_node_record_lvar_usage(node_t *node, lvar_t *local);

#endif
