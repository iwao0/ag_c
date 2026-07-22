#include "declarator_shape_builder.h"

#include "arena.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>

void ps_declarator_shape_init(psx_declarator_shape_t *shape) {
  if (!shape) return;
  *shape = (psx_declarator_shape_t){0};
}

static int declarator_shape_next_capacity(
    int current_capacity, int required_capacity) {
  if (current_capacity < 0 || required_capacity <= 0) return 0;
  int capacity = current_capacity > 0 ? current_capacity : 8;
  while (capacity < required_capacity) {
    if (capacity > INT_MAX / 2) return 0;
    capacity *= 2;
  }
  return capacity;
}

static psx_declarator_op_t *declarator_shape_append(
    arena_context_t *arena_context, psx_declarator_shape_t *shape,
    psx_declarator_op_kind_t kind) {
  if (!shape || shape->count < 0 || shape->capacity < 0 ||
      shape->count > shape->capacity)
    return NULL;
  if (shape->count == shape->capacity) {
    int capacity = declarator_shape_next_capacity(
        shape->capacity, shape->count + 1);
    if (capacity <= 0) return NULL;
    psx_declarator_op_t *ops = arena_alloc_in(
        arena_context, (size_t)capacity * sizeof(*ops));
    if (shape->ops && shape->count > 0)
      memcpy(ops, shape->ops, (size_t)shape->count * sizeof(*ops));
    shape->ops = ops;
    shape->capacity = capacity;
  }
  psx_declarator_op_t *op = &shape->ops[shape->count++];
  *op = (psx_declarator_op_t){0};
  op->kind = kind;
  return op;
}

int ps_declarator_shape_append_pointer_in(
    arena_context_t *arena_context, psx_declarator_shape_t *shape,
    int is_const_qualified, int is_volatile_qualified,
    int is_restrict_qualified) {
  psx_declarator_op_t *op = declarator_shape_append(
      arena_context, shape, PSX_DECL_OP_POINTER);
  if (!op) return 0;
  op->is_const_qualified = is_const_qualified ? 1u : 0u;
  op->is_volatile_qualified = is_volatile_qualified ? 1u : 0u;
  op->is_restrict_qualified = is_restrict_qualified ? 1u : 0u;
  return 1;
}

int ps_declarator_shape_append_array_in(
    arena_context_t *arena_context, psx_declarator_shape_t *shape,
    int array_len) {
  return ps_declarator_shape_append_array_ex_in(
      arena_context, shape, array_len, 0);
}

int ps_declarator_shape_append_array_ex_in(
    arena_context_t *arena_context, psx_declarator_shape_t *shape,
    int array_len, int is_incomplete) {
  psx_declarator_op_t *op = declarator_shape_append(
      arena_context, shape, PSX_DECL_OP_ARRAY);
  if (!op) return 0;
  op->array_len = array_len;
  op->is_incomplete_array = is_incomplete ? 1u : 0u;
  return 1;
}

int ps_declarator_shape_append_vla_array_in(
    arena_context_t *arena_context, psx_declarator_shape_t *shape) {
  psx_declarator_op_t *op = declarator_shape_append(
      arena_context, shape, PSX_DECL_OP_ARRAY);
  if (!op) return 0;
  op->is_vla_array = 1;
  return 1;
}

int ps_declarator_shape_append_function_in(
    arena_context_t *arena_context, psx_declarator_shape_t *shape) {
  return declarator_shape_append(
             arena_context, shape, PSX_DECL_OP_FUNCTION) != NULL;
}

