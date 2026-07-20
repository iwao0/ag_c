#include "initializer_resolution.h"

#include "character_array_initializer.h"
#include "constant_expression.h"
#include "../diag/diag.h"
#include "../parser/arena.h"
#include "../parser/diag.h"
#include "../type_layout.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
  psx_type_id_t type_id;
  int relative_offset;
  int leaf_begin;
  int leaf_end;
  psx_initializer_member_ref_t member_ref;
} psx_initializer_object_span_t;

typedef struct {
  arena_context_t *arena_context;
  const psx_semantic_type_table_t *semantic_types;
  const psx_record_decl_table_t *record_decls;
  const psx_record_layout_table_t *record_layouts;
  const ag_target_info_t *target;
  psx_initializer_constant_index_resolver_t resolve_index;
  psx_initializer_value_type_resolver_t resolve_value_type;
  void *resolve_index_context;
  psx_initializer_scalar_leaf_list_t *leaves;
  psx_local_initializer_plan_t *plan;
} psx_flat_initializer_context_t;

static int aggregate_member_index_by_name(
    const psx_record_decl_t *record,
    const psx_initializer_designator_t *designator) {
  if (!record || !designator) return -1;
  for (int i = 0; i < record->member_count; i++) {
    const psx_record_member_decl_t *member = &record->members[i];
    if (member->len == designator->member_len && member->name &&
        memcmp(member->name, designator->member_name,
               (size_t)designator->member_len) == 0)
      return i;
  }
  return -1;
}

static int flat_initializer_leaf_count(
    const psx_flat_initializer_context_t *context,
    psx_type_id_t type_id) {
  psx_initializer_scalar_leaf_list_t leaves = {0};
  if (!psx_collect_initializer_scalar_leaves_with_records(
          context->semantic_types, context->record_decls,
          context->record_layouts, context->target, type_id, 0,
          &leaves)) {
    psx_initializer_scalar_leaf_list_dispose(&leaves);
    return 0;
  }
  int count = leaves.count;
  psx_initializer_scalar_leaf_list_dispose(&leaves);
  return count;
}

static int flat_initializer_member_offset(
    const psx_flat_initializer_context_t *context,
    const psx_type_t *aggregate_type, int member_index) {
  const psx_record_layout_t *layout = psx_record_layout_table_lookup(
      context->record_layouts, ps_type_record_id(aggregate_type),
      ag_target_info_data_layout(context->target));
  const psx_record_member_layout_t *member =
      psx_record_layout_member(layout, member_index);
  return member ? member->offset : -1;
}

static psx_initializer_member_ref_t flat_initializer_member_ref(
    const psx_flat_initializer_context_t *context,
    const psx_type_t *aggregate_type, int member_index,
    const psx_record_member_decl_t *declaration) {
  psx_initializer_member_ref_t ref = {
      .declaration = declaration,
      .record_id = ps_type_record_id(aggregate_type),
      .member_index = member_index,
  };
  const psx_record_layout_t *layout =
      context && aggregate_type
          ? psx_record_layout_table_lookup(
                context->record_layouts, ps_type_record_id(aggregate_type),
                ag_target_info_data_layout(context->target))
          : NULL;
  const psx_record_member_layout_t *member =
      psx_record_layout_member(layout, member_index);
  if (member) ref.layout = *member;
  return ref;
}

static int flat_initializer_set_item_from_leaf(
    psx_flat_initializer_context_t *context, int item_index,
    const psx_initializer_scalar_leaf_t *leaf) {
  if (!context || !context->plan || !context->plan->items ||
      !leaf || item_index < 0 ||
      item_index >= context->plan->item_count)
    return 0;
  psx_qual_type_t target_qual_type = {
      leaf->type_id, PSX_TYPE_QUALIFIER_NONE};
  context->plan->items[item_index] =
      (psx_local_initializer_item_t){
          .relative_offset = leaf->relative_offset,
          .target_qual_type = target_qual_type,
          .bit_width = leaf->member_ref.declaration &&
                               leaf->member_ref.declaration->bit_width > 0
                           ? (unsigned char)
                                 leaf->member_ref.declaration->bit_width
                           : 0,
          .bit_offset = leaf->member_ref.declaration
                            ? (unsigned char)
                                  leaf->member_ref.layout.bit_offset
                            : 0,
          .bit_is_signed = leaf->member_ref.declaration &&
                           leaf->member_ref.declaration->bit_is_signed,
          .is_active = 1,
          .evaluation_group = -1,
      };
  return target_qual_type.type_id != PSX_TYPE_ID_INVALID;
}

