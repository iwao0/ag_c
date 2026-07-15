#include "type.h"
#include "type_builder.h"
#include "declarator_shape_builder.h"
#include "arena.h"
#include "tag_member_public.h"
#include "type_owned_internal.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

int ps_type_tag_identity_matches(const psx_type_t *a,
                                 const psx_type_t *b) {
  if (!a || !b || a->tag_kind != b->tag_kind || a->tag_len != b->tag_len)
    return 0;
  psx_record_id_t a_id = ps_type_record_id(a);
  psx_record_id_t b_id = ps_type_record_id(b);
  if (a_id != PSX_RECORD_ID_INVALID && b_id != PSX_RECORD_ID_INVALID)
    return a_id == b_id;
  if (a->aggregate_definition && b->aggregate_definition &&
      a->aggregate_definition == b->aggregate_definition)
    return 1;
  if (a->tag_len > 0 &&
      (!a->tag_name || !b->tag_name ||
       strncmp(a->tag_name, b->tag_name, (size_t)a->tag_len) != 0))
    return 0;
  if (a->tag_len == 0 &&
      (a->aggregate_definition || b->aggregate_definition))
    return 0;
  return a->tag_scope_depth_p1 == 0 || b->tag_scope_depth_p1 == 0 ||
         a->tag_scope_depth_p1 == b->tag_scope_depth_p1;
}

psx_record_id_t ps_type_record_id(const psx_type_t *type) {
  if (!type || !ps_type_is_tag_aggregate(type))
    return PSX_RECORD_ID_INVALID;
  if (type->record_id != PSX_RECORD_ID_INVALID) return type->record_id;
  return type->aggregate_definition
             ? type->aggregate_definition->record_id
             : PSX_RECORD_ID_INVALID;
}

typedef struct psx_type_validation_path_t psx_type_validation_path_t;
struct psx_type_validation_path_t {
  const psx_type_t *type;
  const psx_type_validation_path_t *parent;
};

static int type_is_well_formed_from(
    const psx_type_t *type, const psx_type_validation_path_t *path) {
  if (!type) return 1;
  for (const psx_type_validation_path_t *current = path; current;
       current = current->parent) {
    if (current->type == type) return 0;
  }
  if (type->param_count < 0 ||
      (type->param_count > 0 && !type->param_types)) {
    return 0;
  }
  psx_type_validation_path_t current_path = {
      .type = type,
      .parent = path,
  };
  if (!type_is_well_formed_from(type->base, &current_path)) return 0;
  for (int i = 0; i < type->param_count; i++) {
    if (!type->param_types[i] ||
        !type_is_well_formed_from(type->param_types[i], &current_path)) {
      return 0;
    }
  }
  return 1;
}

int ps_type_is_well_formed(const psx_type_t *type) {
  return type && type_is_well_formed_from(type, NULL);
}

psx_type_t *psx_type_owned_base_mut(psx_type_t *owner) {
  return owner ? (psx_type_t *)owner->base : NULL;
}

psx_type_t *psx_type_owned_param_mut(psx_type_t *owner, int index) {
  if (!owner || !owner->param_types || index < 0 ||
      index >= owner->param_count)
    return NULL;
  return (psx_type_t *)owner->param_types[index];
}

static token_kind_t canonical_integer_scalar_kind(
    token_kind_t scalar_kind, int size) {
  if (scalar_kind != TK_SIGNED && scalar_kind != TK_UNSIGNED &&
      scalar_kind != TK_EOF)
    return scalar_kind;
  if (size <= 1) return TK_CHAR;
  if (size == 2) return TK_SHORT;
  if (size >= 8) return TK_LONG;
  return TK_INT;
}

psx_type_t *ps_type_new_in(
    arena_context_t *arena_context, psx_type_kind_t kind) {
  psx_type_t *type = arena_alloc_in(arena_context, sizeof(psx_type_t));
  if (!type) return NULL;
  type->kind = kind;
  type->scalar_kind = TK_EOF;
  type->tag_kind = TK_EOF;
  return type;
}

void ps_type_normalize_integer_identity(psx_type_t *type) {
  if (!type) return;
  if (type->kind == PSX_TYPE_INTEGER) {
    if (type->tag_kind == TK_ENUM) type->scalar_kind = TK_ENUM;
    else
      type->scalar_kind = canonical_integer_scalar_kind(
          type->scalar_kind, type->size);
  }
  ps_type_normalize_integer_identity(psx_type_owned_base_mut(type));
  if (type->kind == PSX_TYPE_FUNCTION) {
    for (int i = 0; i < type->param_count; i++)
      ps_type_normalize_integer_identity(
          psx_type_owned_param_mut(type, i));
  }
}

void ps_type_clear_cached_layout(psx_type_t *type) {
  if (!type) return;
  type->size = 0;
  type->align = 0;
  ps_type_clear_cached_layout(psx_type_owned_base_mut(type));
  if (type->kind == PSX_TYPE_FUNCTION) {
    for (int i = 0; i < type->param_count; ++i)
      ps_type_clear_cached_layout(psx_type_owned_param_mut(type, i));
  }
}

static int type_layout_contains_record_object(const psx_type_t *type) {
  if (!type) return 0;
  if (type->kind == PSX_TYPE_STRUCT || type->kind == PSX_TYPE_UNION)
    return 1;
  return type->kind == PSX_TYPE_ARRAY &&
         type_layout_contains_record_object(type->base);
}

void ps_type_clear_record_layout_cache(psx_type_t *type) {
  if (!type) return;
  ps_type_clear_record_layout_cache(psx_type_owned_base_mut(type));
  if (type->kind == PSX_TYPE_FUNCTION) {
    for (int i = 0; i < type->param_count; ++i)
      ps_type_clear_record_layout_cache(psx_type_owned_param_mut(type, i));
  }
  if (type->kind == PSX_TYPE_STRUCT || type->kind == PSX_TYPE_UNION ||
      (type->kind == PSX_TYPE_ARRAY &&
       type_layout_contains_record_object(type->base))) {
    type->size = 0;
    type->align = 0;
  }
}

psx_type_t *ps_type_new_integer_in(
    arena_context_t *arena_context, token_kind_t scalar_kind,
    int size, int is_unsigned) {
  psx_type_t *type = ps_type_new_in(
      arena_context,
      scalar_kind == TK_BOOL ? PSX_TYPE_BOOL : PSX_TYPE_INTEGER);
  if (!type) return NULL;
  type->scalar_kind = canonical_integer_scalar_kind(scalar_kind, size);
  type->size = size;
  type->align = size > 0 ? size : 1;
  if (type->align > 8) type->align = 8;
  type->is_unsigned = is_unsigned ? 1 : 0;
  if (scalar_kind == TK_CHAR) type->is_plain_char = 1;
  return type;
}

