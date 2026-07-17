#include "runtime_initializer_plan.h"

#include "runtime_context.h"
#include "../parser/arena.h"
#include "../parser/lvar_public.h"
#include "../parser/node_utils.h"
#include "../semantic/resolved_node_kind.h"
#include "../semantic/resolved_object_ref.h"

static int count_items(const node_t *node) {
  if (!node) return 0;
  if (node->kind == ND_COMMA)
    return count_items(node->lhs) + count_items(node->rhs);
  return 1;
}

static psx_qual_type_t resolved_node_qual_type(
    const psx_lowering_context_t *lowering_context,
    const node_t *node) {
  psx_qual_type_t qual_type = ps_node_qual_type(node);
  if (qual_type.type_id != PSX_TYPE_ID_INVALID) return qual_type;
  return psx_semantic_type_table_find(
      ps_lowering_semantic_types(lowering_context),
      ps_node_get_type(node));
}

static int local_ref_from_node(
    const psx_lowering_context_t *lowering_context,
    const node_t *node, psx_runtime_initializer_local_ref_t *ref) {
  if (!node || psx_resolved_object_ref_node_kind(node) != ND_LVAR || !ref)
    return 0;
  lvar_t *local = psx_resolved_object_ref_local(node);
  if (!local) return 0;
  int bit_width = 0;
  int bit_offset = 0;
  int bit_is_signed = 0;
  ps_node_bitfield_info(
      (node_t *)node, &bit_width, &bit_offset, &bit_is_signed);
  *ref = (psx_runtime_initializer_local_ref_t){
      .local = local,
      .relative_offset =
          psx_resolved_object_ref_storage_offset(node) -
          ps_lvar_offset(local),
      .qual_type = resolved_node_qual_type(lowering_context, node),
      .bit_width = (unsigned char)(bit_width > 0 ? bit_width : 0),
      .bit_offset = (unsigned char)(bit_offset > 0 ? bit_offset : 0),
      .bit_is_signed = bit_is_signed ? 1 : 0,
  };
  return ref->qual_type.type_id != PSX_TYPE_ID_INVALID;
}

static int value_from_node(
    const psx_lowering_context_t *lowering_context,
    const node_t *node, psx_runtime_initializer_value_t *value) {
  if (!node || !value) return 0;
  *value = (psx_runtime_initializer_value_t){
      .kind = PSX_RUNTIME_INITIALIZER_VALUE_EXPRESSION,
      .resolved_qual_type =
          resolved_node_qual_type(lowering_context, node),
      .expression = node,
  };
  if (psx_resolved_object_ref_node_kind(node) == ND_LVAR &&
      local_ref_from_node(lowering_context, node, &value->local)) {
    value->kind = PSX_RUNTIME_INITIALIZER_VALUE_LOCAL;
    value->resolved_qual_type = value->local.qual_type;
    return 1;
  }
  if (node->kind == ND_NUM) {
    const node_num_t *number = (const node_num_t *)node;
    value->kind = PSX_RUNTIME_INITIALIZER_VALUE_NUMBER;
    value->number.integer_value = number->val;
    value->number.floating_value = number->fval;
  }
  return value->resolved_qual_type.type_id != PSX_TYPE_ID_INVALID;
}

static int fill_items(
    const psx_lowering_context_t *lowering_context,
    const node_t *node, psx_runtime_initializer_item_t *items,
    int *cursor) {
  if (!node) return 1;
  if (node->kind == ND_COMMA)
    return fill_items(
               lowering_context, node->lhs, items, cursor) &&
           fill_items(
               lowering_context, node->rhs, items, cursor);

  psx_runtime_initializer_item_t *item = &items[(*cursor)++];
  item->result_qual_type =
      resolved_node_qual_type(lowering_context, node);
  if (node->kind != ND_ASSIGN) {
    item->kind = PSX_RUNTIME_INITIALIZER_EVALUATE;
    return value_from_node(
        lowering_context, node, &item->value);
  }
  item->kind = PSX_RUNTIME_INITIALIZER_ASSIGN;
  return local_ref_from_node(
             lowering_context, node->lhs, &item->target) &&
         value_from_node(
             lowering_context, node->rhs, &item->value);
}

psx_runtime_initializer_plan_t *psx_build_runtime_initializer_plan(
    psx_lowering_context_t *lowering_context,
    const node_t *lowered_initialization) {
  if (!lowering_context || !lowered_initialization) return NULL;
  int item_count = count_items(lowered_initialization);
  if (item_count <= 0) return NULL;
  arena_context_t *arena_context =
      ps_lowering_arena(lowering_context);
  psx_runtime_initializer_plan_t *plan =
      arena_alloc_in(arena_context, sizeof(*plan));
  psx_runtime_initializer_item_t *items =
      arena_alloc_in(
          arena_context, (size_t)item_count * sizeof(*items));
  if (!plan || !items) return NULL;
  *plan = (psx_runtime_initializer_plan_t){
      .items = items,
      .item_count = item_count,
  };
  int cursor = 0;
  if (!fill_items(
          lowering_context, lowered_initialization, items, &cursor) ||
      cursor != item_count)
    return NULL;
  return plan;
}