static int flat_initializer_activate_union_member(
    psx_flat_initializer_context_t *context,
    const psx_initializer_object_span_t *parent, int member_index,
    psx_initializer_object_span_t *child) {
  const psx_type_t *union_type = context && parent
      ? psx_semantic_type_table_lookup(
            context->semantic_types, parent->type_id)
      : NULL;
  const psx_record_decl_t *record = union_type &&
          union_type->kind == PSX_TYPE_UNION
      ? psx_record_decl_table_lookup(
            context->record_decls, ps_type_record_id(union_type))
      : NULL;
  if (!record || !child || member_index < 0 ||
      member_index >= record->member_count)
    return 0;
  psx_type_id_t member_type_id =
      psx_semantic_type_table_record_member(
          context->semantic_types, parent->type_id,
          member_index).type_id;
  int member_offset = flat_initializer_member_offset(
      context, union_type, member_index);
  psx_initializer_scalar_leaf_list_t selected = {0};
  if (member_type_id == PSX_TYPE_ID_INVALID || member_offset < 0 ||
      !psx_collect_initializer_scalar_leaves_with_records(
          context->semantic_types, context->record_decls,
          context->record_layouts, context->target,
          member_type_id, parent->relative_offset + member_offset,
          &selected) ||
      selected.count <= 0 ||
      selected.count > parent->leaf_end - parent->leaf_begin) {
    psx_initializer_scalar_leaf_list_dispose(&selected);
    return 0;
  }
  for (int i = parent->leaf_begin; i < parent->leaf_end; i++) {
    context->plan->items[i].is_active = 0;
    context->plan->items[i].evaluation_group = -1;
    context->plan->items[i].value = NULL;
    context->plan->items[i].has_integer_value = 0;
    context->plan->items[i].integer_value = 0;
  }
  for (int i = 0; i < selected.count; i++) {
    int target_index = parent->leaf_begin + i;
    context->leaves->items[target_index] = selected.items[i];
    if (!flat_initializer_set_item_from_leaf(
            context, target_index, &selected.items[i])) {
      psx_initializer_scalar_leaf_list_dispose(&selected);
      return 0;
    }
  }
  *child = (psx_initializer_object_span_t){
      .type_id = member_type_id,
      .relative_offset = parent->relative_offset + member_offset,
      .leaf_begin = parent->leaf_begin,
      .leaf_end = parent->leaf_begin + selected.count,
      .member_ref = flat_initializer_member_ref(
          context, union_type, member_index,
          &record->members[member_index]),
  };
  psx_initializer_scalar_leaf_list_dispose(&selected);
  return 1;
}

static int flat_initializer_child_span(
    psx_flat_initializer_context_t *context,
    const psx_initializer_object_span_t *parent, int child_index,
    psx_initializer_object_span_t *child) {
  if (!context || !parent || !child || child_index < 0) return 0;
  const psx_type_t *parent_type = psx_semantic_type_table_lookup(
      context->semantic_types, parent->type_id);
  if (!parent_type) return 0;
  if (parent_type->kind == PSX_TYPE_ARRAY) {
    if (child_index >= parent_type->array_len) return 0;
    psx_type_id_t child_type_id = psx_semantic_type_table_base(
        context->semantic_types, parent->type_id).type_id;
    int child_leaf_count = flat_initializer_leaf_count(
        context, child_type_id);
    int child_size = ps_type_sizeof_id(
        context->semantic_types, context->record_layouts, child_type_id,
        ag_target_info_data_layout(context->target));
    if (child_leaf_count <= 0 || child_size < 0) return 0;
    *child = (psx_initializer_object_span_t){
        .type_id = child_type_id,
        .relative_offset = parent->relative_offset +
                           child_index * child_size,
        .leaf_begin = parent->leaf_begin +
                      child_index * child_leaf_count,
        .leaf_end = parent->leaf_begin +
                    (child_index + 1) * child_leaf_count,
        .member_ref = {0},
    };
    return child->leaf_end <= parent->leaf_end;
  }
  if (parent_type->kind == PSX_TYPE_UNION)
    return flat_initializer_activate_union_member(
        context, parent, child_index, child);
  if (parent_type->kind != PSX_TYPE_STRUCT) return 0;
  const psx_record_decl_t *record = psx_record_decl_table_lookup(
      context->record_decls, ps_type_record_id(parent_type));
  if (!record || child_index >= record->member_count) return 0;
  psx_type_id_t member_type_id =
      psx_semantic_type_table_record_member(
          context->semantic_types, parent->type_id,
          child_index).type_id;
  int member_offset = flat_initializer_member_offset(
      context, parent_type, child_index);
  int member_size = ps_type_sizeof_id(
      context->semantic_types, context->record_layouts, member_type_id,
      ag_target_info_data_layout(context->target));
  int member_leaf_count = flat_initializer_leaf_count(
      context, member_type_id);
  if (member_type_id == PSX_TYPE_ID_INVALID ||
      member_offset < 0 || member_size <= 0 ||
      member_leaf_count <= 0)
    return 0;
  int member_begin = parent->relative_offset + member_offset;
  int member_end = member_begin + member_size;
  int leaf_begin = -1;
  int leaf_end = -1;
  for (int i = parent->leaf_begin; i < parent->leaf_end; i++) {
    int leaf_offset = context->leaves->items[i].relative_offset;
    if (leaf_offset < member_begin || leaf_offset >= member_end)
      continue;
    leaf_begin = i;
    leaf_end = i + member_leaf_count;
    break;
  }
  if (leaf_begin < 0 || leaf_end <= leaf_begin ||
      leaf_end > parent->leaf_end)
    return 0;
  for (int i = leaf_begin; i < leaf_end; i++) {
    int leaf_offset = context->leaves->items[i].relative_offset;
    if (leaf_offset < member_begin || leaf_offset >= member_end)
      return 0;
  }
  *child = (psx_initializer_object_span_t){
      .type_id = member_type_id,
      .relative_offset = member_begin,
      .leaf_begin = leaf_begin,
      .leaf_end = leaf_end,
      .member_ref = flat_initializer_member_ref(
          context, parent_type, child_index,
          &record->members[child_index]),
  };
  return 1;
}

static int flat_initializer_record_has_named_member(
    const psx_flat_initializer_context_t *context,
    psx_type_id_t aggregate_type_id,
    const psx_initializer_designator_t *designator) {
  const psx_type_t *type = context
      ? psx_semantic_type_table_lookup(
            context->semantic_types, aggregate_type_id)
      : NULL;
  const psx_record_decl_t *record = type && ps_type_is_tag_aggregate(type)
      ? psx_record_decl_table_lookup(
            context->record_decls, ps_type_record_id(type))
      : NULL;
  return aggregate_member_index_by_name(record, designator) >= 0;
}

