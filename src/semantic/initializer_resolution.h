#ifndef SEMANTIC_INITIALIZER_RESOLUTION_H
#define SEMANTIC_INITIALIZER_RESOLUTION_H

#include "../parser/ast.h"
#include "../parser/tag_member_public.h"

typedef struct {
  const psx_type_t *type;
  int relative_offset;
  const tag_member_info_t *direct_member;
  int first_array_index;
  int first_member_index;
  int union_relative_offset;
  int union_member_index;
} psx_initializer_target_t;

typedef struct {
  const psx_type_t *type;
  int relative_offset;
  const tag_member_info_t *direct_member;
  const psx_type_t *string_array_type;
  int string_array_offset;
} psx_initializer_scalar_leaf_t;

typedef struct {
  psx_initializer_scalar_leaf_t *items;
  int count;
  int capacity;
} psx_initializer_scalar_leaf_list_t;

psx_initializer_target_t psx_resolve_initializer_designator_path(
    const psx_initializer_entry_t *entry, const psx_type_t *root_type,
    int root_relative_offset, token_t *fallback_tok);
int psx_collect_initializer_scalar_leaves(
    const psx_type_t *type, int relative_offset,
    psx_initializer_scalar_leaf_list_t *list);
int psx_initializer_leaf_cursor_after_target(
    const psx_initializer_scalar_leaf_list_t *leaves,
    const psx_initializer_target_t *target);
void psx_initializer_scalar_leaf_list_dispose(
    psx_initializer_scalar_leaf_list_t *list);

#endif
