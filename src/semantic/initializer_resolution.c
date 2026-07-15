#include "initializer_resolution.h"

#include "constant_expression.h"
#include "../diag/diag.h"
#include "../parser/diag.h"
#include "../parser/tag_public.h"
#include "../type_layout.h"
#include <stdlib.h>
#include <string.h>

static int initializer_type_size(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_layout_table_t *record_layouts,
    psx_type_id_t type_id, const ag_target_info_t *target) {
  return ps_type_sizeof_id_with_records(
      semantic_types, record_layouts, type_id, target);
}

static int initializer_member_offset(
    const psx_record_layout_table_t *record_layouts,
    const psx_type_t *aggregate_type, const ag_target_info_t *target,
    int member_index, const tag_member_info_t *member) {
  if (!member) return 0;
  if (!record_layouts) return -1;
  if (!aggregate_type ||
      aggregate_type->record_id == PSX_RECORD_ID_INVALID)
    return -1;
  const psx_record_layout_t *layout = psx_record_layout_table_lookup(
      record_layouts, aggregate_type->record_id, target);
  const psx_record_member_layout_t *member_layout =
      psx_record_layout_member(layout, member_index);
  return member_layout ? member_layout->offset : -1;
}

static long long resolve_designator_index(
    ag_diagnostic_context_t *diagnostics, node_t *expr,
    token_t *designator_tok, token_t *fallback_tok) {
  int ok = 1;
  long long index = psx_eval_const_int(expr, &ok);
  token_t *tok = designator_tok ? designator_tok : fallback_tok;
  if (!ok) {
    ps_diag_ctx_in(
        diagnostics, tok, "init", "%s",
        diag_message_for_in(
            diagnostics, DIAG_ERR_PARSER_NONNEG_CONSTEXPR_REQUIRED),
        diag_text_for_in(
            diagnostics, DIAG_TEXT_ARRAY_DESIGNATOR_INDEX));
  }
  if (index < 0) {
    ps_diag_ctx_in(
        diagnostics, tok, "init", "%s",
        diag_message_for_in(
            diagnostics, DIAG_ERR_PARSER_NONNEG_VALUE_REQUIRED),
        diag_text_for_in(
            diagnostics, DIAG_TEXT_ARRAY_DESIGNATOR_INDEX));
  }
  return index;
}

static int aggregate_member_index_by_name(
    const psx_record_decl_t *record,
    const psx_initializer_designator_t *designator) {
  if (!record || !designator) return -1;
  for (int i = 0; i < record->member_count; i++) {
    const tag_member_info_t *member = &record->members[i];
    if (member->len == designator->member_len && member->name &&
        memcmp(member->name, designator->member_name,
               (size_t)designator->member_len) == 0)
      return i;
  }
  return -1;
}