static int flat_initializer_designated_member_span(
    psx_flat_initializer_context_t *context,
    const psx_initializer_object_span_t *aggregate,
    const psx_initializer_designator_t *designator,
    psx_initializer_object_span_t *target) {
  const psx_type_t *type = context && aggregate
      ? psx_semantic_type_table_lookup(
            context->semantic_types, aggregate->type_id)
      : NULL;
  const psx_record_decl_t *record = type && ps_type_is_tag_aggregate(type)
      ? psx_record_decl_table_lookup(
            context->record_decls, ps_type_record_id(type))
      : NULL;
  if (!record || !designator || !target) return 0;

  /* Promoted names designate the physical member inside the anonymous
     aggregate. Following that path is what selects the active union member. */
  for (int i = 0; i < record->member_count; i++) {
    const psx_record_member_decl_t *member = &record->members[i];
    if (!psx_record_member_decl_is_unnamed_aggregate(member)) continue;
    psx_type_id_t member_type_id = psx_semantic_type_table_record_member(
        context->semantic_types, aggregate->type_id, i).type_id;
    if (!flat_initializer_record_has_named_member(
            context, member_type_id, designator))
      continue;
    psx_initializer_object_span_t anonymous_span;
    if (!flat_initializer_child_span(
            context, aggregate, i, &anonymous_span))
      return 0;
    return flat_initializer_designated_member_span(
        context, &anonymous_span, designator, target);
  }

  int member_index = aggregate_member_index_by_name(record, designator);
  return member_index >= 0 &&
         flat_initializer_child_span(
             context, aggregate, member_index, target);
}

static int flat_initializer_child_containing_leaf(
    psx_flat_initializer_context_t *context,
    const psx_initializer_object_span_t *parent, int leaf_index,
    psx_initializer_object_span_t *child) {
  const psx_type_t *parent_type = psx_semantic_type_table_lookup(
      context->semantic_types, parent->type_id);
  if (!parent_type) return 0;
  const psx_record_decl_t *record =
      parent_type->kind == PSX_TYPE_STRUCT
          ? psx_record_decl_table_lookup(
                context->record_decls,
                ps_type_record_id(parent_type))
          : NULL;
  int child_count = parent_type->kind == PSX_TYPE_ARRAY
                        ? parent_type->array_len
                        : parent_type->kind == PSX_TYPE_STRUCT
                              ? record ? record->member_count : 0
                              : 0;
  for (int i = 0; i < child_count; i++) {
    if (!flat_initializer_child_span(context, parent, i, child))
      return 0;
    if (leaf_index >= child->leaf_begin &&
        leaf_index < child->leaf_end)
      return 1;
  }
  return 0;
}

static int flat_initializer_string_span(
    const psx_flat_initializer_context_t *context, int cursor,
    psx_initializer_object_span_t *target) {
  if (!context || !context->leaves || !target || cursor < 0 ||
      cursor >= context->leaves->count)
    return 0;
  const psx_initializer_scalar_leaf_t *leaf =
      &context->leaves->items[cursor];
  if (leaf->string_array_type_id == PSX_TYPE_ID_INVALID)
    return 0;
  int begin = cursor;
  while (begin > 0) {
    const psx_initializer_scalar_leaf_t *previous =
        &context->leaves->items[begin - 1];
    if (previous->string_array_type_id != leaf->string_array_type_id ||
        previous->string_array_offset != leaf->string_array_offset)
      break;
    begin--;
  }
  if (begin != cursor) return 0;
  int end = cursor + 1;
  while (end < context->leaves->count) {
    const psx_initializer_scalar_leaf_t *next =
        &context->leaves->items[end];
    if (next->string_array_type_id != leaf->string_array_type_id ||
        next->string_array_offset != leaf->string_array_offset)
      break;
    end++;
  }
  *target = (psx_initializer_object_span_t){
      .type_id = leaf->string_array_type_id,
      .relative_offset = leaf->string_array_offset,
      .leaf_begin = begin,
      .leaf_end = end,
      .member_ref = leaf->member_ref,
  };
  return end > begin;
}

static int flat_initializer_aggregate_value_span(
    psx_flat_initializer_context_t *context,
    const psx_initializer_object_span_t *object, int cursor,
    const node_t *value, psx_initializer_object_span_t *target) {
  if (!context || !object || !value || !target ||
      !context->resolve_value_type)
    return 0;
  psx_qual_type_t value_type;
  if (!context->resolve_value_type(
          context->resolve_index_context, value, &value_type))
    return 0;
  psx_initializer_object_span_t candidate = *object;
  for (;;) {
    const psx_type_t *candidate_type = psx_semantic_type_table_lookup(
        context->semantic_types, candidate.type_id);
    if (candidate_type &&
        (ps_type_is_tag_aggregate(candidate_type) ||
         candidate_type->kind == PSX_TYPE_ARRAY) &&
        candidate.type_id == value_type.type_id) {
      *target = candidate;
      return 1;
    }
    psx_initializer_object_span_t child;
    if (!flat_initializer_child_containing_leaf(
            context, &candidate, cursor, &child) ||
        (child.type_id == candidate.type_id &&
         child.leaf_begin == candidate.leaf_begin &&
         child.leaf_end == candidate.leaf_end))
      return 0;
    candidate = child;
  }
}

