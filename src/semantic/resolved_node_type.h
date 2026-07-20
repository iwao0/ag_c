#ifndef SEMANTIC_RESOLVED_NODE_TYPE_H
#define SEMANTIC_RESOLVED_NODE_TYPE_H

#include <stddef.h>

#include "../tokenizer/token.h"
#include "../type_system/type_ids.h"
#include "../type_system/type_shape.h"
#include "../parser/node_fwd.h"

typedef struct arena_context_t arena_context_t;
typedef struct psx_type_t psx_type_t;
typedef struct psx_node_resolution_state_t psx_node_resolution_state_t;
typedef struct psx_resolution_store_t psx_resolution_store_t;
typedef struct psx_lvar_usage_region_t psx_lvar_usage_region_t;
typedef struct lvar_t lvar_t;

const psx_type_t *ps_node_get_type(
    const psx_resolution_store_t *store, const node_t *node);
psx_qual_type_t ps_node_qual_type(
    const psx_resolution_store_t *store, const node_t *node);
int ps_node_type_shape(
    const psx_resolution_store_t *store, const node_t *node,
    psx_type_shape_t *shape);
psx_node_resolution_state_t *ps_node_resolution_state(
    psx_resolution_store_t *store, node_t *node);
const psx_node_resolution_state_t *ps_node_resolution_state_const(
    const psx_resolution_store_t *store, const node_t *node);
int ps_node_has_resolution_state(
    const psx_resolution_store_t *store, const node_t *node);
size_t psx_resolution_node_storage_size(
    const psx_resolution_store_t *store, const node_t *node);
void *psx_resolution_node_alloc_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    size_t node_size);
void ps_node_bind_type(
    psx_resolution_store_t *store, node_t *node,
    const psx_type_t *type);
int ps_node_bind_qual_type(
    psx_resolution_store_t *store, node_t *node,
    psx_qual_type_t qual_type);
int ps_node_prepare_resolution_state_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *node);
int ps_node_prepare_resolution_state_for_size_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *node, size_t node_size);
int ps_node_copy_resolution_state_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *destination, const node_t *source);
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
psx_lvar_usage_region_t *ps_node_lvar_usage_region(
    const psx_resolution_store_t *store, const node_t *node);
void ps_node_set_lvar_usage_region(
    psx_resolution_store_t *store, node_t *node,
    psx_lvar_usage_region_t *region);
lvar_t *ps_node_lvar_usage_symbol(
    const psx_resolution_store_t *store, const node_t *node);
int ps_node_records_lvar_usage(
    const psx_resolution_store_t *store, const node_t *node);
void ps_node_record_lvar_usage(
    psx_resolution_store_t *store, node_t *node, lvar_t *local);
int ps_node_lvar_usage_is_unevaluated(
    const psx_resolution_store_t *store, const node_t *node);
void ps_node_set_lvar_usage_unevaluated(
    psx_resolution_store_t *store, node_t *node, int enabled);
int psx_compound_literal_requires_addressable_storage(
    const psx_resolution_store_t *store, const node_t *node);
void psx_compound_literal_require_addressable_storage(
    psx_resolution_store_t *store, node_t *node);
int ps_node_is_decl_initializer(
    const psx_resolution_store_t *store, const node_t *node);
void ps_node_set_decl_initializer(
    psx_resolution_store_t *store, node_t *node, int enabled);
int ps_node_is_source_assignment(
    const psx_resolution_store_t *store, const node_t *node);
void ps_node_set_source_assignment(
    psx_resolution_store_t *store, node_t *node, int enabled);
int ps_node_is_source_cast(
    const psx_resolution_store_t *store, const node_t *node);
void ps_node_set_source_cast(
    psx_resolution_store_t *store, node_t *node, int enabled);
int ps_node_is_implicit_int_return(
    const psx_resolution_store_t *store, const node_t *node);
void ps_node_set_implicit_int_return(
    psx_resolution_store_t *store, node_t *node, int enabled);
int ps_node_widen_zext_i64(
    const psx_resolution_store_t *store, const node_t *node);
void ps_node_set_widen_zext_i64(
    psx_resolution_store_t *store, node_t *node, int enabled);

#endif