psx_type_t *ps_type_new_enum_in(
    arena_context_t *arena_context, char *tag_name, int tag_len,
    int tag_scope_depth_p1, int size) {
  psx_type_t *type = ps_type_new_integer_in(
      arena_context, TK_ENUM, size > 0 ? size : 4, 0);
  if (!type) return NULL;
  type->tag_kind = TK_ENUM;
  type->tag_name = tag_name;
  type->tag_len = tag_len;
  type->tag_scope_depth_p1 = tag_scope_depth_p1;
  return type;
}

psx_type_t *ps_type_new_float_in(
    arena_context_t *arena_context, tk_float_kind_t fp_kind, int size) {
  psx_type_t *type = ps_type_new_in(arena_context, PSX_TYPE_FLOAT);
  if (!type) return NULL;
  type->fp_kind = fp_kind;
  type->size = size;
  type->align = size > 0 ? size : 1;
  if (type->align > 8) type->align = 8;
  if (fp_kind == TK_FLOAT_KIND_LONG_DOUBLE) type->is_long_double = 1;
  return type;
}

static int type_integer_promotion_size(const psx_type_t *type) {
  int size = ps_type_sizeof(type);
  if (size <= 0) return 4;
  return size < 4 ? 4 : size;
}

int ps_type_integer_rank(const psx_type_t *type) {
  if (!type) return 0;
  if (type->kind == PSX_TYPE_BOOL) return 0;
  if (type->kind != PSX_TYPE_INTEGER) return 0;
  if (type->is_plain_char) return 1;
  if (type->is_long_long) return 5;
  switch (type->scalar_kind) {
    case TK_CHAR:
      return 1;
    case TK_SHORT:
      return 2;
    case TK_INT:
    case TK_ENUM:
      return 3;
    case TK_LONG:
      return 4;
    default:
      return 0;
  }
}

int ps_type_character_code_unit_width(const psx_type_t *type) {
  if (!type || type->kind != PSX_TYPE_INTEGER ||
      type->tag_kind == TK_ENUM)
    return 0;
  switch (type->scalar_kind) {
    case TK_CHAR:
      return 1;
    case TK_SHORT:
      return 2;
    case TK_INT:
      return 4;
    default:
      return 0;
  }
}

int ps_type_integer_promotion_is_unsigned(const psx_type_t *type) {
  if (!type || (type->kind != PSX_TYPE_BOOL &&
                type->kind != PSX_TYPE_INTEGER)) {
    return 0;
  }
  return ps_type_integer_rank(type) >= 3 && ps_type_is_unsigned(type);
}

int ps_type_usual_arithmetic_result_is_unsigned(
    const psx_type_t *lhs, const psx_type_t *rhs) {
  if ((lhs && (lhs->kind == PSX_TYPE_FLOAT ||
               lhs->kind == PSX_TYPE_COMPLEX)) ||
      (rhs && (rhs->kind == PSX_TYPE_FLOAT ||
               rhs->kind == PSX_TYPE_COMPLEX))) {
    return 0;
  }
  int lhs_size = type_integer_promotion_size(lhs);
  int rhs_size = type_integer_promotion_size(rhs);
  int lhs_unsigned = ps_type_integer_promotion_is_unsigned(lhs);
  int rhs_unsigned = ps_type_integer_promotion_is_unsigned(rhs);
  if (lhs_unsigned == rhs_unsigned) return lhs_unsigned;
  int unsigned_size = lhs_unsigned ? lhs_size : rhs_size;
  int signed_size = lhs_unsigned ? rhs_size : lhs_size;
  return unsigned_size >= signed_size;
}

const psx_type_t *ps_type_usual_arithmetic_result_in(
    arena_context_t *arena_context,
    const psx_type_t *lhs, const psx_type_t *rhs,
    tk_float_kind_t fallback_fp_kind, int force_complex) {
  int result_is_complex =
      force_complex ||
      (lhs && lhs->kind == PSX_TYPE_COMPLEX) ||
      (rhs && rhs->kind == PSX_TYPE_COMPLEX);
  if (result_is_complex) {
    tk_float_kind_t fp = fallback_fp_kind;
    if (lhs && lhs->fp_kind > fp) fp = lhs->fp_kind;
    if (rhs && rhs->fp_kind > fp) fp = rhs->fp_kind;
    if (fp == TK_FLOAT_KIND_NONE) fp = TK_FLOAT_KIND_DOUBLE;
    int size = fp == TK_FLOAT_KIND_FLOAT ? 8 : 16;
    psx_type_t *type = ps_type_new_in(arena_context, PSX_TYPE_COMPLEX);
    type->fp_kind = fp;
    type->size = size;
    type->align = size >= 8 ? 8 : 4;
    return type;
  }

  if ((lhs && lhs->kind == PSX_TYPE_FLOAT) ||
      (rhs && rhs->kind == PSX_TYPE_FLOAT) ||
      fallback_fp_kind != TK_FLOAT_KIND_NONE) {
    tk_float_kind_t fp = fallback_fp_kind;
    if (lhs && lhs->fp_kind > fp) fp = lhs->fp_kind;
    if (rhs && rhs->fp_kind > fp) fp = rhs->fp_kind;
    if (fp == TK_FLOAT_KIND_NONE) fp = TK_FLOAT_KIND_DOUBLE;
    psx_type_t *type = ps_type_new_float_in(
        arena_context, fp, fp == TK_FLOAT_KIND_FLOAT ? 4 : 8);
    if ((lhs && lhs->is_long_double) || (rhs && rhs->is_long_double))
      type->is_long_double = 1;
    return type;
  }

  int lhs_size = type_integer_promotion_size(lhs);
  int rhs_size = type_integer_promotion_size(rhs);
  int result_unsigned =
      ps_type_usual_arithmetic_result_is_unsigned(lhs, rhs);
  int size = lhs_size > rhs_size ? lhs_size : rhs_size;
  psx_type_t *type = ps_type_new_integer_in(
      arena_context, TK_EOF, size, result_unsigned);
  type->is_long_long =
      (lhs && lhs->is_long_long) || (rhs && rhs->is_long_long);
  return type;
}