static int flat_initializer_designated_span(
    psx_flat_initializer_context_t *context,
    const psx_initializer_object_span_t *root,
    const psx_initializer_entry_t *entry,
    const unsigned char *range_overrides,
    const long long *range_indices,
    psx_initializer_object_span_t *target) {
  if (!context || !root || !entry || !target ||
      entry->designator_count <= 0)
    return 0;
  *target = *root;
  for (int i = 0; i < entry->designator_count; i++) {
    const psx_initializer_designator_t *designator =
        &entry->designators[i];
    const psx_type_t *type = psx_semantic_type_table_lookup(
        context->semantic_types, target->type_id);
    if (!type) return 0;
    int child_index = -1;
    if (designator->kind == PSX_INIT_DESIGNATOR_INDEX) {
      long long index = -1;
      if (type->kind != PSX_TYPE_ARRAY || !designator->index_expr ||
          !context->resolve_index)
        return 0;
      if (designator->is_range) {
        if (!range_overrides || !range_indices ||
            !range_overrides[i])
          return 0;
        index = range_indices[i];
      } else if (!context->resolve_index(
                     context->resolve_index_context,
                     designator->index_expr, &index)) {
        return 0;
      }
      if (index < 0 || index >= type->array_len)
        return 0;
      child_index = (int)index;
    } else if (designator->kind == PSX_INIT_DESIGNATOR_MEMBER) {
      if (type->kind != PSX_TYPE_STRUCT &&
          type->kind != PSX_TYPE_UNION)
        return 0;
      psx_initializer_object_span_t child;
      if (!flat_initializer_designated_member_span(
              context, target, designator, &child))
        return 0;
      *target = child;
      continue;
    } else {
      return 0;
    }
    psx_initializer_object_span_t child;
    if (!flat_initializer_child_span(
            context, target, child_index, &child))
      return 0;
    *target = child;
  }
  return 1;
}

static int flat_initializer_apply_value(
    psx_flat_initializer_context_t *context,
    const psx_initializer_object_span_t *target,
    const node_t *value, int range_grouped);

static int flat_initializer_entry_ranges(
    const psx_flat_initializer_context_t *context,
    const psx_initializer_entry_t *entry,
    unsigned char *range_overrides,
    long long *range_begins, long long *range_ends,
    int *range_count) {
  if (range_count) *range_count = 0;
  if (!context || !entry || !range_overrides || !range_begins ||
      !range_ends || !range_count || !context->resolve_index)
    return 0;
  for (int i = 0; i < entry->designator_count; i++) {
    const psx_initializer_designator_t *designator =
        &entry->designators[i];
    range_overrides[i] = 0;
    range_begins[i] = 0;
    range_ends[i] = 0;
    if (!designator->is_range) continue;
    if (designator->kind != PSX_INIT_DESIGNATOR_INDEX ||
        !designator->index_expr || !designator->range_end_expr)
      return 0;
    long long begin = 0;
    long long end = 0;
    if (!context->resolve_index(
            context->resolve_index_context,
            designator->index_expr, &begin) ||
        !context->resolve_index(
            context->resolve_index_context,
            designator->range_end_expr, &end) ||
        begin < 0 || end < begin)
      return 0;
    range_overrides[i] = 1;
    range_begins[i] = begin;
    range_ends[i] = end;
    (*range_count)++;
  }
  return 1;
}

static int flat_initializer_apply_designated_ranges(
    psx_flat_initializer_context_t *context,
    const psx_initializer_object_span_t *object,
    const psx_initializer_entry_t *entry,
    const unsigned char *range_overrides,
    const long long *range_begins,
    const long long *range_ends,
    long long *range_indices, int designator_index,
    psx_initializer_object_span_t *last_target) {
  if (!context || !object || !entry || !range_overrides ||
      !range_begins || !range_ends || !range_indices ||
      !last_target || designator_index < 0)
    return 0;
  if (designator_index == entry->designator_count) {
    return flat_initializer_designated_span(
               context, object, entry, range_overrides,
               range_indices, last_target) &&
           flat_initializer_apply_value(
               context, last_target, entry->value, 1);
  }
  if (!range_overrides[designator_index]) {
    return flat_initializer_apply_designated_ranges(
        context, object, entry, range_overrides,
        range_begins, range_ends, range_indices,
        designator_index + 1, last_target);
  }
  for (long long index = range_begins[designator_index];;
       index++) {
    range_indices[designator_index] = index;
    if (!flat_initializer_apply_designated_ranges(
            context, object, entry, range_overrides,
            range_begins, range_ends, range_indices,
            designator_index + 1, last_target))
      return 0;
    if (index == range_ends[designator_index]) break;
  }
  return 1;
}

