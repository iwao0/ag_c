#include "initializer_resolution.h"

#include "character_array_initializer.h"
#include "../diag/diag.h"
#include "../parser/arena.h"
#include "../parser/diag.h"
#include "../type_layout.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  psx_qual_type_t qual_type;
  int relative_offset;
  int leaf_begin;
  int leaf_end;
  psx_initializer_member_ref_t member_ref;
  int union_relative_offset;
  int union_member_index;
} psx_initializer_object_span_t;

typedef struct {
  psx_type_id_t union_type_id;
  int relative_offset;
  int leaf_begin;
  int leaf_end;
  int member_index;
} psx_initializer_union_activation_t;

typedef struct {
  unsigned char is_range;
  long long begin;
  long long end;
  long long index;
} psx_initializer_designator_range_t;

typedef struct {
  arena_context_t *arena_context;
  const psx_semantic_type_table_t *semantic_types;
  const psx_record_decl_table_t *record_decls;
  const psx_record_layout_table_t *record_layouts;
  const ag_data_layout_t *data_layout;
  psx_initializer_constant_index_resolver_t resolve_index;
  psx_initializer_value_type_resolver_t resolve_value_type;
  psx_record_member_name_lookup_t resolve_member;
  void *resolver_context;
  psx_initializer_scalar_leaf_list_t *leaves;
  psx_local_initializer_plan_t *plan;
  psx_initializer_union_activation_t *union_activations;
  int union_activation_count;
  int union_activation_capacity;
  psx_local_initializer_status_t failure_status;
} psx_flat_initializer_context_t;

static const psx_record_member_layout_t *initializer_member_layout(
    const psx_record_layout_table_t *record_layouts,
    psx_record_id_t record_id, const ag_data_layout_t *data_layout,
    int member_index) {
  if (!record_layouts || record_id == PSX_RECORD_ID_INVALID) return NULL;
  const psx_record_layout_t *layout = psx_record_layout_table_lookup(
      record_layouts, record_id, data_layout);
  return psx_record_layout_member(layout, member_index);
}

static int initializer_member_offset(
    const psx_record_layout_table_t *record_layouts,
    psx_record_id_t record_id, const ag_data_layout_t *data_layout,
    int member_index) {
  const psx_record_member_layout_t *layout = initializer_member_layout(
      record_layouts, record_id, data_layout, member_index);
  return layout ? layout->offset : -1;
}

static psx_initializer_member_ref_t initializer_member_ref(
    const psx_record_layout_table_t *record_layouts,
    psx_record_id_t record_id, const ag_data_layout_t *data_layout,
    int member_index, const psx_record_member_decl_t *declaration) {
  psx_initializer_member_ref_t ref = {
      .declaration = declaration,
      .record_id = record_id,
      .member_index = member_index,
  };
  const psx_record_member_layout_t *layout = initializer_member_layout(
      record_layouts, record_id, data_layout, member_index);
  if (layout) ref.layout = *layout;
  return ref;
}

static int flat_initializer_leaf_count(
    const psx_flat_initializer_context_t *context,
    psx_qual_type_t qual_type) {
  psx_initializer_scalar_leaf_list_t leaves = {0};
  if (!psx_collect_initializer_scalar_leaves_with_records(
          context->semantic_types, context->record_decls,
          context->record_layouts, context->data_layout,
          qual_type, 0, &leaves)) {
    psx_initializer_scalar_leaf_list_dispose(&leaves);
    return 0;
  }
  int count = leaves.count;
  psx_initializer_scalar_leaf_list_dispose(&leaves);
  return count;
}

