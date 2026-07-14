#include "type_query_resolution.h"

#include "constant_expression.h"
#include "declaration_resolution.h"
#include "type_name_resolution.h"
#include "../parser/arena.h"
#include "../parser/lvar_public.h"
#include "../parser/node_utils.h"
#include "../parser/semantic_ctx.h"

#include <string.h>

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

static lvar_t *sizeof_lvar(node_t *base) {
  lvar_t *var = ps_node_lvar_symbol(base);
  if (!var && base && base->kind == ND_ADDR)
    var = ps_node_lvar_symbol(base->lhs);
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

static node_t *widen_size_value(node_t *value) {
  return ps_node_new_integer_cast_result(
      value, ps_type_new_integer(TK_UNSIGNED, 8, 1));
}

static void resolve_sizeof_type_name(
    node_sizeof_query_t *query,
    psx_sizeof_query_resolution_t *resolution) {
  psx_parsed_type_name_t *syntax =
      query ? query->type_name.syntax : NULL;
  if (!query || !query->is_type_name || !syntax) return;
  if (!psx_bind_type_name_ref(&query->type_name) ||
      !query->type_name.bound_base_type) {
    resolution->status = PSX_TYPE_QUERY_RESOLUTION_TYPE_UNRESOLVED;
    return;
  }

  psx_type_t *base_type = query->type_name.bound_base_type;
  psx_declarator_shape_t *shape = &syntax->declarator.declarator_shape;
  if (syntax->declarator.array_bound_count > 0) {
    resolution->zero_length_bound_indices = arena_alloc(
        (size_t)syntax->declarator.array_bound_count *
        sizeof(*resolution->zero_length_bound_indices));
  }
  for (int i = 0; i < syntax->declarator.array_bound_count; i++) {
    psx_parsed_array_bound_t *parsed_bound =
        &syntax->declarator.array_bounds[i];
    node_t *bound = parsed_bound->expression.node;
    int is_constant = 1;
    long long value = psx_eval_const_int(bound, &is_constant);
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
    if (op_index < 0 || op_index >= shape->count ||
        shape->ops[op_index].kind != PSX_DECL_OP_ARRAY) {
      resolution->status =
          PSX_TYPE_QUERY_RESOLUTION_INVALID_ARRAY_BOUND_TARGET;
      resolution->issue_bound_index = i;
      return;
    }
    psx_declarator_op_t *op = &shape->ops[op_index];
    op->array_len = is_constant ? (int)value : 0;
    op->is_incomplete_array = 0;
    op->is_vla_array = is_constant ? 0 : 1;
  }

  psx_type_t *resolved_type = psx_resolve_decl_type(
      &(psx_decl_type_request_t){
          .base_type = base_type,
          .declarator_shape = shape,
      });
  ps_ctx_refresh_type_completeness(resolved_type);
  query->type_name.resolved_type = resolved_type;
  query->queried_type = resolved_type;
  if (!query->queried_type) {
    resolution->status = PSX_TYPE_QUERY_RESOLUTION_TYPE_UNRESOLVED;
    return;
  }

  int base_size = ps_type_sizeof(base_type);
  if (base_type->kind == PSX_TYPE_VOID) base_size = 1;
  node_t *size = widen_size_value(ps_node_new_num(base_size));
  int is_runtime = 0;
  for (int i = shape->count - 1; i >= 0; i--) {
    psx_declarator_op_t *op = &shape->ops[i];
    if (op->kind == PSX_DECL_OP_POINTER) {
      size = widen_size_value(ps_node_new_num(8));
      is_runtime = 0;
      continue;
    }
    if (op->kind != PSX_DECL_OP_ARRAY) continue;
    node_t *bound = sizeof_type_bound_for_op(query, i);
    if (op->is_vla_array && bound) {
      size = ps_node_new_binary(
          ND_MUL, widen_size_value(bound), size);
      is_runtime = 1;
    } else {
      size = ps_node_new_binary(
          ND_MUL, widen_size_value(ps_node_new_num(op->array_len)), size);
    }
  }
  if (is_runtime) query->runtime_size_expr = size;
}

static const psx_type_t *sizeof_operand_type(node_sizeof_query_t *query) {
  if (!query) return NULL;
  if (query->is_type_name) return query->queried_type;
  node_t *operand = query->operand;
  if (!operand) return NULL;
  if (operand->kind == ND_COMPOUND_LITERAL) {
    node_compound_literal_t *compound = (node_compound_literal_t *)operand;
    if (compound->object_type &&
        compound->object_type->kind == PSX_TYPE_ARRAY &&
        compound->object_type->array_len <= 0 && compound->base.rhs) {
      psx_resolve_incomplete_array_initializer(
          compound->object_type, PSX_DECL_INIT_LIST, compound->base.rhs);
    }
    if (compound->requires_addressable_object)
      return ps_node_get_type(operand);
    return compound->object_type;
  }
  int depth = 0;
  node_t *base = sizeof_base(operand, &depth);
  lvar_t *var = sizeof_lvar(base);
  int explicit_addr =
      operand->kind == ND_ADDR && operand->is_explicit_addr_expr;
  if (depth == 0 && !explicit_addr && var && ps_lvar_is_array(var))
    return ps_lvar_get_decl_type(var);
  if (depth == 0 && operand->kind == ND_ADDR && !explicit_addr &&
      operand->lhs) {
    const psx_type_t *object_type = ps_node_get_type(operand->lhs);
    if (object_type && object_type->kind == PSX_TYPE_ARRAY)
      return object_type;
  }
  return ps_node_get_type(operand);
}

void psx_resolve_sizeof_query(
    node_sizeof_query_t *query,
    psx_sizeof_query_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_TYPE_QUERY_RESOLUTION_OK;
  resolution->issue_bound_index = -1;
  if (!query) {
    resolution->status = PSX_TYPE_QUERY_RESOLUTION_TYPE_UNRESOLVED;
    return;
  }

  resolve_sizeof_type_name(query, resolution);
  if (resolution->status != PSX_TYPE_QUERY_RESOLUTION_OK) return;
  const psx_type_t *type = sizeof_operand_type(query);
  if (!query->is_type_name && type) {
    psx_type_t *completed_view = ps_type_clone(type);
    ps_ctx_refresh_type_completeness(completed_view);
    type = completed_view;
  }
  query->queried_type = type;

  int subscript_depth = 0;
  node_t *base = sizeof_base(query->operand, &subscript_depth);
  lvar_t *var = sizeof_lvar(base);
  if (!query->is_type_name && var && ps_lvar_is_vla(var)) {
    if (subscript_depth == 0) {
      const psx_type_t *decl_type = ps_lvar_get_decl_type(var);
      if (decl_type && decl_type->kind == PSX_TYPE_ARRAY)
        query->runtime_size_slot = ps_lvar_offset(var) + 8;
    } else {
      int row_slot = ps_lvar_vla_row_stride_frame_off(var);
      int remaining = ps_lvar_vla_strides_remaining(var);
      if (row_slot != 0 &&
          (subscript_depth == 1 || subscript_depth - 1 <= remaining)) {
        query->runtime_size_slot = row_slot + 8 * (subscript_depth - 1);
      }
    }
    if (query->runtime_size_slot != 0 && subscript_depth > 0)
      query->evaluates_vla_operand = 1;
  } else if (!query->is_type_name && type && type->is_vla &&
             subscript_depth > 0 && type->kind == PSX_TYPE_ARRAY &&
             ps_node_vla_row_stride_frame_off(query->operand) != 0) {
    query->runtime_size_slot =
        ps_node_vla_row_stride_frame_off(query->operand);
    query->evaluates_vla_operand = 1;
  }

  resolution->usage_root = query->runtime_size_slot != 0
                               ? base
                               : query->operand;
  resolution->evaluates_vla_operand = query->evaluates_vla_operand;
  if (query->runtime_size_slot != 0) return;
  if (query->operand && query->operand->kind == ND_STRING) {
    node_string_t *string = (node_string_t *)query->operand;
    int width = string->char_width ? (int)string->char_width : 1;
    query->resolved_size = (string->byte_len + 1) * width;
    return;
  }
  int size = type ? ps_type_sizeof(type) : 0;
  if (type && type->kind == PSX_TYPE_VOID) size = 1;
  if (size <= 0 && query->operand) size = ps_node_type_size(query->operand);
  query->resolved_size = size > 0 ? size : 8;
}

void psx_resolve_alignof_query(node_alignof_query_t *query) {
  if (!query) return;
  psx_type_t *type =
      psx_resolve_bound_type_name_ref(&query->type_name);
  ps_ctx_refresh_type_completeness(type);
  query->resolved_alignment = type && type->align > 0 ? type->align : 1;
}