psx_initializer_target_t psx_resolve_initializer_designator_path_with_records(
    ag_diagnostic_context_t *diagnostics,
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *layout_target,
    const psx_initializer_entry_t *entry, psx_type_id_t root_type_id,
    int root_relative_offset, token_t *fallback_tok) {
  psx_initializer_target_t target = {
      .type = psx_semantic_type_table_lookup(
          semantic_types, root_type_id),
      .type_id = root_type_id,
      .relative_offset = root_relative_offset,
      .first_array_index = -1,
      .first_member_index = -1,
      .union_relative_offset = -1,
      .union_member_index = -1,
  };
  if (!record_decls || !record_layouts) return target;
  if (!entry) return target;
  for (int d = 0; d < entry->designator_count; d++) {
    const psx_initializer_designator_t *designator = &entry->designators[d];
    if (designator->kind == PSX_INIT_DESIGNATOR_INDEX) {
      if (!target.type || target.type->kind != PSX_TYPE_ARRAY ||
          !target.type->base) {
        ps_diag_ctx_in(
            diagnostics,
            designator->tok ? designator->tok : fallback_tok,
            "init", "%s",
            diag_message_for_in(
                diagnostics,
                d > 0 ? DIAG_ERR_PARSER_NESTED_DESIG_NOT_ARRAY
                      : DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
      }
      long long index = resolve_designator_index(
          diagnostics, designator->index_expr,
          designator->tok, fallback_tok);
      if (index >= target.type->array_len) {
        ps_diag_ctx_in(
            diagnostics,
            designator->tok ? designator->tok : fallback_tok,
            "init", "%s",
            diag_message_for_in(
                diagnostics,
                DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
      }
      if (target.first_array_index < 0)
        target.first_array_index = (int)index;
      psx_type_id_t element_type_id = psx_semantic_type_table_base(
          semantic_types, target.type_id).type_id;
      target.relative_offset +=
          (int)index *
              initializer_type_size(
                  semantic_types, record_layouts,
                  element_type_id, layout_target);
      target.type = target.type->base;
      target.type_id = element_type_id;
      target.direct_member = NULL;
      continue;
    }

    const psx_type_t *aggregate_type = target.type;
    const psx_record_decl_t *record = psx_record_decl_table_lookup(
        record_decls, ps_type_record_id(aggregate_type));
    int member_index = aggregate_member_index_by_name(record, designator);
    if (member_index < 0) {
      ps_diag_ctx_in(
          diagnostics,
          designator->tok ? designator->tok : fallback_tok,
          "init", "%s",
          diag_message_for_in(
              diagnostics,
              DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
    }
    if (target.first_member_index < 0)
      target.first_member_index = member_index;
    if (aggregate_type && aggregate_type->kind == PSX_TYPE_UNION) {
      target.union_relative_offset = target.relative_offset;
      target.union_member_index = member_index;
    }
    const tag_member_info_t *member = &record->members[member_index];
    target.relative_offset += initializer_member_offset(
        record_layouts, aggregate_type, layout_target,
        member_index, member);
    target.type = ps_tag_member_decl_type(member);
    target.type_id = psx_semantic_type_table_record_member(
        semantic_types, target.type_id, member_index).type_id;
    target.direct_member = member;
  }
  return target;
}

static int append_scalar_leaf(
    psx_initializer_scalar_leaf_list_t *list, const psx_type_t *type,
    psx_type_id_t type_id,
    int relative_offset, const tag_member_info_t *direct_member,
    const psx_type_t *string_array_type,
    psx_type_id_t string_array_type_id, int string_array_offset) {
  if (list->count == list->capacity) {
    int next_capacity = list->capacity ? list->capacity * 2 : 16;
    psx_initializer_scalar_leaf_t *next = realloc(
        list->items, sizeof(*next) * (size_t)next_capacity);
    if (!next) return 0;
    list->items = next;
    list->capacity = next_capacity;
  }
  list->items[list->count++] = (psx_initializer_scalar_leaf_t){
      type, type_id, relative_offset, direct_member,
      string_array_type, string_array_type_id, string_array_offset};
  return 1;
}

static int canonical_definition_flat_slot_count(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target,
    psx_type_id_t aggregate_type_id);

static int canonical_member_flat_slot_count(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target, const tag_member_info_t *member,
    psx_type_id_t member_type_id) {
  if (!member || ps_tag_member_is_unnamed_struct(member)) return 0;
  int per = 1;
  const psx_type_t *member_type = ps_tag_member_decl_type(member);
  psx_type_id_t aggregate_type_id = ps_tag_member_is_tag_aggregate(member)
                                        ? psx_semantic_type_table_array_leaf(
                                              semantic_types,
                                              member_type_id).type_id
                                        : PSX_TYPE_ID_INVALID;
  const psx_type_t *aggregate_type = psx_semantic_type_table_lookup(
      semantic_types, aggregate_type_id);
  if (aggregate_type && psx_record_decl_table_lookup(
                            record_decls,
                            ps_type_record_id(aggregate_type))) {
    per = canonical_definition_flat_slot_count(
        semantic_types, record_decls, record_layouts, target,
        aggregate_type_id);
  }
  int count = ps_type_array_flat_element_count(member_type);
  return count > 0 ? count * per : per;
}

static int canonical_definition_flat_slot_count(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target,
    psx_type_id_t aggregate_type_id) {
  const psx_type_t *aggregate_type = psx_semantic_type_table_lookup(
      semantic_types, aggregate_type_id);
  const psx_record_decl_t *record = psx_record_decl_table_lookup(
      record_decls, ps_type_record_id(aggregate_type));
  if (!record || record->member_count <= 0) return 1;
  int slots = 0;
  int union_max_bytes = -1;
  int covered_union_offset = 0;
  int covered_union_size = 0;
  for (int i = 0; i < record->member_count; i++) {
    const tag_member_info_t *member = &record->members[i];
    psx_type_id_t member_type_id = psx_semantic_type_table_record_member(
        semantic_types, aggregate_type_id, i).type_id;
    int member_slots = canonical_member_flat_slot_count(
        semantic_types, record_decls, record_layouts, target,
        member, member_type_id);
    if (record->tag_kind == TK_UNION) {
      int bytes = initializer_type_size(
          semantic_types, record_layouts, member_type_id, target);
      if (bytes > union_max_bytes ||
          (bytes == union_max_bytes && member_slots > slots)) {
        union_max_bytes = bytes;
        slots = member_slots;
      }
      continue;
    }
    if (ps_tag_member_is_unnamed_struct(member)) continue;
    int member_offset = initializer_member_offset(
        record_layouts, aggregate_type, target, i, member);
    if (covered_union_size > 0 &&
        member_offset >= covered_union_offset &&
        member_offset < covered_union_offset + covered_union_size) {
      continue;
    }
    slots += member_slots;
    if (ps_tag_member_is_unnamed_union(member)) {
      covered_union_offset = member_offset;
      covered_union_size = initializer_type_size(
          semantic_types, record_layouts, member_type_id, target);
    }
  }
  return slots > 0 ? slots : 1;
}

static int collect_initializer_scalar_leaves(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target, psx_type_id_t type_id,
    int relative_offset,
    psx_initializer_scalar_leaf_list_t *list) {
  const psx_type_t *type = psx_semantic_type_table_lookup(
      semantic_types, type_id);
  if (!type || !list) return 0;
  if (type->kind == PSX_TYPE_ARRAY) {
    psx_type_id_t child_type_id = psx_semantic_type_table_base(
        semantic_types, type_id).type_id;
    int child_size = initializer_type_size(
        semantic_types, record_layouts, child_type_id, target);
    if (type->base && type->base->kind != PSX_TYPE_ARRAY &&
        !ps_type_is_tag_aggregate(type->base)) {
      for (int i = 0; i < type->array_len; i++) {
        if (!append_scalar_leaf(
                list, type->base, child_type_id,
                relative_offset + i * child_size,
                NULL, type, type_id, relative_offset))
          return 0;
      }
      return 1;
    }
    for (int i = 0; i < type->array_len; i++) {
      if (!collect_initializer_scalar_leaves(
              semantic_types, record_decls, record_layouts, target,
              child_type_id,
              relative_offset + i * child_size, list))
        return 0;
    }
    return 1;
  }
  if (ps_type_is_tag_aggregate(type)) {
    const psx_record_decl_t *record = psx_record_decl_table_lookup(
        record_decls, ps_type_record_id(type));
    if (!record || record->member_count <= 0) return 0;
    int first_member = 0;
    int member_count = record->member_count;
    if (type->kind == PSX_TYPE_UNION) {
      int max_bytes = -1;
      int max_slots = -1;
      for (int i = 0; i < record->member_count; i++) {
        const tag_member_info_t *candidate = &record->members[i];
        psx_type_id_t candidate_type_id =
            psx_semantic_type_table_record_member(
                semantic_types, type_id, i).type_id;
        int bytes = initializer_type_size(
            semantic_types, record_layouts, candidate_type_id, target);
        int slots = canonical_member_flat_slot_count(
            semantic_types, record_decls, record_layouts, target,
            candidate, candidate_type_id);
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
      const tag_member_info_t *member = &record->members[i];
      int member_offset = initializer_member_offset(
          record_layouts, type, target, i, member);
      if (type->kind == PSX_TYPE_STRUCT && member_offset < covered_end)
        continue;
      const psx_type_t *member_type = ps_tag_member_decl_type(member);
      psx_type_id_t member_type_id = psx_semantic_type_table_record_member(
          semantic_types, type_id, i).type_id;
      if (member_type && (member_type->kind == PSX_TYPE_ARRAY ||
                          ps_type_is_tag_aggregate(member_type))) {
        if (!collect_initializer_scalar_leaves(
                semantic_types, record_decls, record_layouts, target,
                member_type_id,
                relative_offset + member_offset, list))
          return 0;
      } else if (!append_scalar_leaf(
                     list, member_type, member_type_id,
                     relative_offset + member_offset,
                     member, NULL, PSX_TYPE_ID_INVALID, 0)) {
        return 0;
      }
      if (type->kind == PSX_TYPE_STRUCT && member->len <= 0) {
        int member_size = initializer_type_size(
            semantic_types, record_layouts, member_type_id, target);
        int end = member_offset + member_size;
        if (member_size > 0 && end > covered_end) covered_end = end;
      }
    }
    return 1;
  }
  return append_scalar_leaf(
      list, type, type_id, relative_offset, NULL,
      NULL, PSX_TYPE_ID_INVALID, 0);
}

int psx_collect_initializer_scalar_leaves_with_records(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target, psx_type_id_t type_id,
    int relative_offset, psx_initializer_scalar_leaf_list_t *list) {
  if (!record_decls || !record_layouts) return 0;
  return collect_initializer_scalar_leaves(
      semantic_types, record_decls, record_layouts, target, type_id,
      relative_offset, list);
}

int psx_initializer_leaf_cursor_after_target_with_records(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *layout_target,
  const psx_initializer_scalar_leaf_list_t *leaves,
  const psx_initializer_target_t *target) {
  if (!record_layouts || !leaves || !target) return 0;
  if (target->direct_member && target->type &&
      target->type->kind != PSX_TYPE_ARRAY &&
      !ps_type_is_tag_aggregate(target->type)) {
    for (int i = 0; i < leaves->count; i++) {
      if (leaves->items[i].direct_member == target->direct_member &&
          leaves->items[i].relative_offset == target->relative_offset)
        return i + 1;
    }
  }
  int target_size = initializer_type_size(
      semantic_types, record_layouts, target->type_id, layout_target);
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
