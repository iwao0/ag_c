#ifndef SEMANTIC_INITIALIZER_RESOLUTION_H
#define SEMANTIC_INITIALIZER_RESOLUTION_H

#include "../parser/ast.h"
#include "type_identity.h"
#include "record_decl_table.h"
#include "record_layout.h"

typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;
typedef struct ag_target_info_t ag_target_info_t;
typedef struct arena_context_t arena_context_t;
typedef int (*psx_initializer_constant_index_resolver_t)(
    void *context, const node_t *expression, long long *value);
typedef int (*psx_initializer_value_type_resolver_t)(
    void *context, const node_t *expression, psx_qual_type_t *type);

typedef struct {
  int relative_offset;
  psx_qual_type_t target_qual_type;
  unsigned char bit_width;
  unsigned char bit_offset;
  unsigned char bit_is_signed;
  unsigned char is_active;
  unsigned char is_object_copy;
  unsigned char has_integer_value;
  long long integer_value;
  const node_t *value;
  int evaluation_group;
} psx_local_initializer_item_t;

typedef struct {
  psx_qual_type_t object_qual_type;
  psx_local_initializer_item_t *items;
  const node_t **evaluation_values;
  int item_count;
  int explicit_item_count;
  int evaluation_group_count;
} psx_local_initializer_plan_t;

typedef enum {
  PSX_LOCAL_INITIALIZER_OK = 0,
  PSX_LOCAL_INITIALIZER_NOT_SUPPORTED,
  PSX_LOCAL_INITIALIZER_OUT_OF_MEMORY,
} psx_local_initializer_status_t;

typedef struct {
  const psx_record_member_decl_t *declaration;
  psx_record_id_t record_id;
  int member_index;
  psx_record_member_layout_t layout;
} psx_initializer_member_ref_t;

typedef struct {
  const psx_type_t *type;
  psx_type_id_t type_id;
  int relative_offset;
  psx_initializer_member_ref_t member_ref;
  int first_array_index;
  int first_member_index;
  int union_relative_offset;
  int union_member_index;
} psx_initializer_target_t;

typedef struct {
  const psx_type_t *type;
  psx_type_id_t type_id;
  int relative_offset;
  psx_initializer_member_ref_t member_ref;
  const psx_type_t *string_array_type;
  psx_type_id_t string_array_type_id;
  int string_array_offset;
} psx_initializer_scalar_leaf_t;

typedef struct {
  psx_initializer_scalar_leaf_t *items;
  int count;
  int capacity;
} psx_initializer_scalar_leaf_list_t;

psx_local_initializer_status_t
psx_resolve_flat_local_initializer_plan(
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target,
    psx_qual_type_t object_qual_type,
    const node_init_list_t *initializer,
    psx_initializer_constant_index_resolver_t resolve_index,
    psx_initializer_value_type_resolver_t resolve_value_type,
    void *resolve_index_context,
    psx_local_initializer_plan_t *plan);

psx_initializer_target_t psx_resolve_initializer_designator_path_with_records(
    ag_diagnostic_context_t *diagnostics,
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target,
    const psx_initializer_entry_t *entry, psx_type_id_t root_type_id,
    int root_relative_offset, token_t *fallback_tok);
int psx_collect_initializer_scalar_leaves_with_records(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target, psx_type_id_t type_id,
    int relative_offset, psx_initializer_scalar_leaf_list_t *list);
int psx_initializer_flat_slot_count_with_records(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target, psx_type_id_t aggregate_type_id);
int psx_initializer_leaf_cursor_after_target_with_records(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *layout_target,
    const psx_initializer_scalar_leaf_list_t *leaves,
    const psx_initializer_target_t *target);
void psx_initializer_scalar_leaf_list_dispose(
    psx_initializer_scalar_leaf_list_t *list);

#endif