const psx_type_t *ps_type_binary_result_in(
    arena_context_t *arena_context, psx_type_binary_op_t op,
    const psx_type_t *lhs,
    const psx_type_t *rhs) {
  if (op == PSX_TYPE_BINARY_COMMA)
    return ps_type_clone_in(arena_context, rhs);
  if (op == PSX_TYPE_BINARY_COMPARE || op == PSX_TYPE_BINARY_LOGICAL)
    return ps_type_new_integer_in(arena_context, TK_INT, 4, 0);
  if (op == PSX_TYPE_BINARY_SHL || op == PSX_TYPE_BINARY_SHR)
    return ps_type_usual_arithmetic_result_in(
        arena_context, lhs, NULL, TK_FLOAT_KIND_NONE, 0);

  int lhs_pointer = ps_type_is_pointer_like(lhs);
  int rhs_pointer = ps_type_is_pointer_like(rhs);
  if (op == PSX_TYPE_BINARY_ADD && lhs_pointer != rhs_pointer)
    return (lhs_pointer ? lhs : rhs)->kind == PSX_TYPE_ARRAY
               ? ps_type_decay_array_in(
                     arena_context, lhs_pointer ? lhs : rhs)
               : ps_type_clone_in(
                     arena_context, lhs_pointer ? lhs : rhs);
  if (op == PSX_TYPE_BINARY_SUB) {
    if (lhs_pointer && rhs_pointer)
      return ps_type_new_integer_in(arena_context, TK_LONG, 8, 0);
    if (lhs_pointer)
      return lhs->kind == PSX_TYPE_ARRAY
                 ? ps_type_decay_array_in(arena_context, lhs)
                 : ps_type_clone_in(arena_context, lhs);
  }
  return ps_type_usual_arithmetic_result_in(
      arena_context, lhs, rhs, TK_FLOAT_KIND_NONE,
      (lhs && lhs->kind == PSX_TYPE_COMPLEX) ||
          (rhs && rhs->kind == PSX_TYPE_COMPLEX));
}

const psx_type_t *ps_type_conditional_result_in(
    arena_context_t *arena_context,
    const psx_type_t *then_type, const psx_type_t *else_type) {
  if (ps_type_is_pointer_like(then_type))
    return then_type->kind == PSX_TYPE_ARRAY
               ? ps_type_decay_array_in(arena_context, then_type)
               : ps_type_clone_in(arena_context, then_type);
  if (ps_type_is_pointer_like(else_type))
    return else_type->kind == PSX_TYPE_ARRAY
               ? ps_type_decay_array_in(arena_context, else_type)
               : ps_type_clone_in(arena_context, else_type);
  if (then_type && else_type && then_type->kind == else_type->kind &&
      ps_type_is_tag_aggregate(then_type))
    return ps_type_clone_in(arena_context, then_type);
  return ps_type_usual_arithmetic_result_in(
      arena_context, then_type, else_type, TK_FLOAT_KIND_NONE,
      (then_type && then_type->kind == PSX_TYPE_COMPLEX) ||
          (else_type && else_type->kind == PSX_TYPE_COMPLEX));
}

psx_type_t *ps_type_new_pointer_in(
    arena_context_t *arena_context, const psx_type_t *base) {
  psx_type_t *type = ps_type_new_in(arena_context, PSX_TYPE_POINTER);
  if (!type) return NULL;
  type->base = base;
  return type;
}

psx_type_t *ps_type_new_function_in(
    arena_context_t *arena_context, const psx_type_t *return_type) {
  psx_type_t *type = ps_type_new_in(arena_context, PSX_TYPE_FUNCTION);
  if (!type) return NULL;
  type->base = return_type;
  return type;
}

void ps_type_set_function_params_in(
    arena_context_t *arena_context, psx_type_t *function_type,
    const psx_type_t *const *param_types,
    int param_count, int is_variadic) {
  if (!function_type || function_type->kind != PSX_TYPE_FUNCTION) return;
  if (param_count < 0) param_count = 0;
  function_type->param_types = NULL;
  function_type->param_count = param_count;
  function_type->is_variadic_function = is_variadic ? 1 : 0;
  if (param_count == 0) return;
  const psx_type_t **params =
      arena_alloc_in(
          arena_context, (size_t)param_count * sizeof(*params));
  for (int i = 0; i < param_count; i++)
    params[i] = param_types
                    ? ps_type_clone_in(arena_context, param_types[i])
                    : NULL;
  function_type->param_types = params;
}

const psx_type_t *ps_type_derived_function(const psx_type_t *type) {
  while (type) {
    if (type->kind == PSX_TYPE_FUNCTION) return type;
    if (type->kind != PSX_TYPE_POINTER && type->kind != PSX_TYPE_ARRAY)
      return NULL;
    type = type->base;
  }
  return NULL;
}

const psx_type_t *ps_type_callable_function(const psx_type_t *type) {
  if (!type) return NULL;
  if (type->kind == PSX_TYPE_FUNCTION) return type;
  if (type->kind == PSX_TYPE_POINTER && type->base &&
      type->base->kind == PSX_TYPE_FUNCTION)
    return type->base;
  return NULL;
}

const psx_type_t *ps_type_function_return_type(const psx_type_t *type) {
  const psx_type_t *function = ps_type_callable_function(type);
  return function ? function->base : NULL;
}

psx_type_t *ps_type_new_array_in(
    arena_context_t *arena_context, const psx_type_t *base,
    int array_len, int size, int is_vla) {
  psx_type_t *type = ps_type_new_in(arena_context, PSX_TYPE_ARRAY);
  if (!type) return NULL;
  type->base = base;
  type->array_len = array_len;
  type->size = size;
  type->align = base && base->align > 0 ? base->align : 1;
  type->is_vla = is_vla ? 1 : 0;
  return type;
}

int ps_type_complete_array(psx_type_t *type, int array_len) {
  if (!type || type->kind != PSX_TYPE_ARRAY || type->is_vla ||
      array_len <= 0 || !type->base) return 0;
  int child_size = ps_type_sizeof(type->base);
  if (child_size <= 0 || array_len > INT_MAX / child_size) return 0;
  if (type->array_len > 0 && type->array_len != array_len) return 0;
  type->array_len = array_len;
  type->size = array_len * child_size;
  return 1;
}

int ps_type_is_incomplete_array(const psx_type_t *type) {
  return type && type->kind == PSX_TYPE_ARRAY &&
         type->array_len <= 0 && !type->is_vla;
}

psx_type_t *ps_type_clone_in(
    arena_context_t *arena_context, const psx_type_t *src) {
  if (!src) return NULL;
  psx_type_t *dst = ps_type_new_in(arena_context, src->kind);
  if (!dst) return NULL;
  *dst = *src;
  dst->param_types = NULL;
  dst->base = ps_type_clone_in(arena_context, src->base);
  if (src->param_count > 0) {
    const psx_type_t **params =
        arena_alloc_in(
            arena_context,
            (size_t)src->param_count * sizeof(*params));
    for (int i = 0; i < src->param_count; i++)
      params[i] = ps_type_clone_in(
          arena_context,
          src->param_types ? src->param_types[i] : NULL);
    dst->param_types = params;
  }
  return dst;
}

