#include "type_query_resolution.h"
#include "declaration_type_builder.h"

#include "constant_expression.h"
#include "declaration_resolution.h"
#include "alignof_query_resolution.h"
#include "sizeof_query_resolution.h"
#include "type_name_resolution.h"
#include "resolved_object_ref.h"
#include "../parser/arena.h"
#include "../parser/declarator_shape_builder.h"
#include "../parser/global_registry.h"
#include "../parser/lvar_public.h"
#include "../parser/local_registry.h"
#include "../parser/node_utils.h"
#include "../parser/semantic_ctx.h"
#include "../parser/type_builder.h"
#include "../parser/vla_runtime.h"

#include <string.h>

static int query_type_size(
    psx_semantic_context_t *semantic_context,
    const psx_type_t *type) {
  return ps_ctx_type_sizeof_in(semantic_context, type);
}

static int query_type_alignment(
    psx_semantic_context_t *semantic_context,
    const psx_type_t *type) {
  return ps_ctx_type_alignof_in(semantic_context, type);
}

static node_t *sizeof_base(node_t *operand, int *subscript_depth) {
  int depth = 0;
  node_t *base = operand;
  while (base && base->kind == ND_SUBSCRIPT) {
    depth++;
    base = base->lhs;
  }
  if (subscript_depth) *subscript_depth = depth;
  return base;
}

static lvar_t *sizeof_lvar(
    const psx_resolution_store_t *store, node_t *base) {
  lvar_t *var = ps_node_lvar_symbol(store, base);
  if (!var && base && base->kind == ND_ADDR)
    var = ps_node_lvar_symbol(store, base->lhs);
  return var;
}

static node_t *sizeof_type_bound_for_op(
    node_sizeof_query_t *query, int op_index) {
  psx_parsed_type_name_t *syntax =
      query ? query->type_name.syntax : NULL;
  for (int i = 0;
       syntax && i < syntax->declarator.array_bound_count; i++) {
    psx_parsed_array_bound_t *bound = &syntax->declarator.array_bounds[i];
    if (bound->declarator_op_index == op_index)
      return bound->expression.node;
  }
  return NULL;
}

