#include "initializer_resolution.h"

#include "constant_expression.h"
#include "../diag/diag.h"
#include "../parser/diag.h"
#include "../parser/tag_public.h"
#include <stdlib.h>
#include <string.h>

static long long resolve_designator_index(
    node_t *expr, token_t *designator_tok, token_t *fallback_tok) {
  int ok = 1;
  long long index = psx_eval_const_int(expr, &ok);
  token_t *tok = designator_tok ? designator_tok : fallback_tok;
  if (!ok) {
    ps_diag_ctx(tok, "init", "%s",
                 diag_message_for(DIAG_ERR_PARSER_NONNEG_CONSTEXPR_REQUIRED),
                 diag_text_for(DIAG_TEXT_ARRAY_DESIGNATOR_INDEX));
  }
  if (index < 0) {
    ps_diag_ctx(tok, "init", "%s",
                 diag_message_for(DIAG_ERR_PARSER_NONNEG_VALUE_REQUIRED),
                 diag_text_for(DIAG_TEXT_ARRAY_DESIGNATOR_INDEX));
  }
  return index;
}

static int aggregate_member_index_by_name(
    const psx_aggregate_definition_t *definition,
    const psx_initializer_designator_t *designator) {
  if (!definition || !designator) return -1;
  for (int i = 0; i < definition->member_count; i++) {
    const tag_member_info_t *member = &definition->members[i];
    if (member->len == designator->member_len && member->name &&
        memcmp(member->name, designator->member_name,
               (size_t)designator->member_len) == 0)
      return i;
  }
  return -1;
}