psx_type_t *ps_type_clone_persistent(const psx_type_t *src) {
  if (!src) return NULL;
  psx_type_t *dst = calloc(1, sizeof(psx_type_t));
  if (!dst) return NULL;
  *dst = *src;
  dst->param_types = NULL;
  dst->base = ps_type_clone_persistent(src->base);
  if (src->param_count > 0) {
    const psx_type_t **params = calloc(
        (size_t)src->param_count, sizeof(*params));
    if (!params) return NULL;
    for (int i = 0; i < src->param_count; i++)
      params[i] = ps_type_clone_persistent(
          src->param_types ? src->param_types[i] : NULL);
    dst->param_types = params;
  }
  return dst;
}

psx_type_t *ps_type_wrap_array_dims_in(
    arena_context_t *arena_context, psx_type_t *base,
    const int *dims, int dim_count) {
  if (!base || !dims || dim_count <= 0) return base;
  const psx_type_t *child = base;
  psx_type_t *result = NULL;
  int child_size = ps_type_sizeof(base);
  if (child_size <= 0) child_size = 1;
  for (int i = dim_count - 1; i >= 0; i--) {
    int len = dims[i];
    int size = len > 0 ? len * child_size : 0;
    result = ps_type_new_array_in(
        arena_context, child, len, size, 0);
    child = result;
    child_size = size;
  }
  return result;
}

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
    psx_declarator_op_t *ops =
        arena_alloc_in(
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
    int is_const_qualified,
    int is_volatile_qualified) {
  psx_declarator_op_t *op =
      declarator_shape_append(
          arena_context, shape, PSX_DECL_OP_POINTER);
  if (!op) return 0;
  op->is_const_qualified = is_const_qualified ? 1u : 0u;
  op->is_volatile_qualified = is_volatile_qualified ? 1u : 0u;
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
  psx_declarator_op_t *op =
      declarator_shape_append(
          arena_context, shape, PSX_DECL_OP_ARRAY);
  if (!op) return 0;
  op->array_len = array_len;
  op->is_incomplete_array = is_incomplete ? 1u : 0u;
  return 1;
}