static void resolve_sizeof_type_name(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    node_sizeof_query_t *query,
    psx_sizeof_query_resolution_t *resolution) {
  psx_parsed_type_name_t *syntax =
      query ? query->type_name.syntax : NULL;
  if (!query || !query->is_type_name || !syntax) return;
  psx_resolution_store_t *store =
      ps_ctx_resolution_store(semantic_context);
  psx_type_name_resolution_state_t *type_name_state =
      psx_node_type_name_state_mut(store, &query->base);
  psx_type_name_resolution_state_t rebound_state = {0};
  psx_type_name_resolution_state_t *bound_state =
      type_name_state && type_name_state->kind == PSX_TYPE_NAME_RESOLVED
          ? &rebound_state : type_name_state;
  if (!psx_bind_type_name_ref_in_contexts(
          semantic_context, global_registry, local_registry,
          &query->type_name, bound_state) ||
      !type_name_state) {
    resolution->status = PSX_TYPE_QUERY_RESOLUTION_TYPE_UNRESOLVED;
    return;
  }

  const psx_type_t *base_type =
      psx_type_name_bound_base_type(bound_state);
  if (!base_type) {
    resolution->status = PSX_TYPE_QUERY_RESOLUTION_TYPE_UNRESOLVED;
    return;
  }
  psx_declarator_shape_t shape_storage;
  if (!ps_declarator_shape_copy_in(
          ps_ctx_arena(semantic_context), &shape_storage,
          &syntax->declarator.declarator_shape)) {
    resolution->status = PSX_TYPE_QUERY_RESOLUTION_TYPE_UNRESOLVED;
    return;
  }
  psx_declarator_shape_t *shape = &shape_storage;
  if (syntax->declarator.array_bound_count > 0) {
    resolution->zero_length_bound_indices = arena_alloc_in(
        ps_ctx_arena(semantic_context),
        (size_t)syntax->declarator.array_bound_count *
        sizeof(*resolution->zero_length_bound_indices));
  }
  for (int i = 0; i < syntax->declarator.array_bound_count; i++) {
    psx_parsed_array_bound_t *parsed_bound =
        &syntax->declarator.array_bounds[i];
    node_t *bound = parsed_bound->expression.node;
    int is_constant = 1;
    long long value =
        psx_eval_const_int(store, bound, &is_constant);
    if (is_constant && value < 0) {
      resolution->status =
          PSX_TYPE_QUERY_RESOLUTION_NEGATIVE_ARRAY_BOUND;
      resolution->issue_bound_index = i;
      return;
    }
    if (is_constant && value == 0) {
      resolution->zero_length_bound_indices[
          resolution->zero_length_bound_count++] = i;
    }
    int op_index = parsed_bound->declarator_op_index;
    if (!ps_declarator_shape_set_array_bound(
            shape, op_index, is_constant ? (int)value : 0,
            !is_constant)) {
      resolution->status =
          PSX_TYPE_QUERY_RESOLUTION_INVALID_ARRAY_BOUND_TARGET;
      resolution->issue_bound_index = i;
      return;
    }
  }

  psx_type_t *resolved_type = psx_build_decl_type(
      &(psx_decl_type_request_t){
          .semantic_context = semantic_context,
          .base_type = base_type,
          .declarator_shape = shape,
      });
  ps_ctx_bind_record_ids_in(
      semantic_context, resolved_type);
  if (!psx_type_name_bind_resolved_type_in(
          semantic_context, type_name_state, resolved_type)) {
    resolution->status = PSX_TYPE_QUERY_RESOLUTION_TYPE_UNRESOLVED;
    return;
  }

  const ag_target_info_t *target = ps_ctx_target_info(semantic_context);
  int base_size = query_type_size(semantic_context, base_type);
  if (base_type->kind == PSX_TYPE_VOID) base_size = 1;
  arena_context_t *arena_context = ps_ctx_arena(semantic_context);
  psx_sizeof_runtime_plan_t *runtime_plan =
      arena_alloc_in(arena_context, sizeof(*runtime_plan));
  node_t **runtime_bounds = syntax->declarator.array_bound_count > 0
                                ? arena_alloc_in(
                                      arena_context,
                                      (size_t)syntax->declarator
                                          .array_bound_count *
                                          sizeof(*runtime_bounds))
                                : NULL;
  if (!runtime_plan ||
      (syntax->declarator.array_bound_count > 0 && !runtime_bounds)) {
    resolution->status = PSX_TYPE_QUERY_RESOLUTION_TYPE_UNRESOLVED;
    return;
  }
  *runtime_plan = (psx_sizeof_runtime_plan_t){
      .runtime_bounds = runtime_bounds,
      .constant_factor = base_size,
  };
  int is_runtime = 0;
  for (int i = shape->count - 1; i >= 0; i--) {
    psx_declarator_op_t *op = &shape->ops[i];
    if (op->kind == PSX_DECL_OP_POINTER) {
      runtime_plan->constant_factor =
          ag_target_info_pointer_size(target);
      runtime_plan->runtime_bound_count = 0;
      is_runtime = 0;
      continue;
    }
    if (op->kind != PSX_DECL_OP_ARRAY) continue;
    node_t *bound = sizeof_type_bound_for_op(query, i);
    if (op->is_vla_array && bound) {
      runtime_plan->runtime_bounds[
          runtime_plan->runtime_bound_count++] = bound;
      is_runtime = 1;
    } else {
      runtime_plan->constant_factor *= op->array_len;
    }
  }
  psx_sizeof_query_resolution_state_t *query_resolution =
      psx_sizeof_query_resolution_state(store, query);
  if (is_runtime && query_resolution)
    query_resolution->runtime_plan = runtime_plan;
}

static const psx_type_t *sizeof_operand_type(
    psx_semantic_context_t *semantic_context,
    node_sizeof_query_t *query) {
  if (!query) return NULL;
  psx_resolution_store_t *store =
      ps_ctx_resolution_store(semantic_context);
  if (query->is_type_name)
    return psx_node_resolved_type_name(store, &query->base);
  node_t *operand = query->operand;
  if (!operand) return NULL;
  if (operand->kind == ND_COMPOUND_LITERAL) {
    node_compound_literal_t *compound = (node_compound_literal_t *)operand;
    psx_type_name_resolution_state_t *compound_type_name =
        psx_node_type_name_state_mut(store, &compound->base);
    const psx_type_t *object_type =
        psx_type_name_resolved_type(compound_type_name);
    if (object_type && object_type->kind == PSX_TYPE_ARRAY &&
        object_type->array_len <= 0 && compound->base.rhs) {
      psx_type_t *completed = ps_type_clone_in(
          ps_ctx_arena(semantic_context), object_type);
      if (completed && psx_resolve_incomplete_array_initializer(
                           semantic_context, completed,
                           PSX_DECL_INIT_LIST,
                           compound->base.rhs)) {
        if (psx_type_name_bind_resolved_type_in(
                semantic_context, compound_type_name, completed))
          object_type = psx_type_name_resolved_type(
              compound_type_name);
      }
    }
    if (compound->requires_addressable_object)
      return ps_node_get_type(store, operand);
    return object_type;
  }
  int depth = 0;
  node_t *base = sizeof_base(operand, &depth);
  lvar_t *var = sizeof_lvar(store, base);
  int explicit_addr =
      operand->kind == ND_ADDR && operand->is_explicit_addr_expr;
  if (depth == 0 && !explicit_addr && var && ps_lvar_is_array(var))
    return ps_lvar_get_decl_type(var);
  if (depth == 0 && operand->kind == ND_ADDR && !explicit_addr &&
      operand->lhs) {
    const psx_type_t *object_type =
        ps_node_get_type(store, operand->lhs);
    if (object_type && object_type->kind == PSX_TYPE_ARRAY)
      return object_type;
  }
  return ps_node_get_type(store, operand);
}