static int flat_initializer_apply_list(
    psx_flat_initializer_context_t *context,
    const psx_initializer_object_span_t *object,
    const node_init_list_t *list, int range_grouped) {
  if (!context || !object || !list ||
      object->leaf_begin < 0 || object->leaf_end <= object->leaf_begin)
    return 0;
  const psx_type_t *object_type = psx_semantic_type_table_lookup(
      context->semantic_types, object->type_id);
  psx_initializer_object_span_t positional_object = *object;
  int positional_union_active = 0;
  int cursor = object->leaf_begin;
  for (int i = 0; i < list->entry_count; i++) {
    const psx_initializer_entry_t *entry = &list->entries[i];
    if (!entry->value) return 0;
    unsigned char range_overrides[8] = {0};
    long long range_begins[8] = {0};
    long long range_ends[8] = {0};
    long long range_indices[8] = {0};
    int range_count = 0;
    if (!flat_initializer_entry_ranges(
            context, entry, range_overrides,
            range_begins, range_ends, &range_count))
      return 0;
    psx_initializer_object_span_t target;
    if (entry->designator_count > 0) {
      if (range_count > 0) {
        if (!flat_initializer_apply_designated_ranges(
                context, object, entry, range_overrides,
                range_begins, range_ends, range_indices,
                0, &target))
          return 0;
        cursor = target.leaf_end;
        continue;
      }
      if (!flat_initializer_designated_span(
              context, object, entry, NULL, NULL, &target))
        return 0;
    } else {
      if (entry->has_index || entry->has_member)
        return 0;
      if (object_type && object_type->kind == PSX_TYPE_UNION &&
          !positional_union_active) {
        if (!flat_initializer_child_span(
                context, object, 0, &positional_object))
          return 0;
        cursor = positional_object.leaf_begin;
        positional_union_active = 1;
      }
      const psx_initializer_object_span_t *cursor_object =
          positional_union_active ? &positional_object : object;
      if (cursor < cursor_object->leaf_begin ||
          cursor >= cursor_object->leaf_end)
        return 0;
      if (entry->value->kind == ND_STRING &&
          flat_initializer_string_span(context, cursor, &target)) {
        /* The scalar leaf metadata identifies the innermost character row. */
      } else if (entry->value->kind != ND_INIT_LIST &&
                 flat_initializer_aggregate_value_span(
                     context, cursor_object, cursor,
                     entry->value, &target)) {
        /* A compatible aggregate expression initializes one subobject. */
      } else if (positional_union_active &&
                 entry->value->kind == ND_INIT_LIST) {
        target = positional_object;
      } else {
        target = (psx_initializer_object_span_t){
            .type_id = context->leaves->items[cursor].type_id,
            .relative_offset =
                context->leaves->items[cursor].relative_offset,
            .leaf_begin = cursor,
            .leaf_end = cursor + 1,
            .member_ref = context->leaves->items[cursor].member_ref,
        };
      }
      if (entry->value->kind == ND_INIT_LIST &&
          !positional_union_active &&
          cursor_object->leaf_end - cursor_object->leaf_begin > 1 &&
          !flat_initializer_child_containing_leaf(
              context, cursor_object, cursor, &target))
        return 0;
    }
    if (!flat_initializer_apply_value(
            context, &target, entry->value, range_grouped))
      return 0;
    cursor = target.leaf_end;
  }
  return 1;
}

