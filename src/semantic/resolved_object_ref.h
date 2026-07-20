#ifndef SEMANTIC_RESOLVED_OBJECT_REF_H
#define SEMANTIC_RESOLVED_OBJECT_REF_H

#include "../parser/core.h"
#include "../parser/syntax_node_kind.h"
#include "../parser/type.h"
#include "../type_system/type_ids.h"
#include "resolved_node_kind.h"

typedef struct arena_context_t arena_context_t;
typedef struct global_var_t global_var_t;
typedef struct lvar_t lvar_t;
typedef struct node_t node_t;
typedef struct psx_type_t psx_type_t;
typedef struct psx_semantic_type_table_t psx_semantic_type_table_t;
typedef struct psx_resolution_store_t psx_resolution_store_t;
typedef enum {
  PSX_RESOLVED_OBJECT_REF_NONE = 0,
  PSX_RESOLVED_OBJECT_REF_LOCAL,
  PSX_RESOLVED_OBJECT_REF_GLOBAL,
  PSX_RESOLVED_OBJECT_REF_FUNCTION,
  PSX_RESOLVED_OBJECT_REF_VARARG_CURSOR,
} psx_resolved_object_ref_kind_t;

int psx_bind_local_reference_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, node_t *node, lvar_t *var,
    int storage_offset, const psx_semantic_type_table_t *semantic_types,
    psx_qual_type_t qual_type);
int psx_bind_global_reference_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, node_t *node,
    global_var_t *global, char *name, int name_len,
    const psx_semantic_type_table_t *semantic_types,
    psx_qual_type_t qual_type,
    int is_thread_local);
int psx_bind_function_reference_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, node_t *node,
    char *name, int name_len,
    const psx_semantic_type_table_t *types,
    psx_qual_type_t function_qual_type);
int psx_bind_va_arg_area_reference_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, node_t *node);
psx_resolution_node_kind_t psx_resolved_object_ref_node_kind(
    const psx_resolution_store_t *store, const node_t *node);

node_t *psx_node_new_lvar_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, int offset);
node_t *ps_node_new_lvar_typed_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, int offset, int type_size);
node_t *ps_node_new_lvar_storage_slot_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, lvar_t *owner, int offset,
    int type_size);
node_t *ps_node_new_lvar_type_at_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, lvar_t *owner, int offset,
    const psx_type_t *type);
node_t *psx_node_new_lvar_scalar_slot_at_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, int offset, int type_size,
    psx_floating_kind_t floating_kind, int is_bool);
node_t *psx_node_new_lvar_fp_slot_at_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, int offset, int type_size,
    psx_floating_kind_t floating_kind);
node_t *ps_node_new_lvar_fp_slot_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types,
    lvar_t *owner, int offset, int type_size);
node_t *ps_node_new_param_placeholder_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, const psx_type_t *type);
node_t *ps_node_new_unsigned_lvar_typed_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, int offset, int type_size);
node_t *psx_node_new_lvar_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types, lvar_t *var);
node_t *psx_node_new_lvar_object_ref_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types, lvar_t *var);
node_t *ps_node_new_lvar_expr_ref_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types, lvar_t *var);
node_t *psx_node_new_lvar_identifier_ref_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types, lvar_t *var);
node_t *psx_node_new_vla_decay_ref_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types, lvar_t *var,
    psx_qual_type_t decay_qual_type);
node_t *ps_node_new_param_lvar_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types, lvar_t *var);

node_t *ps_node_new_gvar_array_addr_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types,
    global_var_t *global, psx_qual_type_t expression_qual_type);
node_t *psx_node_new_static_local_array_addr_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types,
    lvar_t *var, psx_qual_type_t expression_qual_type);
node_t *ps_node_new_lvar_array_addr_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types, lvar_t *var,
    psx_qual_type_t expression_qual_type);
node_t *ps_node_new_tag_member_lvar_ref_with_layout_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types, lvar_t *owner,
    int member_offset, psx_qual_type_t member_qual_type,
    int bit_is_signed, int bit_width, int bit_offset);
node_t *ps_node_new_gvar_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types,
    global_var_t *global);
node_t *psx_node_new_gvar_array_base_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types,
    global_var_t *global);
node_t *psx_node_new_static_local_gvar_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types, lvar_t *var);
node_t *psx_node_new_function_reference_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, char *name, int name_len,
    const psx_type_t *function_type);
node_t *psx_node_new_va_arg_area_reference_in(
    psx_resolution_store_t *store, arena_context_t *arena_context);
lvar_t *ps_node_lvar_symbol(
    const psx_resolution_store_t *store, node_t *node);
psx_resolved_object_ref_kind_t psx_resolved_object_ref_kind(
    const psx_resolution_store_t *store, const node_t *node);
lvar_t *psx_resolved_object_ref_local(
    const psx_resolution_store_t *store, const node_t *node);
global_var_t *psx_resolved_object_ref_global(
    const psx_resolution_store_t *store, const node_t *node);
int psx_resolved_object_ref_storage_offset(
    const psx_resolution_store_t *store, const node_t *node);
char *psx_resolved_object_ref_name(
    const psx_resolution_store_t *store,
    const node_t *node, int *name_len);
int psx_resolved_object_ref_is_thread_local(
    const psx_resolution_store_t *store, const node_t *node);

#endif