void psx_resolve_sizeof_query_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    node_sizeof_query_t *query,
    psx_sizeof_query_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_TYPE_QUERY_RESOLUTION_OK;
  resolution->issue_bound_index = -1;
  if (!semantic_context || !global_registry || !local_registry || !query) {
    resolution->status = PSX_TYPE_QUERY_RESOLUTION_TYPE_UNRESOLVED;
    return;
  }
  psx_resolution_store_t *store =
      ps_ctx_resolution_store(semantic_context);
  if (!ps_node_prepare_resolution_state_for_size_in(
          store, ps_ctx_arena(semantic_context), &query->base,
          sizeof(*query))) {
    resolution->status = PSX_TYPE_QUERY_RESOLUTION_TYPE_UNRESOLVED;
    return;
  }
  psx_sizeof_query_resolution_state_t *query_resolution =
      psx_sizeof_query_resolution_state(store, query);
  if (!query_resolution) {
    resolution->status = PSX_TYPE_QUERY_RESOLUTION_TYPE_UNRESOLVED;
    return;
  }
  memset(query_resolution, 0, sizeof(*query_resolution));

  resolve_sizeof_type_name(
      semantic_context, global_registry, local_registry,
      query, resolution);
  if (resolution->status != PSX_TYPE_QUERY_RESOLUTION_OK) return;
  const psx_type_t *type = sizeof_operand_type(
      semantic_context, query);
  if (!query->is_type_name && type) {
    psx_type_t *completed_view = ps_type_clone_in(
        ps_ctx_arena(semantic_context), type);
    ps_ctx_bind_record_ids_in(
        semantic_context, completed_view);
    type = completed_view;
  }

  int subscript_depth = 0;
  node_t *base = sizeof_base(query->operand, &subscript_depth);
  lvar_t *var = sizeof_lvar(store, base);
  if (!query->is_type_name && var && ps_lvar_is_vla(var)) {
    if (subscript_depth == 0) {
      const psx_type_t *decl_type = ps_lvar_get_decl_type(var);
      if (decl_type && decl_type->kind == PSX_TYPE_ARRAY)
        query_resolution->runtime_size_slot =
            ps_lvar_offset(var) + PSX_VLA_RUNTIME_SIZE_RELATIVE_OFFSET;
    } else {
      int row_slot = ps_lvar_vla_row_stride_frame_off(var);
      int remaining = ps_lvar_vla_strides_remaining(var);
      if (row_slot != 0 &&
          (subscript_depth == 1 || subscript_depth - 1 <= remaining)) {
        query_resolution->runtime_size_slot =
            row_slot + PSX_VLA_RUNTIME_SLOT_SIZE * (subscript_depth - 1);
      }
    }
    if (query_resolution->runtime_size_slot != 0 && subscript_depth > 0)
      query_resolution->evaluates_vla_operand = 1;
  } else if (!query->is_type_name && type && type->is_vla &&
             subscript_depth > 0 && type->kind == PSX_TYPE_ARRAY &&
             ps_node_vla_row_stride_frame_off(
                 store, query->operand) != 0) {
    query_resolution->runtime_size_slot =
        ps_node_vla_row_stride_frame_off(store, query->operand);
    query_resolution->evaluates_vla_operand = 1;
  }

  resolution->usage_root = query_resolution->runtime_size_slot != 0
                               ? base
                               : query->operand;
  resolution->evaluates_vla_operand =
      query_resolution->evaluates_vla_operand;
  if (query_resolution->runtime_size_slot != 0) return;
  if (query->operand && query->operand->kind == ND_STRING) {
    node_string_t *string = (node_string_t *)query->operand;
    int width = string->char_width ? (int)string->char_width : 1;
    query_resolution->resolved_size =
        (string->byte_len + 1) * width;
    return;
  }
  int size = type ? query_type_size(semantic_context, type) : 0;
  if (type && type->kind == PSX_TYPE_VOID) size = 1;
  query_resolution->resolved_size = size > 0 ? size : 8;
}

void psx_resolve_alignof_query_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    node_alignof_query_t *query) {
  if (!semantic_context || !global_registry || !local_registry || !query)
    return;
  psx_resolution_store_t *store =
      ps_ctx_resolution_store(semantic_context);
  if (!ps_node_prepare_resolution_state_for_size_in(
          store, ps_ctx_arena(semantic_context), &query->base,
          sizeof(*query)))
    return;
  const psx_type_t *type =
      psx_resolve_bound_type_name_ref_in_contexts(
          semantic_context, global_registry, local_registry,
          &query->type_name,
          psx_node_type_name_state_mut(store, &query->base));
  int alignment = query_type_alignment(semantic_context, type);
  psx_alignof_query_resolution_state_t *resolution =
      psx_alignof_query_resolution_state(store, query);
  if (resolution)
    resolution->resolved_alignment = alignment > 0 ? alignment : 1;
}