static int flat_initializer_apply_value(
    psx_flat_initializer_context_t *context,
    const psx_initializer_object_span_t *target,
    const node_t *value, int range_grouped) {
  if (!context || !target || !value) return 0;
  if (value->kind == ND_INIT_LIST)
    return flat_initializer_apply_list(
        context, target, (const node_init_list_t *)value,
        range_grouped);
  if (value->kind == ND_STRING) {
    const psx_type_t *target_type = psx_semantic_type_table_lookup(
        context->semantic_types, target->type_id);
    if (target_type && target_type->kind == PSX_TYPE_ARRAY) {
      const node_string_t *string = (const node_string_t *)value;
      psx_character_array_initializer_plan_t string_plan = {0};
      psx_character_array_initializer_status_t status =
          psx_plan_character_array_string_initializer(
              context->arena_context, context->semantic_types,
              (psx_qual_type_t){
                  target->type_id, PSX_TYPE_QUALIFIER_NONE},
              string->literal_contents, string->literal_length,
              (int)string->char_width, &string_plan);
      if (status != PSX_CHARACTER_ARRAY_INITIALIZER_OK ||
          string_plan.unit_count != target->leaf_end - target->leaf_begin)
        return 0;
      for (int i = 0; i < string_plan.unit_count; i++) {
        psx_local_initializer_item_t *item =
            &context->plan->items[target->leaf_begin + i];
        item->value = NULL;
        item->has_integer_value = 1;
        item->integer_value = string_plan.units[i];
      }
      return 1;
    }
  }
  const psx_type_t *target_type = psx_semantic_type_table_lookup(
      context->semantic_types, target->type_id);
  if (target->leaf_end != target->leaf_begin + 1 ||
      ps_type_is_tag_aggregate(target_type)) {
    if (target->leaf_begin < 0 ||
        target->leaf_end > context->plan->item_count)
      return 0;
    psx_local_initializer_item_t *item =
        &context->plan->items[target->leaf_begin];
    *item = (psx_local_initializer_item_t){
        .relative_offset = target->relative_offset,
        .target_qual_type = {
            target->type_id, PSX_TYPE_QUALIFIER_NONE},
        .is_active = 1,
        .is_object_copy =
            target_type && target_type->kind == PSX_TYPE_ARRAY,
        .value = value,
        .evaluation_group = -1,
    };
    for (int i = target->leaf_begin + 1; i < target->leaf_end; i++) {
      context->plan->items[i].is_active = 0;
      context->plan->items[i].value = NULL;
      context->plan->items[i].has_integer_value = 0;
      context->plan->items[i].integer_value = 0;
    }
    return 1;
  }
  psx_local_initializer_item_t *item =
      &context->plan->items[target->leaf_begin];
  item->relative_offset = target->relative_offset;
  item->target_qual_type = (psx_qual_type_t){
      target->type_id, PSX_TYPE_QUALIFIER_NONE};
  item->bit_width = target->member_ref.declaration &&
                            target->member_ref.declaration->bit_width > 0
                        ? (unsigned char)
                              target->member_ref.declaration->bit_width
                        : 0;
  item->bit_offset = target->member_ref.declaration
                         ? (unsigned char)target->member_ref.layout.bit_offset
                         : 0;
  item->bit_is_signed = target->member_ref.declaration &&
                        target->member_ref.declaration->bit_is_signed;
  item->has_integer_value = 0;
  item->integer_value = 0;
  item->value = value;
  if (range_grouped) {
    int group = -1;
    for (int i = 0; i < context->plan->evaluation_group_count; i++) {
      if (context->plan->evaluation_values[i] == value) {
        group = i;
        break;
      }
    }
    if (group < 0) {
      group = context->plan->evaluation_group_count++;
      context->plan->evaluation_values[group] = value;
    }
    item->evaluation_group = group;
  }
  return 1;
}

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
    psx_local_initializer_plan_t *plan) {
  if (plan) *plan = (psx_local_initializer_plan_t){0};
  if (!arena_context || !semantic_types || !record_decls ||
      !record_layouts || !target || !initializer || !plan ||
      object_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return PSX_LOCAL_INITIALIZER_NOT_SUPPORTED;
  const psx_type_t *object_type = psx_semantic_type_table_lookup(
      semantic_types, object_qual_type.type_id);
  if (!object_type || initializer->entry_count < 0)
    return PSX_LOCAL_INITIALIZER_NOT_SUPPORTED;
  psx_flat_initializer_context_t context = {
      .arena_context = arena_context,
      .semantic_types = semantic_types,
      .record_decls = record_decls,
      .record_layouts = record_layouts,
      .target = target,
      .resolve_index = resolve_index,
      .resolve_value_type = resolve_value_type,
      .resolve_index_context = resolve_index_context,
      .plan = plan,
  };
  psx_initializer_scalar_leaf_list_t leaves = {0};
  if (!psx_collect_initializer_scalar_leaves_with_records(
          semantic_types, record_decls, record_layouts, target,
          object_qual_type.type_id, 0, &leaves) ||
      leaves.count <= 0) {
    psx_initializer_scalar_leaf_list_dispose(&leaves);
    return PSX_LOCAL_INITIALIZER_NOT_SUPPORTED;
  }
  psx_local_initializer_item_t *items = arena_alloc_in(
      arena_context, (size_t)leaves.count * sizeof(*items));
  const node_t **evaluation_values = arena_alloc_in(
      arena_context,
      (size_t)leaves.count * sizeof(*evaluation_values));
  if (!items || !evaluation_values) {
    psx_initializer_scalar_leaf_list_dispose(&leaves);
    return PSX_LOCAL_INITIALIZER_OUT_OF_MEMORY;
  }
  plan->object_qual_type = object_qual_type;
  plan->items = items;
  plan->evaluation_values = evaluation_values;
  plan->item_count = leaves.count;
  plan->explicit_item_count = initializer->entry_count;
  context.leaves = &leaves;
  for (int i = 0; i < leaves.count; i++) {
    const psx_initializer_scalar_leaf_t *leaf = &leaves.items[i];
    if (!flat_initializer_set_item_from_leaf(&context, i, leaf)) {
      *plan = (psx_local_initializer_plan_t){0};
      psx_initializer_scalar_leaf_list_dispose(&leaves);
      return PSX_LOCAL_INITIALIZER_NOT_SUPPORTED;
    }
  }
  psx_initializer_object_span_t root = {
      .type_id = object_qual_type.type_id,
      .relative_offset = 0,
      .leaf_begin = 0,
      .leaf_end = leaves.count,
  };
  if (!flat_initializer_apply_list(
          &context, &root, initializer, 0)) {
    *plan = (psx_local_initializer_plan_t){0};
    psx_initializer_scalar_leaf_list_dispose(&leaves);
    return PSX_LOCAL_INITIALIZER_NOT_SUPPORTED;
  }
  psx_initializer_scalar_leaf_list_dispose(&leaves);
  return PSX_LOCAL_INITIALIZER_OK;
}

static int initializer_type_size(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_layout_table_t *record_layouts,
    psx_type_id_t type_id, const ag_target_info_t *target) {
  return ps_type_sizeof_id(semantic_types, record_layouts, type_id,
                           ag_target_info_data_layout(target));
}

static const psx_record_member_layout_t *initializer_member_layout(
    const psx_record_layout_table_t *record_layouts,
    const psx_type_t *aggregate_type, const ag_target_info_t *target,
    int member_index) {
  if (!record_layouts) return NULL;
  if (!aggregate_type ||
      aggregate_type->record_id == PSX_RECORD_ID_INVALID)
    return NULL;
  const psx_record_layout_t *layout =
      psx_record_layout_table_lookup(record_layouts, aggregate_type->record_id,
                                     ag_target_info_data_layout(target));
  return psx_record_layout_member(layout, member_index);
}

static int initializer_member_offset(
    const psx_record_layout_table_t *record_layouts,
    const psx_type_t *aggregate_type, const ag_target_info_t *target,
    int member_index) {
  const psx_record_member_layout_t *layout = initializer_member_layout(
      record_layouts, aggregate_type, target, member_index);
  return layout ? layout->offset : -1;
}

static psx_initializer_member_ref_t initializer_member_ref(
    const psx_record_layout_table_t *record_layouts,
    const psx_type_t *aggregate_type, const ag_target_info_t *target,
    int member_index, const psx_record_member_decl_t *declaration) {
  psx_initializer_member_ref_t ref = {
      .declaration = declaration,
      .record_id = ps_type_record_id(aggregate_type),
      .member_index = member_index,
  };
  const psx_record_member_layout_t *layout = initializer_member_layout(
      record_layouts, aggregate_type, target, member_index);
  if (layout) ref.layout = *layout;
  return ref;
}

static int initializer_record_member_index(
    const psx_record_decl_t *record,
    const char *member_name, int member_name_len) {
  if (!record || !member_name || member_name_len <= 0) return -1;
  for (int i = 0; i < record->member_count; i++) {
    const psx_record_member_decl_t *member = &record->members[i];
    if (member->len == member_name_len && member->name &&
        memcmp(member->name, member_name, (size_t)member_name_len) == 0)
      return i;
  }
  return -1;
}

static int initializer_target_descend_member(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target,
    int member_index, psx_initializer_target_t *target_inout) {
  const psx_type_t *aggregate_type = target_inout
      ? psx_semantic_type_table_lookup(
            semantic_types, target_inout->type_id)
      : NULL;
  const psx_record_decl_t *record = aggregate_type &&
          ps_type_is_tag_aggregate(aggregate_type)
      ? psx_record_decl_table_lookup(
            record_decls, ps_type_record_id(aggregate_type))
      : NULL;
  if (!record || member_index < 0 || member_index >= record->member_count)
    return 0;
  const psx_record_member_layout_t *layout = initializer_member_layout(
      record_layouts, aggregate_type, target, member_index);
  psx_qual_type_t member_type = psx_semantic_type_table_record_member(
      semantic_types, target_inout->type_id, member_index);
  if (!layout || member_type.type_id == PSX_TYPE_ID_INVALID) return 0;
  if (aggregate_type->kind == PSX_TYPE_UNION) {
    target_inout->union_relative_offset = target_inout->relative_offset;
    target_inout->union_member_index = member_index;
  }
  const psx_record_member_decl_t *member = &record->members[member_index];
  target_inout->relative_offset += layout->offset;
  target_inout->type_id = member_type.type_id;
  target_inout->member_ref = initializer_member_ref(
      record_layouts, aggregate_type, target, member_index, member);
  psx_type_shape_t member_shape = {0};
  return psx_semantic_type_table_describe(
      semantic_types, member_type.type_id, &member_shape);
}

int psx_resolve_initializer_member_target_with_records(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target,
    const char *member_name, int member_name_len,
    psx_initializer_target_t *target_inout) {
  const psx_type_t *aggregate_type = target_inout
      ? psx_semantic_type_table_lookup(
            semantic_types, target_inout->type_id)
      : NULL;
  const psx_record_decl_t *record = aggregate_type &&
          ps_type_is_tag_aggregate(aggregate_type)
      ? psx_record_decl_table_lookup(
            record_decls, ps_type_record_id(aggregate_type))
      : NULL;
  int logical_member = initializer_record_member_index(
      record, member_name, member_name_len);
  if (logical_member < 0) return 0;
  if (target_inout->first_member_index < 0)
    target_inout->first_member_index = logical_member;

  for (int i = 0; i < record->member_count; i++) {
    const psx_record_member_decl_t *member = &record->members[i];
    if (!psx_record_member_decl_is_unnamed_aggregate(member)) continue;
    psx_qual_type_t nested_type = psx_semantic_type_table_record_member(
        semantic_types, target_inout->type_id, i);
    const psx_type_t *nested = psx_semantic_type_table_lookup(
        semantic_types, nested_type.type_id);
    const psx_record_decl_t *nested_record = nested &&
            ps_type_is_tag_aggregate(nested)
        ? psx_record_decl_table_lookup(
              record_decls, ps_type_record_id(nested))
        : NULL;
    if (initializer_record_member_index(
            nested_record, member_name, member_name_len) < 0)
      continue;
    return initializer_target_descend_member(
               semantic_types, record_decls, record_layouts, target,
               i, target_inout) &&
           psx_resolve_initializer_member_target_with_records(
               semantic_types, record_decls, record_layouts, target,
               member_name, member_name_len, target_inout);
  }
  return initializer_target_descend_member(
      semantic_types, record_decls, record_layouts, target,
      logical_member, target_inout);
}

static long long resolve_designator_index(
    const psx_resolution_store_t *store,
    ag_diagnostic_context_t *diagnostics, node_t *expr,
    token_t *designator_tok, token_t *fallback_tok) {
  int ok = 1;
  long long index = psx_eval_const_int(store, expr, &ok);
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

psx_initializer_target_t psx_resolve_initializer_designator_path_with_records(
    const psx_resolution_store_t *store,
    ag_diagnostic_context_t *diagnostics,
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *layout_target,
    const psx_initializer_entry_t *entry, psx_type_id_t root_type_id,
    int root_relative_offset, token_t *fallback_tok) {
  psx_initializer_target_t target = {
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
      psx_type_shape_t target_shape = {0};
      if (!psx_semantic_type_table_describe(
              semantic_types, target.type_id, &target_shape) ||
          target_shape.kind != PSX_TYPE_ARRAY) {
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
          store, diagnostics, designator->index_expr,
          designator->tok, fallback_tok);
      if (index >= target_shape.array_len) {
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
      target.type_id = element_type_id;
      target.member_ref = (psx_initializer_member_ref_t){0};
      continue;
    }

    if (!psx_resolve_initializer_member_target_with_records(
            semantic_types, record_decls, record_layouts, layout_target,
            designator->member_name, designator->member_len, &target)) {
      ps_diag_ctx_in(
          diagnostics,
          designator->tok ? designator->tok : fallback_tok,
          "init", "%s",
          diag_message_for_in(
              diagnostics,
              DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
    }
  }
  return target;
}

static int append_scalar_leaf(
    psx_initializer_scalar_leaf_list_t *list, psx_type_id_t type_id,
    int relative_offset, psx_initializer_member_ref_t member_ref,
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
      .type_id = type_id,
      .relative_offset = relative_offset,
      .member_ref = member_ref,
      .string_array_type_id = string_array_type_id,
      .string_array_offset = string_array_offset,
  };
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
    const ag_target_info_t *target,
    const psx_record_member_decl_t *member,
    psx_type_id_t member_type_id) {
  if (!member || psx_record_member_decl_is_unnamed_struct(member)) return 0;
  int per = 1;
  const psx_type_t *member_type = psx_record_member_decl_type(member);
  psx_type_id_t aggregate_type_id =
      psx_record_member_decl_is_tag_aggregate(member)
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
  const psx_type_t *scalar_leaf = member_type;
  while (scalar_leaf && scalar_leaf->kind == PSX_TYPE_ARRAY)
    scalar_leaf = scalar_leaf->base;
  if (scalar_leaf && scalar_leaf->kind == PSX_TYPE_COMPLEX)
    per = 2;
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
    const psx_record_member_decl_t *member = &record->members[i];
    psx_type_id_t member_type_id = psx_semantic_type_table_record_member(
        semantic_types, aggregate_type_id, i).type_id;
    int member_slots = canonical_member_flat_slot_count(
        semantic_types, record_decls, record_layouts, target,
        member, member_type_id);
    if (record->record_kind == PSX_TYPE_UNION) {
      int bytes = initializer_type_size(
          semantic_types, record_layouts, member_type_id, target);
      if (bytes > union_max_bytes ||
          (bytes == union_max_bytes && member_slots > slots)) {
        union_max_bytes = bytes;
        slots = member_slots;
      }
      continue;
    }
    if (psx_record_member_decl_is_unnamed_struct(member)) continue;
    int member_offset = initializer_member_offset(
        record_layouts, aggregate_type, target, i);
    if (covered_union_size > 0 &&
        member_offset >= covered_union_offset &&
        member_offset < covered_union_offset + covered_union_size) {
      continue;
    }
    slots += member_slots;
    if (psx_record_member_decl_is_unnamed_union(member)) {
      covered_union_offset = member_offset;
      covered_union_size = initializer_type_size(
          semantic_types, record_layouts, member_type_id, target);
    }
  }
  return slots > 0 ? slots : 1;
}

int psx_initializer_flat_slot_count_with_records(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target, psx_type_id_t aggregate_type_id) {
  if (!semantic_types || !record_decls || !record_layouts || !target ||
      aggregate_type_id == PSX_TYPE_ID_INVALID)
    return 0;
  return canonical_definition_flat_slot_count(
      semantic_types, record_decls, record_layouts, target,
      aggregate_type_id);
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
        type->base->kind != PSX_TYPE_COMPLEX &&
        !ps_type_is_tag_aggregate(type->base)) {
      int is_character_array =
          ps_type_character_code_unit_width(type->base) > 0;
      for (int i = 0; i < type->array_len; i++) {
        if (!append_scalar_leaf(
                list, child_type_id,
                relative_offset + i * child_size,
                (psx_initializer_member_ref_t){0},
                is_character_array ? type_id : PSX_TYPE_ID_INVALID,
                is_character_array ? relative_offset : 0))
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
        const psx_record_member_decl_t *candidate = &record->members[i];
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
      const psx_record_member_decl_t *member = &record->members[i];
      int member_offset = initializer_member_offset(
          record_layouts, type, target, i);
      if (type->kind == PSX_TYPE_STRUCT && member_offset < covered_end)
        continue;
      const psx_type_t *member_type = psx_record_member_decl_type(member);
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
                     list, member_type_id,
                     relative_offset + member_offset,
                     initializer_member_ref(
                         record_layouts, type, target, i, member),
                     PSX_TYPE_ID_INVALID, 0)) {
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
  if (type->kind == PSX_TYPE_COMPLEX) {
    psx_qual_type_t component = psx_semantic_type_table_base(
        semantic_types, type_id);
    const psx_type_t *component_type =
        psx_semantic_type_table_lookup(
            semantic_types, component.type_id);
    int component_size = initializer_type_size(
        semantic_types, record_layouts, component.type_id, target);
    if (!component_type || component_type->kind != PSX_TYPE_FLOAT ||
        component_size <= 0)
      return 0;
    return append_scalar_leaf(
               list, component.type_id,
               relative_offset, (psx_initializer_member_ref_t){0},
               PSX_TYPE_ID_INVALID, 0) &&
           append_scalar_leaf(
               list, component.type_id,
               relative_offset + component_size,
               (psx_initializer_member_ref_t){0},
               PSX_TYPE_ID_INVALID, 0);
  }
  return append_scalar_leaf(
      list, type_id, relative_offset,
      (psx_initializer_member_ref_t){0},
      PSX_TYPE_ID_INVALID, 0);
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
  psx_type_shape_t target_shape = {0};
  int has_target_shape = psx_semantic_type_table_describe(
      semantic_types, target->type_id, &target_shape);
  if (target->member_ref.declaration && has_target_shape &&
      target_shape.kind != PSX_TYPE_ARRAY &&
      target_shape.kind != PSX_TYPE_STRUCT &&
      target_shape.kind != PSX_TYPE_UNION) {
    for (int i = 0; i < leaves->count; i++) {
      if (leaves->items[i].member_ref.record_id ==
              target->member_ref.record_id &&
          leaves->items[i].member_ref.member_index ==
              target->member_ref.member_index &&
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