int ps_declarator_shape_append_vla_array_in(
    arena_context_t *arena_context, psx_declarator_shape_t *shape) {
  psx_declarator_op_t *op =
      declarator_shape_append(
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

int ps_declarator_op_set_function_params_in(
    arena_context_t *arena_context, psx_declarator_op_t *op,
    const psx_type_t *const *param_types,
    int param_count, int is_variadic) {
  if (!op || op->kind != PSX_DECL_OP_FUNCTION || param_count < 0)
    return 0;
  op->function_param_types = NULL;
  op->function_param_count = param_count;
  op->function_is_variadic = is_variadic ? 1 : 0;
  op->has_canonical_function_params = 1;
  if (param_count == 0) return 1;
  op->function_param_types =
      arena_alloc_in(
          arena_context,
          (size_t)param_count * sizeof(*op->function_param_types));
  for (int i = 0; i < param_count; i++)
    op->function_param_types[i] = param_types ? param_types[i] : NULL;
  return 1;
}

int ps_declarator_shape_set_array_bound(
    psx_declarator_shape_t *shape, int op_index,
    int array_len, int is_vla) {
  if (!shape || op_index < 0 || op_index >= shape->count ||
      shape->ops[op_index].kind != PSX_DECL_OP_ARRAY) {
    return 0;
  }
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

static int declarator_shape_append_shape(
    arena_context_t *arena_context, psx_declarator_shape_t *shape,
    const psx_declarator_shape_t *suffix) {
  if (!shape || !suffix || suffix->count < 0) return 0;
  int suffix_count = suffix->count;
  for (int i = 0; i < suffix_count; i++) {
    const psx_declarator_op_t *op = &suffix->ops[i];
    int appended = 0;
    if (op->kind == PSX_DECL_OP_POINTER) {
      appended = ps_declarator_shape_append_pointer_in(
          arena_context, shape,
          op->is_const_qualified, op->is_volatile_qualified);
    } else if (op->kind == PSX_DECL_OP_ARRAY) {
      if (op->is_vla_array)
        appended = ps_declarator_shape_append_vla_array_in(
            arena_context, shape);
      else
        appended = ps_declarator_shape_append_array_ex_in(
            arena_context, shape,
            op->array_len, op->is_incomplete_array);
    } else if (op->kind == PSX_DECL_OP_FUNCTION) {
      appended = ps_declarator_shape_append_function_in(
          arena_context, shape);
      if (appended && op->has_canonical_function_params) {
        psx_declarator_op_t *copy = &shape->ops[shape->count - 1];
        appended = ps_declarator_op_set_function_params_in(
            arena_context, copy,
            op->function_param_types, op->function_param_count,
            op->function_is_variadic);
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
  return declarator_shape_append_shape(arena_context, dst, src);
}

int ps_declarator_shape_count_ops(
    const psx_declarator_shape_t *shape, psx_declarator_op_kind_t kind) {
  if (!shape) return 0;
  int count = 0;
  for (int i = 0; i < shape->count; i++) {
    if (shape->ops[i].kind == kind) count++;
  }
  return count;
}

psx_type_t *ps_type_apply_declarator_shape_in(
    arena_context_t *arena_context, psx_type_t *base,
    const psx_declarator_shape_t *shape) {
  if (!base || !shape) return base;
  psx_type_t *type = base;
  for (int i = shape->count - 1; i >= 0; i--) {
    const psx_declarator_op_t *op = &shape->ops[i];
    if (op->kind == PSX_DECL_OP_POINTER) {
      type = ps_type_new_pointer_in(arena_context, type);
      psx_type_qualifiers_t qualifiers = PSX_TYPE_QUALIFIER_NONE;
      if (op->is_const_qualified) qualifiers |= PSX_TYPE_QUALIFIER_CONST;
      if (op->is_volatile_qualified)
        qualifiers |= PSX_TYPE_QUALIFIER_VOLATILE;
      ps_type_add_qualifiers(type, qualifiers);
    } else if (op->kind == PSX_DECL_OP_ARRAY) {
      int elem_size = ps_type_sizeof(type);
      if (elem_size <= 0) elem_size = ps_type_deref_size(type);
      if (elem_size <= 0) elem_size = 1;
      int total_size = op->array_len > 0 ? op->array_len * elem_size : 0;
      type = ps_type_new_array_in(
          arena_context, type, op->array_len,
          total_size, op->is_vla_array);
    } else if (op->kind == PSX_DECL_OP_FUNCTION) {
      type = ps_type_new_function_in(arena_context, type);
      if (op->has_canonical_function_params) {
        ps_type_set_function_params_in(
            arena_context, type, op->function_param_types,
            op->function_param_count, op->function_is_variadic);
      }
    }
  }
  return type;
}

psx_type_t *ps_type_adjust_parameter_type_in(
    arena_context_t *arena_context, psx_type_t *type) {
  if (!type) return NULL;
  if (type->kind == PSX_TYPE_ARRAY) {
    psx_type_t *adjusted = ps_type_new_pointer_in(
        arena_context, type->base);
    return adjusted;
  }
  if (type->kind == PSX_TYPE_FUNCTION)
    return ps_type_new_pointer_in(arena_context, type);
  return type;
}

psx_type_kind_t ps_type_kind_from_tag_kind(token_kind_t tag_kind) {
  switch (tag_kind) {
    case TK_STRUCT: return PSX_TYPE_STRUCT;
    case TK_UNION: return PSX_TYPE_UNION;
    default: return PSX_TYPE_INVALID;
  }
}

psx_type_t *ps_type_new_tag_in(
    arena_context_t *arena_context, token_kind_t tag_kind,
    char *tag_name, int tag_len, int tag_scope_depth_p1, int size) {
  psx_type_t *type = ps_type_new_in(
      arena_context, ps_type_kind_from_tag_kind(tag_kind));
  if (!type) return NULL;
  type->tag_kind = tag_kind;
  type->tag_name = tag_name;
  type->tag_len = tag_len;
  type->tag_scope_depth_p1 = tag_scope_depth_p1;
  type->size = size;
  type->align = size >= 8 ? 8 : (size >= 4 ? 4 : (size >= 2 ? 2 : 1));
  return type;
}

int ps_type_sizeof(const psx_type_t *type) {
  if (!type) return 0;
  if (type->kind == PSX_TYPE_VOID || type->kind == PSX_TYPE_FUNCTION) return 0;
  if (type->kind == PSX_TYPE_POINTER) return 8;
  return type->size;
}

int ps_type_deref_size(const psx_type_t *type) {
  if (!type) return 0;
  if (type->kind == PSX_TYPE_POINTER || type->kind == PSX_TYPE_ARRAY) {
    if (type->base && type->base->kind == PSX_TYPE_ARRAY &&
        ps_type_is_tag_aggregate(ps_type_array_leaf_type(type->base)))
      return 0;
    return ps_type_sizeof(type->base);
  }
  return 0;
}

const psx_type_t *ps_type_array_leaf_type(const psx_type_t *type) {
  while (type && type->kind == PSX_TYPE_ARRAY) type = type->base;
  return type;
}

const psx_type_t *ps_type_pointee_value_type(const psx_type_t *type) {
  if (!type || !type->base ||
      (type->kind != PSX_TYPE_POINTER && type->kind != PSX_TYPE_ARRAY)) {
    return NULL;
  }
  return ps_type_array_leaf_type(type->base);
}

int ps_type_pointee_value_size(const psx_type_t *type) {
  return ps_type_sizeof(ps_type_pointee_value_type(type));
}

const psx_type_t *ps_type_derived_leaf_type(const psx_type_t *type) {
  while (type &&
         (type->kind == PSX_TYPE_POINTER || type->kind == PSX_TYPE_ARRAY)) {
    type = type->base;
  }
  return type;
}

int ps_type_array_rank(const psx_type_t *type) {
  int rank = 0;
  while (type && type->kind == PSX_TYPE_ARRAY) {
    rank++;
    type = type->base;
  }
  return rank;
}

int ps_type_array_dimension(const psx_type_t *type, int index) {
  if (index < 0) return 0;
  while (type && type->kind == PSX_TYPE_ARRAY) {
    if (index-- == 0) return type->array_len;
    type = type->base;
  }
  return 0;
}

int ps_type_array_flat_element_count(const psx_type_t *type) {
  if (!type || type->kind != PSX_TYPE_ARRAY) return 0;
  int count = 1;
  while (type && type->kind == PSX_TYPE_ARRAY) {
    if (type->array_len <= 0 || count > INT_MAX / type->array_len) return 0;
    count *= type->array_len;
    type = type->base;
  }
  return count;
}

int ps_type_array_scalar_element_size(const psx_type_t *type) {
  if (!type || type->kind != PSX_TYPE_ARRAY) return 0;
  return ps_type_sizeof(ps_type_array_leaf_type(type));
}

int ps_type_array_subscript_stride_elements(const psx_type_t *type,
                                             int depth) {
  if (!type || type->kind != PSX_TYPE_ARRAY || depth < 0) return 0;
  while (depth-- > 0) {
    type = type->base;
    if (!type || type->kind != PSX_TYPE_ARRAY) return 0;
  }
  const psx_type_t *selected = type->base;
  if (!selected) return 0;
  if (selected->kind != PSX_TYPE_ARRAY) return 1;
  return ps_type_array_flat_element_count(selected);
}

int ps_type_array_subscript_stride_bytes(const psx_type_t *type, int depth) {
  if (!type || type->kind != PSX_TYPE_ARRAY || depth < 0) return 0;
  while (depth-- > 0) {
    type = type->base;
    if (!type || type->kind != PSX_TYPE_ARRAY) return 0;
  }
  return ps_type_sizeof(type->base);
}

const psx_type_t *ps_type_address_result_in(
    arena_context_t *arena_context, const psx_type_t *type) {
  if (!type) return NULL;
  return ps_type_new_pointer_in(arena_context, type);
}

const psx_type_t *ps_type_decay_array_in(
    arena_context_t *arena_context, const psx_type_t *type) {
  if (!type || type->kind != PSX_TYPE_ARRAY || !type->base) return NULL;
  return ps_type_new_pointer_in(arena_context, type->base);
}

const psx_type_t *ps_type_dereference_result(const psx_type_t *type) {
  if (!type || !type->base ||
      (type->kind != PSX_TYPE_POINTER && type->kind != PSX_TYPE_ARRAY)) {
    return NULL;
  }
  return type->base;
}

const psx_type_t *ps_type_subscript_result_in(
    arena_context_t *arena_context, const psx_type_t *type) {
  if (!type || !type->base ||
      (type->kind != PSX_TYPE_POINTER && type->kind != PSX_TYPE_ARRAY)) {
    return NULL;
  }
  return ps_type_clone_in(arena_context, type->base);
}

int ps_type_subscript_static_stride(const psx_type_t *type) {
  if (!type || !type->base ||
      (type->kind != PSX_TYPE_POINTER && type->kind != PSX_TYPE_ARRAY)) {
    return 0;
  }
  return ps_type_sizeof(type->base);
}

int ps_type_is_pointer(const psx_type_t *type) {
  return type && type->kind == PSX_TYPE_POINTER;
}

int ps_type_is_pointer_like(const psx_type_t *type) {
  return type &&
         (type->kind == PSX_TYPE_POINTER || type->kind == PSX_TYPE_ARRAY);
}

int ps_type_contains_vla_array(const psx_type_t *type) {
  for (; type; type = type->base) {
    if (type->kind == PSX_TYPE_ARRAY && type->is_vla) return 1;
  }
  return 0;
}

int ps_type_is_unsigned(const psx_type_t *type) {
  if (!type) return 0;
  return type->is_unsigned ? 1 : 0;
}

psx_type_qualifiers_t ps_type_qualifiers(const psx_type_t *type) {
  return type ? type->qualifiers : PSX_TYPE_QUALIFIER_NONE;
}

int ps_type_has_qualifier(const psx_type_t *type,
                          psx_type_qualifiers_t qualifier) {
  return qualifier != PSX_TYPE_QUALIFIER_NONE &&
         (ps_type_qualifiers(type) & qualifier) == qualifier;
}

int ps_type_is_scalar(const psx_type_t *type) {
  if (!type) return 0;
  switch (type->kind) {
    case PSX_TYPE_BOOL:
    case PSX_TYPE_INTEGER:
    case PSX_TYPE_FLOAT:
    case PSX_TYPE_POINTER:
      return 1;
    default:
      return 0;
  }
}

int ps_type_shape_matches(const psx_type_t *a, const psx_type_t *b) {
  if (a == b) return 1;
  if (!a || !b || a->kind != b->kind) return 0;
  if (a->qualifiers != b->qualifiers ||
      a->is_unsigned != b->is_unsigned ||
      a->is_long_long != b->is_long_long ||
      a->is_plain_char != b->is_plain_char ||
      a->is_long_double != b->is_long_double) {
    return 0;
  }
  switch (a->kind) {
    case PSX_TYPE_BOOL:
    case PSX_TYPE_INTEGER:
      if ((a->scalar_kind == TK_ENUM || b->scalar_kind == TK_ENUM) &&
          !ps_type_tag_identity_matches(a, b))
        return 0;
      return a->scalar_kind == b->scalar_kind;
    case PSX_TYPE_FLOAT:
    case PSX_TYPE_COMPLEX:
      return a->fp_kind == b->fp_kind;
    case PSX_TYPE_POINTER:
      return ps_type_shape_matches(a->base, b->base);
    case PSX_TYPE_ARRAY:
      return a->array_len == b->array_len && a->is_vla == b->is_vla &&
             ps_type_shape_matches(a->base, b->base);
    case PSX_TYPE_STRUCT:
    case PSX_TYPE_UNION:
      return ps_type_tag_identity_matches(a, b);
    case PSX_TYPE_FUNCTION:
      if (a->param_count != b->param_count ||
          a->is_variadic_function != b->is_variadic_function ||
          !ps_type_shape_matches(a->base, b->base)) {
        return 0;
      }
      if (a->param_count > 0 && (!a->param_types || !b->param_types))
        return 0;
      for (int i = 0; i < a->param_count; i++) {
        if (!ps_type_shape_matches(a->param_types[i], b->param_types[i]))
          return 0;
      }
      return 1;
    case PSX_TYPE_VOID:
    case PSX_TYPE_INVALID:
      return 1;
    default:
      return 0;
  }
}

static int semantic_type_matches(
    const psx_type_t *a, const psx_type_t *b, int compare_qualifiers) {
  if (a == b) return 1;
  if (!a || !b || a->kind != b->kind) return 0;
  if (compare_qualifiers && a->qualifiers != b->qualifiers) return 0;
  switch (a->kind) {
    case PSX_TYPE_BOOL:
      return a->is_unsigned == b->is_unsigned;
    case PSX_TYPE_INTEGER:
      if (a->scalar_kind == TK_ENUM || b->scalar_kind == TK_ENUM ||
          a->tag_kind == TK_ENUM || b->tag_kind == TK_ENUM) {
        return ps_type_tag_identity_matches(a, b);
      }
      return a->is_unsigned == b->is_unsigned &&
             a->is_plain_char == b->is_plain_char &&
             ps_type_integer_rank(a) == ps_type_integer_rank(b);
    case PSX_TYPE_FLOAT:
    case PSX_TYPE_COMPLEX:
      return a->fp_kind == b->fp_kind &&
             a->is_long_double == b->is_long_double;
    case PSX_TYPE_POINTER:
      return semantic_type_matches(a->base, b->base, 1);
    case PSX_TYPE_ARRAY:
      return a->array_len == b->array_len && a->is_vla == b->is_vla &&
             semantic_type_matches(a->base, b->base, 1);
    case PSX_TYPE_STRUCT:
    case PSX_TYPE_UNION:
      return ps_type_tag_identity_matches(a, b);
    case PSX_TYPE_FUNCTION:
      if (a->param_count != b->param_count ||
          a->is_variadic_function != b->is_variadic_function ||
          !semantic_type_matches(a->base, b->base, 1)) {
        return 0;
      }
      if (a->param_count > 0 && (!a->param_types || !b->param_types))
        return 0;
      for (int i = 0; i < a->param_count; i++) {
        if (!semantic_type_matches(a->param_types[i], b->param_types[i], 1))
          return 0;
      }
      return 1;
    case PSX_TYPE_VOID:
    case PSX_TYPE_INVALID:
      return 1;
    default:
      return 0;
  }
}

int ps_type_unqualified_semantic_matches(
    const psx_type_t *a, const psx_type_t *b) {
  return semantic_type_matches(a, b, 0);
}

static int type_derivation_to_function_matches(const psx_type_t *a,
                                               const psx_type_t *b) {
  while (a && b && a->kind != PSX_TYPE_FUNCTION &&
         b->kind != PSX_TYPE_FUNCTION) {
    if (a->kind != b->kind ||
        (a->kind != PSX_TYPE_POINTER && a->kind != PSX_TYPE_ARRAY) ||
        (a->qualifiers & (PSX_TYPE_QUALIFIER_CONST |
                          PSX_TYPE_QUALIFIER_VOLATILE)) !=
            (b->qualifiers & (PSX_TYPE_QUALIFIER_CONST |
                              PSX_TYPE_QUALIFIER_VOLATILE))) {
      return 0;
    }
    if (a->kind == PSX_TYPE_ARRAY && a->array_len != b->array_len) return 0;
    a = a->base;
    b = b->base;
  }
  return a && b && a->kind == PSX_TYPE_FUNCTION &&
         b->kind == PSX_TYPE_FUNCTION;
}

typedef struct {
  char *out;
  size_t cap;
  size_t len;
  int failed;
} canonical_sig_writer_t;

typedef struct canonical_sig_path_t canonical_sig_path_t;
struct canonical_sig_path_t {
  const psx_type_t *type;
  const canonical_sig_path_t *parent;
};

static int canonical_sig_path_contains(
    const canonical_sig_path_t *path, const psx_type_t *type) {
  for (const canonical_sig_path_t *current = path; current;
       current = current->parent) {
    if (current->type == type) return 1;
  }
  return 0;
}

static void canonical_sig_bytes(canonical_sig_writer_t *w,
                                const char *s, size_t len) {
  if (!w || w->failed || !s) return;
  if (len > (size_t)INT_MAX || w->len > (size_t)INT_MAX - len) {
    w->failed = 1;
    return;
  }
  if (w->out && w->cap > 0 && w->len < w->cap - 1) {
    size_t writable = w->cap - 1 - w->len;
    if (writable > len) writable = len;
    memcpy(w->out + w->len, s, writable);
  }
  w->len += len;
}

static void canonical_sig_lit(canonical_sig_writer_t *w, const char *s) {
  canonical_sig_bytes(w, s, strlen(s));
}

static void canonical_sig_uint(canonical_sig_writer_t *w, unsigned int value) {
  char digits[16];
  int count = 0;
  do {
    digits[count++] = (char)('0' + value % 10u);
    value /= 10u;
  } while (value != 0 && count < (int)sizeof(digits));
  while (count > 0) canonical_sig_bytes(w, &digits[--count], 1);
}

static void canonical_sig_type(canonical_sig_writer_t *w,
                               const psx_type_t *type,
                               const canonical_sig_path_t *path) {
  if (!type || canonical_sig_path_contains(path, type)) {
    w->failed = 1;
    return;
  }
  canonical_sig_path_t current_path = {
      .type = type,
      .parent = path,
  };
  if (ps_type_has_qualifier(type, PSX_TYPE_QUALIFIER_CONST))
    canonical_sig_lit(w, "k");
  if (ps_type_has_qualifier(type, PSX_TYPE_QUALIFIER_VOLATILE))
    canonical_sig_lit(w, "V");
  if (ps_type_has_qualifier(type, PSX_TYPE_QUALIFIER_ATOMIC))
    canonical_sig_lit(w, "A");

  switch (type->kind) {
    case PSX_TYPE_VOID:
      canonical_sig_lit(w, "v");
      return;
    case PSX_TYPE_BOOL:
      canonical_sig_lit(w, "b");
      return;
    case PSX_TYPE_INTEGER: {
      unsigned int bits = (unsigned int)(ps_type_sizeof(type) > 0
                                             ? ps_type_sizeof(type) * 8
                                             : 32);
      if (type->scalar_kind == TK_ENUM || type->tag_kind == TK_ENUM) {
        canonical_sig_lit(w, "e{");
        canonical_sig_uint(w, (unsigned int)(type->tag_len > 0 ? type->tag_len : 0));
        canonical_sig_lit(w, ":");
        if (type->tag_len > 0)
          canonical_sig_bytes(w, type->tag_name, (size_t)type->tag_len);
        canonical_sig_lit(w, "}");
      } else if (type->is_plain_char) {
        canonical_sig_lit(w, "c");
        canonical_sig_uint(w, bits);
      } else if (type->is_long_long) {
        canonical_sig_lit(w, type->is_unsigned ? "ull" : "ll");
        canonical_sig_uint(w, bits);
      } else if (type->scalar_kind == TK_LONG) {
        canonical_sig_lit(w, type->is_unsigned ? "ul" : "l");
        canonical_sig_uint(w, bits);
      } else {
        canonical_sig_lit(w, type->is_unsigned ? "u" : "i");
        canonical_sig_uint(w, bits);
      }
      return;
    }
    case PSX_TYPE_FLOAT:
      canonical_sig_lit(w, "f");
      canonical_sig_uint(w, (unsigned int)(ps_type_sizeof(type) * 8));
      return;
    case PSX_TYPE_COMPLEX:
      canonical_sig_lit(w, "x");
      canonical_sig_uint(w, (unsigned int)(ps_type_sizeof(type) * 8));
      return;
    case PSX_TYPE_POINTER:
      canonical_sig_lit(w, "p<");
      canonical_sig_type(w, type->base, &current_path);
      canonical_sig_lit(w, ">");
      return;
    case PSX_TYPE_ARRAY:
      canonical_sig_lit(w, "a");
      canonical_sig_uint(w, (unsigned int)(type->array_len > 0 ? type->array_len : 0));
      canonical_sig_lit(w, "<");
      canonical_sig_type(w, type->base, &current_path);
      canonical_sig_lit(w, ">");
      return;
    case PSX_TYPE_FUNCTION:
      if (type->param_count < 0 ||
          (type->param_count > 0 && !type->param_types)) {
        w->failed = 1;
        return;
      }
      canonical_sig_type(w, type->base, &current_path);
      canonical_sig_lit(w, "(");
      for (int i = 0; i < type->param_count; i++) {
        if (i > 0) canonical_sig_lit(w, ",");
        canonical_sig_type(w, type->param_types[i], &current_path);
      }
      if (type->is_variadic_function) {
        if (type->param_count > 0) canonical_sig_lit(w, ",");
        canonical_sig_lit(w, "...");
      }
      canonical_sig_lit(w, ")");
      return;
    case PSX_TYPE_STRUCT:
    case PSX_TYPE_UNION:
      canonical_sig_lit(w, type->kind == PSX_TYPE_STRUCT ? "s{" : "u{");
      canonical_sig_uint(w, (unsigned int)(type->tag_len > 0 ? type->tag_len : 0));
      canonical_sig_lit(w, ":");
      if (type->tag_len > 0)
        canonical_sig_bytes(w, type->tag_name, (size_t)type->tag_len);
      canonical_sig_lit(w, "}");
      return;
    default:
      w->failed = 1;
      return;
  }
}

int ps_type_format_canonical_signature(const psx_type_t *type,
                                       char *out, size_t out_size) {
  canonical_sig_writer_t writer = {out, out_size, 0, 0};
  canonical_sig_type(&writer, type, NULL);
  if (out && out_size > 0) {
    size_t terminator = writer.len < out_size ? writer.len : out_size - 1;
    out[terminator] = '\0';
  }
  if (writer.failed || writer.len > (size_t)INT_MAX) return -1;
  return (int)writer.len;
}

int ps_type_generic_matches(const psx_type_t *control,
                             const psx_type_t *association) {
  if (!control || !association) return 0;
  psx_type_t unqualified_control = *control;
  psx_type_t unqualified_association = *association;
  ps_type_remove_qualifiers(
      &unqualified_control,
      PSX_TYPE_QUALIFIER_CONST | PSX_TYPE_QUALIFIER_VOLATILE);
  ps_type_remove_qualifiers(
      &unqualified_association,
      PSX_TYPE_QUALIFIER_CONST | PSX_TYPE_QUALIFIER_VOLATILE);
  control = &unqualified_control;
  association = &unqualified_association;
  const psx_type_t *control_function = ps_type_derived_function(control);
  const psx_type_t *association_function =
      ps_type_derived_function(association);
  if (!control_function && !association_function)
    return ps_type_shape_matches(control, association);
  if (!control_function || !association_function) return 0;
  if (!type_derivation_to_function_matches(control, association)) return 0;
  return ps_type_shape_matches(control_function, association_function);
}

const psx_type_t *ps_type_generic_control_in(
    arena_context_t *arena_context, const psx_type_t *control) {
  psx_type_t *type = ps_type_clone_in(arena_context, control);
  if (!type) return NULL;
  if (type->kind == PSX_TYPE_ARRAY) {
    type = ps_type_new_pointer_in(arena_context, type->base);
  } else if (type->kind == PSX_TYPE_FUNCTION) {
    type = ps_type_new_pointer_in(arena_context, type);
  }
  ps_type_normalize_integer_identity(type);
  return type;
}

int ps_type_generic_select_index_in(
    arena_context_t *arena_context, const psx_type_t *control,
    const psx_type_t *const *association_types,
    const unsigned char *is_default, int association_count) {
  (void)arena_context;
  return ps_type_generic_select_index(
      control, association_types, is_default, association_count);
}

int ps_type_generic_select_index(
    const psx_type_t *control,
    const psx_type_t *const *association_types,
    const unsigned char *is_default, int association_count) {
  if (!control || !association_types || association_count <= 0) return -1;
  psx_type_t normalized = *control;
  psx_type_t decayed;
  if (normalized.kind == PSX_TYPE_ARRAY ||
      normalized.kind == PSX_TYPE_FUNCTION) {
    memset(&decayed, 0, sizeof(decayed));
    decayed.kind = PSX_TYPE_POINTER;
    decayed.base = normalized.kind == PSX_TYPE_ARRAY
                       ? normalized.base
                       : control;
    decayed.size = 8;
    decayed.align = 8;
    normalized = decayed;
  } else if (normalized.kind == PSX_TYPE_INTEGER) {
    if (normalized.tag_kind == TK_ENUM) normalized.scalar_kind = TK_ENUM;
    else
      normalized.scalar_kind = canonical_integer_scalar_kind(
          normalized.scalar_kind, normalized.size);
  }
  int default_index = -1;
  for (int i = 0; i < association_count; i++) {
    if (is_default && is_default[i]) {
      if (default_index < 0) default_index = i;
      continue;
    }
    if (ps_type_generic_matches(&normalized, association_types[i])) return i;
  }
  return default_index;
}

int ps_type_is_tag_aggregate(const psx_type_t *type) {
  if (!type) return 0;
  return type->kind == PSX_TYPE_STRUCT || type->kind == PSX_TYPE_UNION;
}

const psx_type_t *ps_type_find_aggregate_object_type(
    const psx_type_t *type) {
  for (const psx_type_t *current = type; current;
       current = current->base) {
    if (ps_type_is_tag_aggregate(current)) return current;
  }
  return NULL;
}

const tag_member_info_t *ps_type_find_aggregate_member(
    const psx_type_t *type, token_kind_t tag_kind,
    const char *tag_name, int tag_len,
    const char *member_name, int member_len) {
  for (const psx_type_t *cursor = type; cursor; cursor = cursor->base) {
    if (!ps_type_is_tag_aggregate(cursor) ||
        cursor->tag_kind != tag_kind || cursor->tag_len != tag_len)
      continue;
    if (tag_len > 0 &&
        (!cursor->tag_name || !tag_name ||
         strncmp(cursor->tag_name, tag_name, (size_t)tag_len) != 0))
      continue;
    const psx_aggregate_definition_t *definition =
        cursor->aggregate_definition;
    if (!definition) return NULL;
    for (int i = 0; i < definition->member_count; i++) {
      const tag_member_info_t *member = &definition->members[i];
      if (member->len == member_len && member->name && member_name &&
          strncmp(member->name, member_name, (size_t)member_len) == 0)
        return member;
    }
    return NULL;
  }
  return NULL;
}

static int psx_type_is_pointer_view_type(const psx_type_t *type) {
  return type && (type->kind == PSX_TYPE_POINTER || type->kind == PSX_TYPE_ARRAY);
}

int ps_type_pointer_depth(const psx_type_t *type) {
  int depth = 0;
  while (type && type->kind == PSX_TYPE_POINTER) {
    depth++;
    type = type->base;
  }
  return depth;
}

static int type_structural_base_deref_size(const psx_type_t *type,
                                           int *structurally_known) {
  if (structurally_known) *structurally_known = 0;
  if (!psx_type_is_pointer_view_type(type)) return 0;
  const psx_type_t *cur = ps_type_derived_leaf_type(type);
  if (!cur) return 0;
  if (structurally_known) *structurally_known = 1;
  return ps_type_sizeof(cur);
}

int ps_type_pointer_view_structural_base_deref_size(const psx_type_t *type) {
  int structurally_known = 0;
  int base_deref_size =
      type_structural_base_deref_size(type, &structurally_known);
  return structurally_known && base_deref_size > 0 ? base_deref_size : 0;
}

int ps_type_pointer_view_structural_ptr_array_pointee_bytes(
    const psx_type_t *type) {
  if (!type) return 0;
  while (type && type->kind == PSX_TYPE_ARRAY) type = type->base;
  if (!type || type->kind != PSX_TYPE_POINTER || !type->base ||
      type->base->kind != PSX_TYPE_ARRAY) {
    return 0;
  }
  if (ps_type_is_tag_aggregate(ps_type_array_leaf_type(type->base)))
    return 0;
  int bytes = ps_type_sizeof(type->base);
  if (bytes <= 0) bytes = ps_type_deref_size(type);
  return bytes > 0 ? bytes : 0;
}

void ps_type_set_decl_spec_qualifiers(psx_type_t *type,
                                       int is_const_qualified,
                                       int is_volatile_qualified) {
  while (type && type->kind == PSX_TYPE_ARRAY)
    type = psx_type_owned_base_mut(type);
  if (!type || type->kind == PSX_TYPE_FUNCTION) return;
  psx_type_qualifiers_t qualifiers = PSX_TYPE_QUALIFIER_NONE;
  if (is_const_qualified) qualifiers |= PSX_TYPE_QUALIFIER_CONST;
  if (is_volatile_qualified) qualifiers |= PSX_TYPE_QUALIFIER_VOLATILE;
  ps_type_add_qualifiers(type, qualifiers);
}

void ps_type_add_qualifiers(psx_type_t *type,
                            psx_type_qualifiers_t qualifiers) {
  if (!type) return;
  type->qualifiers |=
      qualifiers & (PSX_TYPE_QUALIFIER_CONST | PSX_TYPE_QUALIFIER_VOLATILE |
                    PSX_TYPE_QUALIFIER_ATOMIC);
}

void ps_type_remove_qualifiers(psx_type_t *type,
                               psx_type_qualifiers_t qualifiers) {
  if (!type) return;
  type->qualifiers &=
      ~(qualifiers & (PSX_TYPE_QUALIFIER_CONST |
                      PSX_TYPE_QUALIFIER_VOLATILE |
                      PSX_TYPE_QUALIFIER_ATOMIC));
}