psx_initializer_target_t psx_resolve_initializer_designator_path(
    const psx_initializer_entry_t *entry, psx_type_t *root_type,
    int root_relative_offset, token_t *fallback_tok) {
  psx_initializer_target_t target = {
      .type = root_type,
      .relative_offset = root_relative_offset,
      .first_array_index = -1,
      .first_member_index = -1,
      .union_relative_offset = -1,
      .union_member_index = -1,
  };
  if (!entry) return target;
  for (int d = 0; d < entry->designator_count; d++) {
    const psx_initializer_designator_t *designator = &entry->designators[d];
    if (designator->kind == PSX_INIT_DESIGNATOR_INDEX) {
      if (!target.type || target.type->kind != PSX_TYPE_ARRAY ||
          !target.type->base) {
        ps_diag_ctx(
            designator->tok ? designator->tok : fallback_tok,
            "init", "%s",
            diag_message_for(
                d > 0 ? DIAG_ERR_PARSER_NESTED_DESIG_NOT_ARRAY
                      : DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
      }
      long long index = resolve_designator_index(
          designator->index_expr, designator->tok, fallback_tok);
      if (index >= target.type->array_len) {
        ps_diag_ctx(designator->tok ? designator->tok : fallback_tok,
                     "init", "%s",
                     diag_message_for(
                         DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
      }
      if (target.first_array_index < 0)
        target.first_array_index = (int)index;
      target.relative_offset +=
          (int)index * ps_type_sizeof(target.type->base);
      target.type = target.type->base;
      target.direct_member = NULL;
      continue;
    }

    psx_type_t *aggregate_type = target.type;
    psx_aggregate_definition_t *definition =
        target.type ? target.type->aggregate_definition : NULL;
    int member_index = aggregate_member_index_by_name(definition, designator);
    if (member_index < 0) {
      ps_diag_ctx(designator->tok ? designator->tok : fallback_tok,
                   "init", "%s",
                   diag_message_for(
                       DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
    }
    if (target.first_member_index < 0)
      target.first_member_index = member_index;
    if (aggregate_type && aggregate_type->kind == PSX_TYPE_UNION) {
      target.union_relative_offset = target.relative_offset;
      target.union_member_index = member_index;
    }
    tag_member_info_t *member = &definition->members[member_index];
    target.relative_offset += member->offset;
    target.type = ps_tag_member_decl_type_mut(member);
    target.direct_member = member;
  }
  return target;
}

static int append_scalar_leaf(
    psx_initializer_scalar_leaf_list_t *list, psx_type_t *type,
    int relative_offset, tag_member_info_t *direct_member,
    psx_type_t *string_array_type, int string_array_offset) {
  if (list->count == list->capacity) {
    int next_capacity = list->capacity ? list->capacity * 2 : 16;
    psx_initializer_scalar_leaf_t *next = realloc(
        list->items, sizeof(*next) * (size_t)next_capacity);
    if (!next) return 0;
    list->items = next;
    list->capacity = next_capacity;
  }
  list->items[list->count++] = (psx_initializer_scalar_leaf_t){
      type, relative_offset, direct_member,
      string_array_type, string_array_offset};
  return 1;
}

int psx_collect_initializer_scalar_leaves(
    psx_type_t *type, int relative_offset,
    psx_initializer_scalar_leaf_list_t *list) {
  if (!type || !list) return 0;
  if (type->kind == PSX_TYPE_ARRAY) {
    int child_size = ps_type_sizeof(type->base);
    if (type->base && type->base->kind != PSX_TYPE_ARRAY &&
        !ps_type_is_tag_aggregate(type->base)) {
      for (int i = 0; i < type->array_len; i++) {
        if (!append_scalar_leaf(
                list, type->base, relative_offset + i * child_size,
                NULL, type, relative_offset))
          return 0;
      }
      return 1;
    }
    for (int i = 0; i < type->array_len; i++) {
      if (!psx_collect_initializer_scalar_leaves(
              type->base, relative_offset + i * child_size, list))
        return 0;
    }
    return 1;
  }
  if (ps_type_is_tag_aggregate(type)) {
    psx_aggregate_definition_t *definition = type->aggregate_definition;
    if (!definition || definition->member_count <= 0) return 0;
    int first_member = 0;
    int member_count = definition->member_count;
    if (type->kind == PSX_TYPE_UNION) {
      int max_bytes = -1;
      int max_slots = -1;
      for (int i = 0; i < definition->member_count; i++) {
        tag_member_info_t *candidate = &definition->members[i];
        int bytes = ps_tag_member_decl_storage_size(candidate);
        int slots = ps_tag_member_flat_slots(candidate);
        if (bytes > max_bytes || (bytes == max_bytes && slots > max_slots)) {
          first_member = i;
          max_bytes = bytes;
          max_slots = slots;
        }
      }
      member_count = first_member + 1;
    }
    int covered_end = -1;
    for (int i = first_member; i < member_count; i++) {
      tag_member_info_t *member = &definition->members[i];
      if (type->kind == PSX_TYPE_STRUCT && member->offset < covered_end)
        continue;
      psx_type_t *member_type = ps_tag_member_decl_type_mut(member);
      if (member_type && (member_type->kind == PSX_TYPE_ARRAY ||
                          ps_type_is_tag_aggregate(member_type))) {
        if (!psx_collect_initializer_scalar_leaves(
                member_type, relative_offset + member->offset, list))
          return 0;
      } else if (!append_scalar_leaf(
                     list, member_type, relative_offset + member->offset,
                     member, NULL, 0)) {
        return 0;
      }
      if (type->kind == PSX_TYPE_STRUCT && member->len <= 0) {
        int member_size = ps_tag_member_decl_storage_size(member);
        int end = member->offset + member_size;
        if (member_size > 0 && end > covered_end) covered_end = end;
      }
    }
    return 1;
  }
  return append_scalar_leaf(
      list, type, relative_offset, NULL, NULL, 0);
}

int psx_initializer_leaf_cursor_after_target(
    const psx_initializer_scalar_leaf_list_t *leaves,
    const psx_initializer_target_t *target) {
  if (!leaves || !target) return 0;
  if (target->direct_member && target->type &&
      target->type->kind != PSX_TYPE_ARRAY &&
      !ps_type_is_tag_aggregate(target->type)) {
    for (int i = 0; i < leaves->count; i++) {
      if (leaves->items[i].direct_member == target->direct_member &&
          leaves->items[i].relative_offset == target->relative_offset)
        return i + 1;
    }
  }
  int target_size = ps_type_sizeof(target->type);
  if (target_size <= 0) target_size = 1;
  int end_offset = target->relative_offset + target_size;
  int cursor = 0;
  while (cursor < leaves->count &&
         leaves->items[cursor].relative_offset < end_offset)
    cursor++;
  return cursor;
}

void psx_initializer_scalar_leaf_list_dispose(
    psx_initializer_scalar_leaf_list_t *list) {
  if (!list) return;
  free(list->items);
  list->items = NULL;
  list->count = 0;
  list->capacity = 0;
}