static int flat_initializer_set_item_from_leaf(
    psx_flat_initializer_context_t *context, int item_index,
    const psx_initializer_scalar_leaf_t *leaf) {
  if (!context || !context->plan || !context->plan->items ||
      !leaf || item_index < 0 ||
      item_index >= context->plan->item_count)
    return 0;
  psx_qual_type_t target_qual_type = leaf->qual_type;
  context->plan->items[item_index] =
      (psx_local_initializer_item_t){
          .relative_offset = leaf->relative_offset,
          .target_qual_type = target_qual_type,
          .union_relative_offset = -1,
          .union_member_index = -1,
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

static int flat_initializer_union_activation_matches(
    const psx_initializer_union_activation_t *activation,
    const psx_initializer_object_span_t *parent) {
  return activation && parent &&
         activation->union_type_id == parent->qual_type.type_id &&
         activation->relative_offset == parent->relative_offset &&
         activation->leaf_begin == parent->leaf_begin;
}

static psx_initializer_union_activation_t *
flat_initializer_find_union_activation(
    psx_flat_initializer_context_t *context,
    const psx_initializer_object_span_t *parent) {
  if (!context || !parent) return NULL;
  for (int i = 0; i < context->union_activation_count; i++) {
    psx_initializer_union_activation_t *activation =
        &context->union_activations[i];
    if (flat_initializer_union_activation_matches(
            activation, parent))
      return activation;
  }
  return NULL;
}

static psx_initializer_union_activation_t *
flat_initializer_innermost_union_activation_at_cursor(
    psx_flat_initializer_context_t *context, int cursor) {
  if (!context || cursor < 0) return NULL;
  psx_initializer_union_activation_t *best = NULL;
  int best_span = INT_MAX;
  for (int i = 0; i < context->union_activation_count; i++) {
    psx_initializer_union_activation_t *activation =
        &context->union_activations[i];
    int span = activation->leaf_end - activation->leaf_begin;
    if (cursor < activation->leaf_begin ||
        cursor >= activation->leaf_end || span <= 0 ||
        span > best_span)
      continue;
    best = activation;
    best_span = span;
  }
  return best;
}

static int flat_initializer_reserve_union_activation(
    psx_flat_initializer_context_t *context) {
  if (!context) return 0;
  if (context->union_activation_count <
      context->union_activation_capacity)
    return 1;
  if (context->union_activation_capacity > INT_MAX / 2) {
    context->failure_status =
        PSX_LOCAL_INITIALIZER_OUT_OF_MEMORY;
    return 0;
  }
  int next_capacity = context->union_activation_capacity > 0
                          ? context->union_activation_capacity * 2
                          : 8;
  psx_initializer_union_activation_t *next = arena_alloc_in(
      context->arena_context,
      (size_t)next_capacity * sizeof(*next));
  if (!next) {
    context->failure_status =
        PSX_LOCAL_INITIALIZER_OUT_OF_MEMORY;
    return 0;
  }
  if (context->union_activation_count > 0) {
    memcpy(next, context->union_activations,
           (size_t)context->union_activation_count * sizeof(*next));
  }
  context->union_activations = next;
  context->union_activation_capacity = next_capacity;
  return 1;
}

static void flat_initializer_clear_nested_union_activations(
    psx_flat_initializer_context_t *context,
    const psx_initializer_object_span_t *parent) {
  if (!context || !parent) return;
  int parent_index = -1;
  for (int i = 0; i < context->union_activation_count; i++) {
    if (flat_initializer_union_activation_matches(
            &context->union_activations[i], parent)) {
      parent_index = i;
      break;
    }
  }
  if (parent_index < 0) return;
  /*
   * Activations are appended while descending from containing unions to
   * nested unions. Same-offset anonymous unions can have identical leaf
   * spans, so span containment alone must not remove an earlier ancestor.
   */
  int write = 0;
  for (int read = 0; read < context->union_activation_count;
       read++) {
    psx_initializer_union_activation_t activation =
        context->union_activations[read];
    int is_parent = flat_initializer_union_activation_matches(
        &activation, parent);
    int is_nested = !is_parent && read > parent_index &&
                    activation.leaf_begin >= parent->leaf_begin &&
                    activation.leaf_begin < parent->leaf_end;
    if (is_nested) continue;
    context->union_activations[write++] = activation;
  }
  context->union_activation_count = write;
}

static int flat_initializer_union_activation_selects_target(
    const psx_flat_initializer_context_t *context,
    const psx_initializer_union_activation_t *activation,
    const psx_initializer_object_span_t *target) {
  psx_type_shape_t union_shape = {0};
  if (!context || !activation || !target ||
      !psx_semantic_type_table_describe(
          context->semantic_types, activation->union_type_id,
          &union_shape) ||
      union_shape.kind != PSX_TYPE_UNION)
    return 0;
  psx_qual_type_t member_type =
      psx_semantic_type_table_record_member(
          context->semantic_types, activation->union_type_id,
          activation->member_index);
  int member_offset = initializer_member_offset(
      context->record_layouts, union_shape.record_id,
      context->data_layout, activation->member_index);
  return member_type.type_id == target->qual_type.type_id &&
         member_offset >= 0 &&
         activation->relative_offset + member_offset ==
             target->relative_offset &&
         activation->leaf_begin == target->leaf_begin &&
         activation->leaf_end == target->leaf_end;
}

static int flat_initializer_reset_aggregate_target(
    psx_flat_initializer_context_t *context,
    const psx_initializer_object_span_t *target) {
  if (!context || !target) return 0;
  int containing_index = -1;
  for (int i = 0; i < context->union_activation_count; i++) {
    if (flat_initializer_union_activation_selects_target(
            context, &context->union_activations[i], target)) {
      containing_index = i;
      break;
    }
  }
  psx_initializer_union_activation_t containing = {0};
  if (containing_index >= 0)
    containing = context->union_activations[containing_index];
  psx_initializer_scalar_leaf_list_t replacement = {0};
  if (!psx_collect_initializer_scalar_leaves_with_records(
          context->semantic_types, context->record_decls,
          context->record_layouts, context->data_layout,
          target->qual_type, target->relative_offset,
          &replacement) ||
      replacement.count != target->leaf_end - target->leaf_begin) {
    psx_initializer_scalar_leaf_list_dispose(&replacement);
    return 0;
  }
  int write = 0;
  for (int read = 0; read < context->union_activation_count;
       read++) {
    psx_initializer_union_activation_t activation =
        context->union_activations[read];
    int is_containing = containing_index >= 0 &&
        activation.union_type_id == containing.union_type_id &&
        activation.relative_offset == containing.relative_offset &&
        activation.leaf_begin == containing.leaf_begin;
    int is_nested = !is_containing &&
                    activation.leaf_begin >= target->leaf_begin &&
                    activation.leaf_begin < target->leaf_end;
    /* Preserve earlier containing activations when spans start together. */
    if (containing_index >= 0)
      is_nested = is_nested && read > containing_index;
    if (is_nested) continue;
    context->union_activations[write++] = activation;
  }
  context->union_activation_count = write;
  for (int i = 0; i < replacement.count; i++) {
    int target_index = target->leaf_begin + i;
    context->leaves->items[target_index] = replacement.items[i];
    if (!flat_initializer_set_item_from_leaf(
            context, target_index, &replacement.items[i])) {
      psx_initializer_scalar_leaf_list_dispose(&replacement);
      return 0;
    }
  }
  psx_initializer_scalar_leaf_list_dispose(&replacement);
  return 1;
}

static int flat_initializer_activate_union_member(
    psx_flat_initializer_context_t *context,
    const psx_initializer_object_span_t *parent, int member_index,
    psx_initializer_object_span_t *child) {
  psx_type_shape_t union_shape = {0};
  const psx_record_decl_t *record =
      context && parent && psx_semantic_type_table_describe(
          context->semantic_types, parent->qual_type.type_id,
          &union_shape) &&
      union_shape.kind == PSX_TYPE_UNION
          ? psx_record_decl_table_lookup(
                context->record_decls, union_shape.record_id)
          : NULL;
  if (!record || !child || member_index < 0 ||
      member_index >= record->member_count)
    return 0;
  psx_qual_type_t member_type = psx_semantic_type_table_record_member(
      context->semantic_types, parent->qual_type.type_id, member_index);
  int member_offset = initializer_member_offset(
      context->record_layouts, union_shape.record_id,
      context->data_layout, member_index);
  psx_initializer_union_activation_t *activation =
      flat_initializer_find_union_activation(context, parent);
  if (activation && activation->member_index == member_index) {
    if (member_type.type_id == PSX_TYPE_ID_INVALID ||
        member_offset < 0 ||
        activation->leaf_end <= parent->leaf_begin ||
        activation->leaf_end > parent->leaf_end)
      return 0;
    *child = (psx_initializer_object_span_t){
        .qual_type = member_type,
        .relative_offset = parent->relative_offset + member_offset,
        .leaf_begin = parent->leaf_begin,
        .leaf_end = activation->leaf_end,
        .member_ref = initializer_member_ref(
            context->record_layouts, union_shape.record_id,
            context->data_layout, member_index,
            &record->members[member_index]),
        .union_relative_offset = parent->relative_offset,
        .union_member_index = member_index,
    };
    return 1;
  }
  psx_initializer_scalar_leaf_list_t selected = {0};
  if (member_type.type_id == PSX_TYPE_ID_INVALID || member_offset < 0 ||
      !psx_collect_initializer_scalar_leaves_with_records(
          context->semantic_types, context->record_decls,
          context->record_layouts, context->data_layout, member_type,
          parent->relative_offset + member_offset, &selected) ||
      selected.count <= 0 ||
      selected.count > parent->leaf_end - parent->leaf_begin) {
    psx_initializer_scalar_leaf_list_dispose(&selected);
    return 0;
  }
  if (activation) {
    flat_initializer_clear_nested_union_activations(
        context, parent);
    activation = flat_initializer_find_union_activation(
        context, parent);
  } else if (!flat_initializer_reserve_union_activation(context)) {
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
  if (!activation) {
    activation = &context->union_activations[
        context->union_activation_count++];
  }
  *activation = (psx_initializer_union_activation_t){
      .union_type_id = parent->qual_type.type_id,
      .relative_offset = parent->relative_offset,
      .leaf_begin = parent->leaf_begin,
      .leaf_end = parent->leaf_begin + selected.count,
      .member_index = member_index,
  };
  *child = (psx_initializer_object_span_t){
      .qual_type = member_type,
      .relative_offset = parent->relative_offset + member_offset,
      .leaf_begin = parent->leaf_begin,
      .leaf_end = parent->leaf_begin + selected.count,
      .member_ref = initializer_member_ref(
          context->record_layouts, union_shape.record_id,
          context->data_layout, member_index,
          &record->members[member_index]),
      .union_relative_offset = parent->relative_offset,
      .union_member_index = member_index,
  };
  psx_initializer_scalar_leaf_list_dispose(&selected);
  return 1;
}

static int flat_initializer_child_span(
    psx_flat_initializer_context_t *context,
    const psx_initializer_object_span_t *parent, int child_index,
    psx_initializer_object_span_t *child) {
  if (!context || !parent || !child || child_index < 0) return 0;
  psx_type_shape_t parent_shape = {0};
  if (!psx_semantic_type_table_describe(
          context->semantic_types, parent->qual_type.type_id,
          &parent_shape))
    return 0;
  if (parent_shape.kind == PSX_TYPE_ARRAY) {
    if (child_index >= parent_shape.array_len) return 0;
    psx_qual_type_t child_type = psx_semantic_type_table_base(
        context->semantic_types, parent->qual_type.type_id);
    int child_leaf_count = flat_initializer_leaf_count(
        context, child_type);
    int child_size = psx_qual_type_layout_sizeof(
        context->semantic_types, context->record_layouts,
        child_type, context->data_layout);
    if (child_leaf_count <= 0 || child_size < 0) return 0;
    *child = (psx_initializer_object_span_t){
        .qual_type = child_type,
        .relative_offset = parent->relative_offset +
                           child_index * child_size,
        .leaf_begin = parent->leaf_begin +
                      child_index * child_leaf_count,
        .leaf_end = parent->leaf_begin +
                    (child_index + 1) * child_leaf_count,
        .member_ref = {0},
        .union_relative_offset = parent->union_relative_offset,
        .union_member_index = parent->union_member_index,
    };
    return child->leaf_end <= parent->leaf_end;
  }
  if (parent_shape.kind == PSX_TYPE_UNION)
    return flat_initializer_activate_union_member(
        context, parent, child_index, child);
  if (parent_shape.kind != PSX_TYPE_STRUCT) return 0;
  const psx_record_decl_t *record = psx_record_decl_table_lookup(
      context->record_decls, parent_shape.record_id);
  if (!record || child_index >= record->member_count) return 0;
  psx_qual_type_t member_type =
      psx_semantic_type_table_record_member(
          context->semantic_types, parent->qual_type.type_id,
          child_index);
  int member_offset = initializer_member_offset(
      context->record_layouts, parent_shape.record_id,
      context->data_layout, child_index);
  int member_size = psx_qual_type_layout_sizeof(
      context->semantic_types, context->record_layouts,
      member_type, context->data_layout);
  int member_leaf_count = flat_initializer_leaf_count(
      context, member_type);
  if (member_type.type_id == PSX_TYPE_ID_INVALID ||
      member_offset < 0 || member_size <= 0 ||
      member_leaf_count <= 0)
    return 0;
  int member_begin = parent->relative_offset + member_offset;
  int member_end = member_begin + member_size;
  int leaf_begin = -1;
  int leaf_end = -1;
  for (int i = parent->leaf_begin; i < parent->leaf_end; i++) {
    const psx_initializer_scalar_leaf_t *leaf =
        &context->leaves->items[i];
    int leaf_offset = leaf->relative_offset;
    if (leaf_offset < member_begin || leaf_offset >= member_end)
      continue;
    if (record->members[child_index].bit_width > 0 &&
        (leaf->member_ref.record_id != parent_shape.record_id ||
         leaf->member_ref.member_index != child_index))
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
      .qual_type = member_type,
      .relative_offset = member_begin,
      .leaf_begin = leaf_begin,
      .leaf_end = leaf_end,
      .member_ref = initializer_member_ref(
          context->record_layouts, parent_shape.record_id,
          context->data_layout, child_index,
          &record->members[child_index]),
      .union_relative_offset = parent->union_relative_offset,
      .union_member_index = parent->union_member_index,
  };
  return 1;
}

static int flat_initializer_record_has_named_member(
    const psx_flat_initializer_context_t *context,
    psx_type_id_t aggregate_type_id,
    const psx_initializer_designator_t *designator) {
  psx_type_shape_t shape = {0};
  int member_index = -1;
  int has_record =
      context && context->resolve_member &&
      psx_semantic_type_table_describe(
          context->semantic_types, aggregate_type_id, &shape) &&
      psx_type_kind_is_aggregate(shape.kind);
  return has_record && designator && context->resolve_member(
      context->resolver_context, shape.record_id,
      designator->member_name, designator->member_len,
      &member_index) && member_index >= 0;
}

static int flat_initializer_designated_member_span(
    psx_flat_initializer_context_t *context,
    const psx_initializer_object_span_t *aggregate,
    const psx_initializer_designator_t *designator,
    psx_initializer_object_span_t *target) {
  psx_type_shape_t shape = {0};
  const psx_record_decl_t *record =
      context && aggregate && psx_semantic_type_table_describe(
          context->semantic_types, aggregate->qual_type.type_id,
          &shape) &&
      psx_type_kind_is_aggregate(shape.kind)
          ? psx_record_decl_table_lookup(
                context->record_decls, shape.record_id)
          : NULL;
  if (!record || !designator || !target) return 0;

  /* Promoted names designate the physical member inside the anonymous
     aggregate. Following that path is what selects the active union member. */
  for (int i = 0; i < record->member_count; i++) {
    const psx_record_member_decl_t *member = &record->members[i];
    if (!psx_record_member_decl_is_unnamed_aggregate(
            context->semantic_types, member))
      continue;
    psx_type_id_t member_type_id = psx_semantic_type_table_record_member(
        context->semantic_types, aggregate->qual_type.type_id, i).type_id;
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

  int member_index = -1;
  if (!context->resolve_member ||
      !context->resolve_member(
          context->resolver_context, shape.record_id,
          designator->member_name, designator->member_len,
          &member_index))
    return 0;
  return member_index >= 0 &&
         flat_initializer_child_span(
             context, aggregate, member_index, target);
}

static int flat_initializer_child_containing_leaf(
    psx_flat_initializer_context_t *context,
    const psx_initializer_object_span_t *parent, int leaf_index,
    psx_initializer_object_span_t *child) {
  psx_type_shape_t parent_shape = {0};
  if (!psx_semantic_type_table_describe(
          context->semantic_types, parent->qual_type.type_id,
          &parent_shape))
    return 0;
  const psx_record_decl_t *record =
      parent_shape.kind == PSX_TYPE_STRUCT
          ? psx_record_decl_table_lookup(
                context->record_decls, parent_shape.record_id)
          : NULL;
  int child_count = parent_shape.kind == PSX_TYPE_ARRAY
                        ? parent_shape.array_len
                        : parent_shape.kind == PSX_TYPE_STRUCT
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

static int flat_initializer_activate_positional_unions_at_cursor(
    psx_flat_initializer_context_t *context,
    const psx_initializer_object_span_t *object, int cursor) {
  if (!context || !object || cursor < object->leaf_begin ||
      cursor >= object->leaf_end)
    return 0;
  psx_initializer_object_span_t candidate = *object;
  for (;;) {
    psx_type_shape_t shape = {0};
    if (!psx_semantic_type_table_describe(
            context->semantic_types, candidate.qual_type.type_id,
            &shape))
      return 0;
    psx_initializer_object_span_t child;
    if (shape.kind == PSX_TYPE_UNION) {
      psx_initializer_union_activation_t *activation =
          flat_initializer_find_union_activation(context, &candidate);
      int member_index = activation ? activation->member_index : 0;
      if (!flat_initializer_child_span(
              context, &candidate, member_index, &child))
        return 0;
    } else if (shape.kind == PSX_TYPE_ARRAY ||
               shape.kind == PSX_TYPE_STRUCT) {
      if (!flat_initializer_child_containing_leaf(
              context, &candidate, cursor, &child))
        return 0;
    } else {
      return 1;
    }
    if (cursor < child.leaf_begin || cursor >= child.leaf_end)
      return 0;
    if (child.qual_type.type_id == candidate.qual_type.type_id &&
        child.leaf_begin == candidate.leaf_begin &&
        child.leaf_end == candidate.leaf_end)
      return 0;
    candidate = child;
  }
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
      .qual_type = {leaf->string_array_type_id,
                    PSX_TYPE_QUALIFIER_NONE},
      .relative_offset = leaf->string_array_offset,
      .leaf_begin = begin,
      .leaf_end = end,
      .member_ref = leaf->member_ref,
      .union_relative_offset = -1,
      .union_member_index = -1,
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
          context->resolver_context, value, &value_type))
    return 0;
  psx_initializer_object_span_t candidate = *object;
  for (;;) {
    psx_type_shape_t candidate_shape = {0};
    if (psx_semantic_type_table_describe(
            context->semantic_types, candidate.qual_type.type_id,
            &candidate_shape) &&
        (psx_type_kind_is_aggregate(candidate_shape.kind) ||
         candidate_shape.kind == PSX_TYPE_ARRAY) &&
        candidate.qual_type.type_id == value_type.type_id) {
      *target = candidate;
      return 1;
    }
    psx_initializer_object_span_t child;
    if (!flat_initializer_child_containing_leaf(
            context, &candidate, cursor, &child) ||
        (child.qual_type.type_id == candidate.qual_type.type_id &&
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
    const psx_initializer_designator_range_t *ranges,
    psx_initializer_object_span_t *target) {
  if (!context || !root || !entry || !target ||
      entry->designator_count <= 0)
    return 0;
  *target = *root;
  for (int i = 0; i < entry->designator_count; i++) {
    const psx_initializer_designator_t *designator =
        &entry->designators[i];
    psx_type_shape_t shape = {0};
    if (!psx_semantic_type_table_describe(
            context->semantic_types, target->qual_type.type_id,
            &shape))
      return 0;
    int child_index = -1;
    if (designator->kind == PSX_INIT_DESIGNATOR_INDEX) {
      long long index = -1;
      if (shape.kind != PSX_TYPE_ARRAY) {
        if (i > 0)
          context->failure_status =
              PSX_LOCAL_INITIALIZER_NESTED_DESIGNATOR_NOT_ARRAY;
        return 0;
      }
      if (!designator->index_expr || !context->resolve_index)
        return 0;
      if (designator->is_range) {
        if (!ranges || !ranges[i].is_range)
          return 0;
        index = ranges[i].index;
      } else if (!context->resolve_index(
                     context->resolver_context,
                     designator->index_expr, &index)) {
        return 0;
      }
      if (index < 0 || index >= shape.array_len)
        return 0;
      child_index = (int)index;
    } else if (designator->kind == PSX_INIT_DESIGNATOR_MEMBER) {
      if (!psx_type_kind_is_aggregate(shape.kind))
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

static int flat_initializer_next_active_cursor(
    const psx_flat_initializer_context_t *context,
    const psx_initializer_object_span_t *object, int cursor) {
  if (!context || !context->plan || !context->plan->items || !object)
    return cursor;
  while (cursor >= object->leaf_begin && cursor < object->leaf_end &&
         !context->plan->items[cursor].is_active)
    cursor++;
  return cursor;
}

static int flat_initializer_relocate_whole_object_value(
    psx_flat_initializer_context_t *context, int item_index) {
  if (!context || !context->plan || item_index < 0 ||
      item_index >= context->plan->item_count)
    return 0;
  psx_local_initializer_item_t *item =
      &context->plan->items[item_index];
  if (!item->is_active || !item->is_whole_object_value)
    return 1;
  if (item->whole_leaf_begin < 0 ||
      item->whole_leaf_begin > item_index ||
      item->whole_leaf_end <= item_index ||
      item->whole_leaf_end > context->plan->item_count)
    return 0;
  psx_local_initializer_item_t whole = *item;
  for (int i = whole.whole_leaf_begin;
       i < whole.whole_leaf_end; i++) {
    if (i == item_index || context->plan->items[i].is_active)
      continue;
    context->plan->items[i] = whole;
    *item = (psx_local_initializer_item_t){0};
    return 1;
  }
  *item = (psx_local_initializer_item_t){0};
  return 1;
}

static int flat_initializer_entry_ranges(
    const psx_flat_initializer_context_t *context,
    const psx_initializer_entry_t *entry,
    psx_initializer_designator_range_t *ranges) {
  if (!context || !entry || !ranges || !context->resolve_index)
    return 0;
  for (int i = 0; i < entry->designator_count; i++) {
    const psx_initializer_designator_t *designator =
        &entry->designators[i];
    ranges[i] = (psx_initializer_designator_range_t){0};
    if (!designator->is_range) continue;
    if (designator->kind != PSX_INIT_DESIGNATOR_INDEX ||
        !designator->index_expr || !designator->range_end_expr)
      return 0;
    long long begin = 0;
    long long end = 0;
    if (!context->resolve_index(
            context->resolver_context,
            designator->index_expr, &begin) ||
        !context->resolve_index(
            context->resolver_context,
            designator->range_end_expr, &end) ||
        begin < 0 || end < begin)
      return 0;
    ranges[i] = (psx_initializer_designator_range_t){
        .is_range = 1,
        .begin = begin,
        .end = end,
        .index = begin,
    };
  }
  return 1;
}

static int flat_initializer_apply_designated_ranges(
    psx_flat_initializer_context_t *context,
    const psx_initializer_object_span_t *object,
    const psx_initializer_entry_t *entry,
    psx_initializer_designator_range_t *ranges,
    int designator_index,
    psx_initializer_object_span_t *last_target) {
  if (!context || !object || !entry || !ranges || !last_target ||
      designator_index < 0)
    return 0;
  if (designator_index == entry->designator_count) {
    return flat_initializer_designated_span(
               context, object, entry, ranges, last_target) &&
           flat_initializer_apply_value(
               context, last_target, entry->value, 1);
  }
  if (!ranges[designator_index].is_range) {
    return flat_initializer_apply_designated_ranges(
        context, object, entry, ranges, designator_index + 1,
        last_target);
  }
  for (long long index = ranges[designator_index].begin;;
       index++) {
    ranges[designator_index].index = index;
    if (!flat_initializer_apply_designated_ranges(
            context, object, entry, ranges,
            designator_index + 1, last_target))
      return 0;
    if (index == ranges[designator_index].end) break;
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
  psx_type_shape_t object_shape = {0};
  if (!psx_semantic_type_table_describe(
          context->semantic_types, object->qual_type.type_id,
          &object_shape))
    return 0;
  psx_initializer_object_span_t positional_object = *object;
  int positional_union_active = 0;
  int cursor = object->leaf_begin;
  for (int i = 0; i < list->entry_count; i++) {
    const psx_initializer_entry_t *entry = &list->entries[i];
    if (!entry->value) return 0;
    if (object_shape.kind == PSX_TYPE_UNION &&
        entry->designator_count == 0 && positional_union_active) {
      psx_type_shape_t positional_shape = {0};
      int positional_aggregate_active =
          psx_semantic_type_table_describe(
              context->semantic_types,
              positional_object.qual_type.type_id,
              &positional_shape) &&
          (positional_shape.kind == PSX_TYPE_ARRAY ||
           psx_type_kind_is_aggregate(positional_shape.kind));
      if (!positional_aggregate_active) {
        context->failure_status =
            PSX_LOCAL_INITIALIZER_UNION_TOO_MANY_ELEMENTS;
        return 0;
      }
    }
    int range_count = 0;
    for (int d = 0; d < entry->designator_count; d++) {
      if (entry->designators[d].is_range) range_count++;
    }
    psx_initializer_designator_range_t *ranges = NULL;
    if (range_count > 0) {
      if ((size_t)entry->designator_count >
          SIZE_MAX / sizeof(*ranges)) {
        context->failure_status =
            PSX_LOCAL_INITIALIZER_OUT_OF_MEMORY;
        return 0;
      }
      ranges = arena_alloc_in(
          context->arena_context,
          (size_t)entry->designator_count * sizeof(*ranges));
      if (!ranges) {
        context->failure_status =
            PSX_LOCAL_INITIALIZER_OUT_OF_MEMORY;
        return 0;
      }
      if (!flat_initializer_entry_ranges(context, entry, ranges))
        return 0;
    }
    psx_initializer_object_span_t target;
    if (entry->designator_count > 0) {
      if (range_count > 0) {
        if (!flat_initializer_apply_designated_ranges(
                context, object, entry, ranges, 0, &target))
          return 0;
        cursor = target.leaf_end;
        continue;
      }
      if (!flat_initializer_designated_span(
              context, object, entry, ranges, &target))
        return 0;
      if (object_shape.kind == PSX_TYPE_UNION) {
        psx_initializer_union_activation_t *activation =
            flat_initializer_find_union_activation(context, object);
        if (!activation ||
            !flat_initializer_child_span(
                context, object, activation->member_index,
                &positional_object))
          return 0;
        positional_union_active = 1;
      }
    } else {
      if (entry->has_index || entry->has_member)
        return 0;
      if (object_shape.kind == PSX_TYPE_UNION &&
          !positional_union_active) {
        if (!flat_initializer_child_span(
                context, object, 0, &positional_object))
          return 0;
        cursor = positional_object.leaf_begin;
        positional_union_active = 1;
      }
      const psx_initializer_object_span_t *cursor_object =
          positional_union_active ? &positional_object : object;
      cursor = flat_initializer_next_active_cursor(
          context, cursor_object, cursor);
      if (cursor < cursor_object->leaf_begin ||
          cursor >= cursor_object->leaf_end) {
        if (object_shape.kind == PSX_TYPE_UNION)
          context->failure_status =
              PSX_LOCAL_INITIALIZER_UNION_TOO_MANY_ELEMENTS;
        return 0;
      }
      if (entry->value->kind == ND_STRING &&
          flat_initializer_activate_positional_unions_at_cursor(
              context, cursor_object, cursor) &&
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
        if (!flat_initializer_activate_positional_unions_at_cursor(
                context, cursor_object, cursor))
          return 0;
        target = (psx_initializer_object_span_t){
            .qual_type = context->leaves->items[cursor].qual_type,
            .relative_offset =
                context->leaves->items[cursor].relative_offset,
            .leaf_begin = cursor,
            .leaf_end = cursor + 1,
            .member_ref = context->leaves->items[cursor].member_ref,
            .union_relative_offset = cursor_object->union_relative_offset,
            .union_member_index = cursor_object->union_member_index,
        };
      }
      if (entry->value->kind == ND_INIT_LIST &&
          !positional_union_active &&
          cursor_object->leaf_end - cursor_object->leaf_begin > 1 &&
          !flat_initializer_child_containing_leaf(
              context, cursor_object, cursor, &target))
        return 0;
      if (target.union_member_index < 0) {
        psx_initializer_union_activation_t *activation =
            flat_initializer_innermost_union_activation_at_cursor(
                context, target.leaf_begin);
        if (activation) {
          target.union_relative_offset = activation->relative_offset;
          target.union_member_index = activation->member_index;
        } else if (cursor_object->union_member_index >= 0) {
          target.union_relative_offset =
              cursor_object->union_relative_offset;
          target.union_member_index = cursor_object->union_member_index;
        }
      }
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
  if (value->kind == ND_INIT_LIST) {
    psx_type_shape_t target_shape = {0};
    if (psx_semantic_type_table_describe(
            context->semantic_types, target->qual_type.type_id,
            &target_shape) &&
        (target_shape.kind == PSX_TYPE_ARRAY ||
         psx_type_kind_is_aggregate(target_shape.kind)) &&
        !flat_initializer_reset_aggregate_target(
            context, target))
      return 0;
    return flat_initializer_apply_list(
        context, target, (const node_init_list_t *)value,
        range_grouped);
  }
  if (value->kind == ND_STRING) {
    psx_type_shape_t target_shape = {0};
    if (psx_semantic_type_table_describe(
            context->semantic_types, target->qual_type.type_id,
            &target_shape) &&
        target_shape.kind == PSX_TYPE_ARRAY) {
      const node_string_t *string = (const node_string_t *)value;
      psx_character_array_initializer_plan_t string_plan = {0};
      psx_character_array_initializer_status_t status =
          psx_plan_character_array_string_initializer(
              context->arena_context, context->semantic_types,
              context->data_layout,
              target->qual_type,
              string->literal_contents, string->literal_length,
              (int)string->char_width, &string_plan);
      if (status != PSX_CHARACTER_ARRAY_INITIALIZER_OK ||
          string_plan.unit_count != target->leaf_end - target->leaf_begin)
        return 0;
      for (int i = 0; i < string_plan.unit_count; i++) {
        psx_local_initializer_item_t *item =
            &context->plan->items[target->leaf_begin + i];
        item->union_relative_offset = target->union_relative_offset;
        item->union_member_index = target->union_member_index;
        item->value = NULL;
        item->has_integer_value = 1;
        item->integer_value = string_plan.units[i];
      }
      return 1;
    }
  }
  psx_type_shape_t target_shape = {0};
  if (!psx_semantic_type_table_describe(
          context->semantic_types, target->qual_type.type_id,
          &target_shape))
    return 0;
  if (target->leaf_end != target->leaf_begin + 1 ||
      psx_type_kind_is_aggregate(target_shape.kind)) {
    if (target->leaf_begin < 0 ||
        target->leaf_end > context->plan->item_count)
      return 0;
    psx_local_initializer_item_t *item =
        &context->plan->items[target->leaf_begin];
    *item = (psx_local_initializer_item_t){
        .relative_offset = target->relative_offset,
        .target_qual_type = target->qual_type,
        .union_relative_offset = target->union_relative_offset,
        .union_member_index = target->union_member_index,
        .is_active = 1,
        .is_object_copy = target_shape.kind == PSX_TYPE_ARRAY,
        .is_whole_object_value = 1,
        .value = value,
        .evaluation_group = -1,
        .whole_leaf_begin = target->leaf_begin,
        .whole_leaf_end = target->leaf_end,
    };
    for (int i = target->leaf_begin + 1; i < target->leaf_end; i++) {
      context->plan->items[i].is_active = 0;
      context->plan->items[i].value = NULL;
      context->plan->items[i].has_integer_value = 0;
      context->plan->items[i].integer_value = 0;
    }
    return 1;
  }
  if (!flat_initializer_relocate_whole_object_value(
          context, target->leaf_begin))
    return 0;
  psx_local_initializer_item_t *item =
      &context->plan->items[target->leaf_begin];
  item->relative_offset = target->relative_offset;
  item->target_qual_type = target->qual_type;
  item->union_relative_offset = target->union_relative_offset;
  item->union_member_index = target->union_member_index;
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
  item->is_active = 1;
  item->is_object_copy = 0;
  item->is_whole_object_value = 0;
  item->has_integer_value = 0;
  item->integer_value = 0;
  item->value = value;
  item->evaluation_group = -1;
  item->whole_leaf_begin = 0;
  item->whole_leaf_end = 0;
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

psx_local_initializer_status_t psx_resolve_flat_local_initializer_plan(
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_data_layout_t *data_layout, psx_qual_type_t object_qual_type,
    const node_init_list_t *initializer,
    psx_record_member_name_lookup_t resolve_member,
    psx_initializer_constant_index_resolver_t resolve_index,
    psx_initializer_value_type_resolver_t resolve_value_type,
    void *resolver_context, psx_local_initializer_plan_t *plan) {
  if (plan) *plan = (psx_local_initializer_plan_t){0};
  if (!arena_context || !semantic_types || !record_decls || !record_layouts ||
      !ag_data_layout_is_valid(data_layout) || !initializer || !plan ||
      object_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return PSX_LOCAL_INITIALIZER_NOT_SUPPORTED;
  psx_type_shape_t object_shape = {0};
  if (!psx_semantic_type_table_describe(
          semantic_types, object_qual_type.type_id, &object_shape) ||
      initializer->entry_count < 0)
    return PSX_LOCAL_INITIALIZER_NOT_SUPPORTED;
  psx_flat_initializer_context_t context = {
      .arena_context = arena_context,
      .semantic_types = semantic_types,
      .record_decls = record_decls,
      .record_layouts = record_layouts,
      .data_layout = data_layout,
      .resolve_member = resolve_member,
      .resolve_index = resolve_index,
      .resolve_value_type = resolve_value_type,
      .resolver_context = resolver_context,
      .plan = plan,
  };
  psx_initializer_scalar_leaf_list_t leaves = {0};
  if (!psx_collect_initializer_scalar_leaves_with_records(
          semantic_types, record_decls, record_layouts, data_layout,
          object_qual_type, 0, &leaves) ||
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
      .qual_type = object_qual_type,
      .relative_offset = 0,
      .leaf_begin = 0,
      .leaf_end = leaves.count,
      .union_relative_offset = -1,
      .union_member_index = -1,
  };
  if (!flat_initializer_apply_list(
          &context, &root, initializer, 0)) {
    psx_local_initializer_status_t failure_status =
        context.failure_status;
    *plan = (psx_local_initializer_plan_t){0};
    psx_initializer_scalar_leaf_list_dispose(&leaves);
    return failure_status != PSX_LOCAL_INITIALIZER_OK
               ? failure_status
               : PSX_LOCAL_INITIALIZER_NOT_SUPPORTED;
  }
  psx_initializer_scalar_leaf_list_dispose(&leaves);
  return PSX_LOCAL_INITIALIZER_OK;
}

static int
initializer_type_size(const psx_semantic_type_table_t *semantic_types,
                      const psx_record_layout_table_t *record_layouts,
                      psx_type_id_t type_id,
                      const ag_data_layout_t *data_layout) {
  return psx_type_layout_sizeof(semantic_types, record_layouts, type_id,
                           data_layout);
}

static int initializer_qual_type_size(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_layout_table_t *record_layouts,
    psx_qual_type_t qual_type,
    const ag_data_layout_t *data_layout) {
  return psx_qual_type_layout_sizeof(
      semantic_types, record_layouts, qual_type, data_layout);
}

static psx_type_qualifiers_t inherited_subobject_qualifiers(
    psx_type_qualifiers_t qualifiers) {
  return qualifiers &
         (PSX_TYPE_QUALIFIER_CONST | PSX_TYPE_QUALIFIER_VOLATILE);
}

static int append_scalar_leaf(
    psx_initializer_scalar_leaf_list_t *list, psx_qual_type_t qual_type,
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
      .qual_type = qual_type,
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
    const ag_data_layout_t *data_layout, psx_type_id_t aggregate_type_id);

static int canonical_member_flat_slot_count(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_data_layout_t *data_layout, const psx_record_member_decl_t *member,
    psx_type_id_t member_type_id) {
  if (!member) return 0;
  int per = 1;
  psx_qual_type_t leaf = psx_semantic_type_table_array_leaf(
      semantic_types, member_type_id);
  psx_type_shape_t leaf_shape = {0};
  int has_leaf_shape = leaf.type_id != PSX_TYPE_ID_INVALID &&
      psx_semantic_type_table_describe(
          semantic_types, leaf.type_id, &leaf_shape);
  psx_type_id_t aggregate_type_id =
      has_leaf_shape && psx_type_kind_is_aggregate(leaf_shape.kind)
          ? leaf.type_id
          : PSX_TYPE_ID_INVALID;
  if (aggregate_type_id != PSX_TYPE_ID_INVALID &&
      psx_record_decl_table_lookup(record_decls, leaf_shape.record_id)) {
    per = canonical_definition_flat_slot_count(semantic_types, record_decls,
                                               record_layouts, data_layout,
                                               aggregate_type_id);
  }
  if (has_leaf_shape && leaf_shape.kind == PSX_TYPE_COMPLEX) per = 2;
  int count = psx_semantic_type_table_array_flat_element_count(
      semantic_types, member_type_id);
  return count > 0 ? count * per : per;
}

static int canonical_definition_flat_slot_count(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_data_layout_t *data_layout, psx_type_id_t aggregate_type_id) {
  psx_type_shape_t aggregate_shape = {0};
  if (!psx_semantic_type_table_describe(
          semantic_types, aggregate_type_id, &aggregate_shape) ||
      !psx_type_kind_is_aggregate(aggregate_shape.kind))
    return 1;
  const psx_record_decl_t *record = psx_record_decl_table_lookup(
      record_decls, aggregate_shape.record_id);
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
        semantic_types, record_decls, record_layouts, data_layout, member,
        member_type_id);
    if (record->record_kind == PSX_TYPE_UNION) {
      int bytes = initializer_type_size(semantic_types, record_layouts,
                                        member_type_id, data_layout);
      if (member_slots > slots ||
          (member_slots == slots && bytes > union_max_bytes)) {
        union_max_bytes = bytes;
        slots = member_slots;
      }
      continue;
    }
    if (psx_record_member_decl_is_unnamed_struct(semantic_types, member))
      continue;
    int member_offset = initializer_member_offset(
        record_layouts, aggregate_shape.record_id, data_layout, i);
    if (covered_union_size > 0 &&
        member_offset >= covered_union_offset &&
        member_offset < covered_union_offset + covered_union_size) {
      continue;
    }
    slots += member_slots;
    if (psx_record_member_decl_is_unnamed_union(semantic_types, member)) {
      covered_union_offset = member_offset;
      covered_union_size = initializer_type_size(semantic_types, record_layouts,
                                                 member_type_id, data_layout);
    }
  }
  return slots > 0 ? slots : 1;
}

int psx_initializer_flat_slot_count_with_records(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_data_layout_t *data_layout, psx_type_id_t aggregate_type_id) {
  if (!semantic_types || !record_decls || !record_layouts ||
      !ag_data_layout_is_valid(data_layout) ||
      aggregate_type_id == PSX_TYPE_ID_INVALID)
    return 0;
  return canonical_definition_flat_slot_count(semantic_types, record_decls,
                                              record_layouts, data_layout,
                                              aggregate_type_id);
}

static int collect_initializer_scalar_leaves(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_data_layout_t *data_layout, psx_qual_type_t qual_type,
    int relative_offset, psx_initializer_scalar_leaf_list_t *list) {
  psx_type_shape_t shape = {0};
  if (!list || !psx_semantic_type_table_describe(
          semantic_types, qual_type.type_id, &shape))
    return 0;
  if (shape.kind == PSX_TYPE_ARRAY) {
    psx_qual_type_t child_type = psx_semantic_type_table_base(
        semantic_types, qual_type.type_id);
    child_type.qualifiers |= inherited_subobject_qualifiers(
        qual_type.qualifiers);
    psx_type_shape_t child_shape = {0};
    int child_size = initializer_qual_type_size(
        semantic_types, record_layouts, child_type, data_layout);
    if (!psx_semantic_type_table_describe(
            semantic_types, child_type.type_id, &child_shape))
      return 0;
    if (child_shape.kind != PSX_TYPE_ARRAY &&
        child_shape.kind != PSX_TYPE_COMPLEX &&
        !psx_type_kind_is_aggregate(child_shape.kind)) {
      int is_character_array =
          psx_type_layout_character_code_unit_width(
              semantic_types, child_type.type_id, data_layout) > 0;
      for (int i = 0; i < shape.array_len; i++) {
        if (!append_scalar_leaf(
                list, child_type,
                relative_offset + i * child_size,
                (psx_initializer_member_ref_t){0},
                is_character_array ? qual_type.type_id
                                   : PSX_TYPE_ID_INVALID,
                is_character_array ? relative_offset : 0))
          return 0;
      }
      return 1;
    }
    for (int i = 0; i < shape.array_len; i++) {
      if (!collect_initializer_scalar_leaves(
              semantic_types, record_decls, record_layouts, data_layout,
              child_type, relative_offset + i * child_size, list))
        return 0;
    }
    return 1;
  }
  if (psx_type_kind_is_aggregate(shape.kind)) {
    const psx_record_decl_t *record = psx_record_decl_table_lookup(
        record_decls, shape.record_id);
    if (!record || record->member_count <= 0) return 0;
    int first_member = 0;
    int member_count = record->member_count;
    int union_capacity_member = -1;
    int union_capacity_slots = -1;
    if (shape.kind == PSX_TYPE_UNION) {
      int max_bytes = -1;
      int max_slots = -1;
      for (int i = 0; i < record->member_count; i++) {
        const psx_record_member_decl_t *candidate = &record->members[i];
        psx_qual_type_t candidate_type =
            psx_semantic_type_table_record_member(
                semantic_types, qual_type.type_id, i);
        int bytes = initializer_qual_type_size(
            semantic_types, record_layouts, candidate_type,
            data_layout);
        int slots = canonical_member_flat_slot_count(
            semantic_types, record_decls, record_layouts, data_layout,
            candidate, candidate_type.type_id);
        if (bytes > max_bytes ||
            (bytes == max_bytes && slots > max_slots)) {
          first_member = i;
          max_bytes = bytes;
          max_slots = slots;
        }
        if (slots > union_capacity_slots) {
          union_capacity_member = i;
          union_capacity_slots = slots;
        }
      }
      member_count = first_member + 1;
    }
    int union_leaf_begin = list->count;
    int covered_end = -1;
    for (int i = first_member; i < member_count; i++) {
      const psx_record_member_decl_t *member = &record->members[i];
      int member_offset =
          initializer_member_offset(record_layouts, shape.record_id,
                                    data_layout, i);
      if (shape.kind == PSX_TYPE_STRUCT && member_offset < covered_end)
        continue;
      psx_qual_type_t member_type = psx_semantic_type_table_record_member(
          semantic_types, qual_type.type_id, i);
      member_type.qualifiers |= inherited_subobject_qualifiers(
          qual_type.qualifiers);
      psx_type_shape_t member_shape = {0};
      if (!psx_semantic_type_table_describe(
              semantic_types, member_type.type_id, &member_shape))
        return 0;
      if (member_shape.kind == PSX_TYPE_ARRAY ||
          member_shape.kind == PSX_TYPE_COMPLEX ||
          psx_type_kind_is_aggregate(member_shape.kind)) {
        if (!collect_initializer_scalar_leaves(
                semantic_types, record_decls, record_layouts, data_layout,
                member_type, relative_offset + member_offset, list))
          return 0;
      } else if (!append_scalar_leaf(
                     list, member_type, relative_offset + member_offset,
                     initializer_member_ref(
                         record_layouts, shape.record_id, data_layout,
                         i, member),
                     PSX_TYPE_ID_INVALID, 0)) {
        return 0;
      }
      if (shape.kind == PSX_TYPE_STRUCT && member->len <= 0) {
        int member_size = initializer_qual_type_size(
            semantic_types, record_layouts, member_type,
            data_layout);
        int end = member_offset + member_size;
        if (member_size > 0 && end > covered_end) covered_end = end;
      }
    }
    if (shape.kind == PSX_TYPE_UNION &&
        union_capacity_member >= 0 &&
        list->count - union_leaf_begin < union_capacity_slots) {
      psx_qual_type_t capacity_type =
          psx_semantic_type_table_record_member(
              semantic_types, qual_type.type_id,
              union_capacity_member);
      capacity_type.qualifiers |= inherited_subobject_qualifiers(
          qual_type.qualifiers);
      int capacity_offset = initializer_member_offset(
          record_layouts, shape.record_id, data_layout,
          union_capacity_member);
      psx_initializer_scalar_leaf_list_t capacity_leaves = {0};
      if (capacity_offset < 0 ||
          !collect_initializer_scalar_leaves(
              semantic_types, record_decls, record_layouts, data_layout,
              capacity_type, relative_offset + capacity_offset,
              &capacity_leaves) ||
          capacity_leaves.count < union_capacity_slots) {
        psx_initializer_scalar_leaf_list_dispose(&capacity_leaves);
        return 0;
      }
      int canonical_count = list->count - union_leaf_begin;
      for (int i = canonical_count; i < union_capacity_slots; i++) {
        const psx_initializer_scalar_leaf_t *leaf =
            &capacity_leaves.items[i];
        if (!append_scalar_leaf(
                list, leaf->qual_type, leaf->relative_offset,
                leaf->member_ref, leaf->string_array_type_id,
                leaf->string_array_offset)) {
          psx_initializer_scalar_leaf_list_dispose(&capacity_leaves);
          return 0;
        }
      }
      psx_initializer_scalar_leaf_list_dispose(&capacity_leaves);
    }
    return 1;
  }
  if (shape.kind == PSX_TYPE_COMPLEX) {
    psx_qual_type_t component = psx_semantic_type_table_base(
        semantic_types, qual_type.type_id);
    component.qualifiers |= inherited_subobject_qualifiers(
        qual_type.qualifiers);
    psx_type_shape_t component_shape = {0};
    int component_size = initializer_type_size(semantic_types, record_layouts,
                                               component.type_id, data_layout);
    if (!psx_semantic_type_table_describe(
            semantic_types, component.type_id, &component_shape) ||
        component_shape.kind != PSX_TYPE_FLOAT ||
        component_size <= 0)
      return 0;
    return append_scalar_leaf(
               list, component,
               relative_offset, (psx_initializer_member_ref_t){0},
               PSX_TYPE_ID_INVALID, 0) &&
           append_scalar_leaf(
               list, component,
               relative_offset + component_size,
               (psx_initializer_member_ref_t){0},
               PSX_TYPE_ID_INVALID, 0);
  }
  return append_scalar_leaf(
      list, qual_type, relative_offset,
      (psx_initializer_member_ref_t){0},
      PSX_TYPE_ID_INVALID, 0);
}

int psx_collect_initializer_scalar_leaves_with_records(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_data_layout_t *data_layout, psx_qual_type_t qual_type,
    int relative_offset, psx_initializer_scalar_leaf_list_t *list) {
  if (!semantic_types || !record_decls || !record_layouts ||
      !ag_data_layout_is_valid(data_layout) || !list)
    return 0;
  return collect_initializer_scalar_leaves(semantic_types, record_decls,
                                           record_layouts, data_layout,
                                           qual_type, relative_offset, list);
}

void psx_initializer_scalar_leaf_list_dispose(
    psx_initializer_scalar_leaf_list_t *list) {
  if (!list) return;
  free(list->items);
  list->items = NULL;
  list->count = 0;
  list->capacity = 0;
}