int ps_declarator_op_set_function_param_qual_types_in(
    arena_context_t *arena_context, psx_declarator_op_t *op,
    const psx_qual_type_t *param_qual_types, int param_count,
    int is_variadic, int has_prototype) {
  if (!op || op->kind != PSX_DECL_OP_FUNCTION || param_count < 0)
    return 0;
  op->function_param_qual_types = NULL;
  op->function_param_count = param_count;
  op->function_is_variadic = is_variadic ? 1 : 0;
  op->function_has_prototype = has_prototype ? 1 : 0;
  op->has_canonical_function_params = 1;
  if (param_count == 0) return 1;
  op->function_param_qual_types = arena_alloc_in(
      arena_context,
      (size_t)param_count * sizeof(*op->function_param_qual_types));
  if (!op->function_param_qual_types) return 0;
  for (int i = 0; i < param_count; i++)
    op->function_param_qual_types[i] = param_qual_types
        ? param_qual_types[i]
        : (psx_qual_type_t){
              PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  return 1;
}

int ps_declarator_shape_set_array_bound(
    psx_declarator_shape_t *shape, int op_index,
    int array_len, int is_vla) {
  if (!shape || op_index < 0 || op_index >= shape->count ||
      shape->ops[op_index].kind != PSX_DECL_OP_ARRAY)
    return 0;
  psx_declarator_op_t *op = &shape->ops[op_index];
  op->array_len = array_len;
  op->is_incomplete_array = 0;
  op->is_vla_array = is_vla ? 1u : 0u;
  return 1;
}

int ps_declarator_op_set_variadic(
    psx_declarator_op_t *op, int is_variadic) {
  if (!op || op->kind != PSX_DECL_OP_FUNCTION) return 0;
  op->function_is_variadic = is_variadic ? 1 : 0;
  return 1;
}

int ps_declarator_shape_append_shape_in(
    arena_context_t *arena_context, psx_declarator_shape_t *shape,
    const psx_declarator_shape_t *suffix) {
  if (!shape || !suffix || suffix->count < 0) return 0;
  int suffix_count = suffix->count;
  for (int i = 0; i < suffix_count; i++) {
    const psx_declarator_op_t *op = &suffix->ops[i];
    int appended = 0;
    if (op->kind == PSX_DECL_OP_POINTER) {
      appended = ps_declarator_shape_append_pointer_in(
          arena_context, shape, op->is_const_qualified,
          op->is_volatile_qualified, op->is_restrict_qualified);
    } else if (op->kind == PSX_DECL_OP_ARRAY) {
      if (op->is_vla_array)
        appended = ps_declarator_shape_append_vla_array_in(
            arena_context, shape);
      else
        appended = ps_declarator_shape_append_array_ex_in(
            arena_context, shape, op->array_len,
            op->is_incomplete_array);
      if (appended) {
        psx_declarator_op_t *copy = &shape->ops[shape->count - 1];
        copy->is_const_qualified = op->is_const_qualified;
        copy->is_volatile_qualified = op->is_volatile_qualified;
        copy->is_restrict_qualified = op->is_restrict_qualified;
      }
    } else if (op->kind == PSX_DECL_OP_FUNCTION) {
      appended = ps_declarator_shape_append_function_in(
          arena_context, shape);
      if (appended && op->has_canonical_function_params) {
        psx_declarator_op_t *copy = &shape->ops[shape->count - 1];
        appended = ps_declarator_op_set_function_param_qual_types_in(
            arena_context, copy, op->function_param_qual_types,
            op->function_param_count, op->function_is_variadic,
            op->function_has_prototype);
      }
    }
    if (!appended) return 0;
  }
  return 1;
}

int ps_declarator_shape_copy_in(
    arena_context_t *arena_context, psx_declarator_shape_t *dst,
    const psx_declarator_shape_t *src) {
  if (!dst || !src) return 0;
  ps_declarator_shape_init(dst);
  return ps_declarator_shape_append_shape_in(arena_context, dst, src);
}

int ps_declarator_shape_count_ops(
    const psx_declarator_shape_t *shape,
    psx_declarator_op_kind_t kind) {
  if (!shape) return 0;
  int count = 0;
  for (int i = 0; i < shape->count; i++) {
    if (shape->ops[i].kind == kind) count++;
  }
  return count;
}
