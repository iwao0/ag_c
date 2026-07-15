#ifndef SEMANTIC_INITIALIZER_RESOLUTION_H
#define SEMANTIC_INITIALIZER_RESOLUTION_H

#include "../parser/ast.h"
#include "../parser/tag_member_public.h"
#include "type_identity.h"

typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;
typedef struct ag_target_info_t ag_target_info_t;

typedef struct {
  const psx_type_t *type;
  psx_type_id_t type_id;
  int relative_offset;
  const tag_member_info_t *direct_member;
  int first_array_index;
  int first_member_index;
  int union_relative_offset;
  int union_member_index;
} psx_initializer_target_t;

typedef struct {
  const psx_type_t *type;
  psx_type_id_t type_id;
  int relative_offset;
  const tag_member_info_t *direct_member;
  const psx_type_t *string_array_type;
  psx_type_id_t string_array_type_id;
  int string_array_offset;
} psx_initializer_scalar_leaf_t;

typedef struct {
  psx_initializer_scalar_leaf_t *items;
  int count;
  int capacity;
} psx_initializer_scalar_leaf_list_t;

psx_initializer_target_t psx_resolve_initializer_designator_path(
    ag_diagnostic_context_t *diagnostics,
    const psx_semantic_type_table_t *semantic_types,
    const ag_target_info_t *target,
    const psx_initializer_entry_t *entry, psx_type_id_t root_type_id,
    int root_relative_offset, token_t *fallback_tok);
int psx_collect_initializer_scalar_leaves(
    const psx_semantic_type_table_t *semantic_types,
    const ag_target_info_t *target, psx_type_id_t type_id,
    int relative_offset,
    psx_initializer_scalar_leaf_list_t *list);
int psx_initializer_leaf_cursor_after_target(
    const psx_semantic_type_table_t *semantic_types,
    const ag_target_info_t *layout_target,
    const psx_initializer_scalar_leaf_list_t *leaves,
    const psx_initializer_target_t *target);
void psx_initializer_scalar_leaf_list_dispose(
    psx_initializer_scalar_leaf_list_t *list);

#endif
