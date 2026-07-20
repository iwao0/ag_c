#include "type.h"
#include "type_builder.h"
#include "declarator_shape_builder.h"
#include "arena.h"
#include "type_owned_internal.h"
#include "../semantic/type_identity.h"
#include "../target_info.h"
#include "../type_layout.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

const psx_type_t *psx_record_member_decl_type(
    const psx_record_member_decl_t *member) {
  if (!member || !member->decl_type_table ||
      member->decl_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return NULL;
  return psx_semantic_type_table_lookup_qual_type(
      member->decl_type_table, member->decl_qual_type);
}

int ps_type_tag_identity_matches(const psx_type_t *a,
                                 const psx_type_t *b) {
  if (!a || !b || ps_type_tag_token_kind(a) != ps_type_tag_token_kind(b) ||
      a->tag_len != b->tag_len)
    return 0;
  psx_record_id_t a_id = ps_type_record_id(a);
  psx_record_id_t b_id = ps_type_record_id(b);
  if (a_id != PSX_RECORD_ID_INVALID && b_id != PSX_RECORD_ID_INVALID)
    return a_id == b_id;
  if (a->tag_len > 0 &&
      (!a->tag_name || !b->tag_name ||
       strncmp(a->tag_name, b->tag_name, (size_t)a->tag_len) != 0))
    return 0;
  return a->tag_scope_depth_p1 == 0 || b->tag_scope_depth_p1 == 0 ||
         a->tag_scope_depth_p1 == b->tag_scope_depth_p1;
}

psx_record_id_t ps_type_record_id(const psx_type_t *type) {
  if (!type || !ps_type_is_tag_aggregate(type))
    return PSX_RECORD_ID_INVALID;
  return type->record_id;
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

static psx_integer_kind_t integer_kind_from_token(token_kind_t token_kind) {
  switch (token_kind) {
    case TK_BOOL: return PSX_INTEGER_KIND_BOOL;
    case TK_CHAR: return PSX_INTEGER_KIND_CHAR;
    case TK_SHORT: return PSX_INTEGER_KIND_SHORT;
    case TK_LONG: return PSX_INTEGER_KIND_LONG;
    case TK_ENUM: return PSX_INTEGER_KIND_ENUM;
    case TK_INT:
    case TK_SIGNED:
    case TK_UNSIGNED:
    case TK_EOF:
    default:
      return PSX_INTEGER_KIND_INT;
  }
}

static psx_floating_kind_t floating_kind_from_token(
    tk_float_kind_t token_kind) {
  switch (token_kind) {
    case TK_FLOAT_KIND_FLOAT: return PSX_FLOATING_KIND_FLOAT;
    case TK_FLOAT_KIND_LONG_DOUBLE: return PSX_FLOATING_KIND_LONG_DOUBLE;
    case TK_FLOAT_KIND_DOUBLE: return PSX_FLOATING_KIND_DOUBLE;
    case TK_FLOAT_KIND_NONE:
    default:
      return PSX_FLOATING_KIND_NONE;
  }
}

psx_type_t *ps_type_new_in(
    arena_context_t *arena_context, psx_type_kind_t kind) {
  psx_type_t *type = arena_alloc_in(arena_context, sizeof(psx_type_t));
  if (!type) return NULL;
  type->kind = kind;
  type->integer_kind = PSX_INTEGER_KIND_NONE;
  return type;
}

void ps_type_normalize_scalar_identity(psx_type_t *type) {
  if (!type) return;
  if (type->kind == PSX_TYPE_INTEGER) {
    if (type->integer_kind == PSX_INTEGER_KIND_NONE)
      type->integer_kind = PSX_INTEGER_KIND_INT;
  }
  ps_type_normalize_scalar_identity(psx_type_owned_base_mut(type));
  if (type->kind == PSX_TYPE_FUNCTION) {
    for (int i = 0; i < type->param_count; i++)
      ps_type_normalize_scalar_identity(
          psx_type_owned_param_mut(type, i));
  }
}

psx_type_t *ps_type_new_integer_kind_in(
    arena_context_t *arena_context, psx_integer_kind_t integer_kind,
    int is_unsigned, int is_plain_char) {
  if (integer_kind <= PSX_INTEGER_KIND_NONE ||
      integer_kind > PSX_INTEGER_KIND_ENUM)
    return NULL;
  psx_type_t *type = ps_type_new_in(
      arena_context,
      integer_kind == PSX_INTEGER_KIND_BOOL
          ? PSX_TYPE_BOOL : PSX_TYPE_INTEGER);
  if (!type) return NULL;
  type->integer_kind = integer_kind;
  type->is_unsigned = is_unsigned ? 1 : 0;
  type->is_plain_char =
      integer_kind == PSX_INTEGER_KIND_CHAR && is_plain_char ? 1 : 0;
  return type;
}

psx_type_t *ps_type_new_integer_in(
    arena_context_t *arena_context, token_kind_t scalar_token_kind,
    int is_unsigned) {
  return ps_type_new_integer_kind_in(
      arena_context, integer_kind_from_token(scalar_token_kind),
      is_unsigned, scalar_token_kind == TK_CHAR);
}

psx_type_t *ps_type_new_enum_in(
    arena_context_t *arena_context, char *tag_name, int tag_len,
    int tag_scope_depth_p1) {
  psx_type_t *type = ps_type_new_integer_kind_in(
      arena_context, PSX_INTEGER_KIND_ENUM, 0, 0);
  if (!type) return NULL;
  type->tag_name = tag_name;
  type->tag_len = tag_len;
  type->tag_scope_depth_p1 = tag_scope_depth_p1;
  return type;
}

psx_type_t *ps_type_new_record_in(
    arena_context_t *arena_context, const psx_record_decl_t *record) {
  if (!record || record->record_id == PSX_RECORD_ID_INVALID ||
      (record->record_kind != PSX_TYPE_STRUCT &&
       record->record_kind != PSX_TYPE_UNION))
    return NULL;
  psx_type_t *type = ps_type_new_in(
      arena_context, record->record_kind);
  if (!type) return NULL;
  type->record_id = record->record_id;
  type->tag_name = record->tag_name;
  type->tag_len = record->tag_len;
  return type;
}

psx_type_t *ps_type_new_floating_in(
    arena_context_t *arena_context, psx_floating_kind_t floating_kind,
    int is_complex) {
  if (floating_kind <= PSX_FLOATING_KIND_NONE ||
      floating_kind > PSX_FLOATING_KIND_LONG_DOUBLE)
    return NULL;
  psx_type_t *type = ps_type_new_in(
      arena_context, is_complex ? PSX_TYPE_COMPLEX : PSX_TYPE_FLOAT);
  if (!type) return NULL;
  type->floating_kind = floating_kind;
  if (is_complex) {
    type->base = ps_type_new_floating_in(
        arena_context, floating_kind, 0);
    if (!type->base) return NULL;
  }
  return type;
}

psx_type_t *ps_type_new_float_in(
    arena_context_t *arena_context, tk_float_kind_t fp_kind) {
  return ps_type_new_floating_in(
      arena_context, floating_kind_from_token(fp_kind), 0);
}

int ps_type_integer_rank(const psx_type_t *type) {
  if (!type) return 0;
  if (type->kind == PSX_TYPE_BOOL) return 0;
  if (type->kind != PSX_TYPE_INTEGER) return 0;
  if (type->is_plain_char) return 1;
  switch (type->integer_kind) {
    case PSX_INTEGER_KIND_CHAR:
      return 1;
    case PSX_INTEGER_KIND_SHORT:
      return 2;
    case PSX_INTEGER_KIND_INT:
    case PSX_INTEGER_KIND_ENUM:
      return 3;
    case PSX_INTEGER_KIND_LONG:
      return 4;
    case PSX_INTEGER_KIND_LONG_LONG:
      return 5;
    default:
      return 0;
  }
}

int ps_type_character_code_unit_width(const psx_type_t *type) {
  if (!type || type->kind != PSX_TYPE_INTEGER ||
      type->integer_kind == PSX_INTEGER_KIND_ENUM)
    return 0;
  switch (type->integer_kind) {
    case PSX_INTEGER_KIND_CHAR:
      return 1;
    case PSX_INTEGER_KIND_SHORT:
      return 2;
    case PSX_INTEGER_KIND_INT:
      return 4;
    default:
      return 0;
  }
}

static ag_target_scalar_kind_t integer_target_kind_for_rank(int rank) {
  if (rank >= 5) return AG_TARGET_SCALAR_LONG_LONG;
  if (rank == 4) return AG_TARGET_SCALAR_LONG;
  if (rank == 2) return AG_TARGET_SCALAR_SHORT;
  if (rank == 1) return AG_TARGET_SCALAR_CHAR;
  return AG_TARGET_SCALAR_INT;
}

static int integer_rank_size(int rank, const ag_data_layout_t *data_layout) {
  return ag_data_layout_scalar_size(data_layout,
                                    integer_target_kind_for_rank(rank));
}

int ps_type_integer_promotion_is_unsigned_for_data_layout(
    const psx_type_t *type, const ag_data_layout_t *data_layout) {
  if (!type || (type->kind != PSX_TYPE_BOOL &&
                type->kind != PSX_TYPE_INTEGER)) {
    return 0;
  }
  if (type->kind == PSX_TYPE_BOOL) return 0;
  int rank = ps_type_integer_rank(type);
  if (rank >= 3) return ps_type_is_unsigned(type);
  return ps_type_is_unsigned(type) && integer_rank_size(rank, data_layout) >=
                                          integer_rank_size(3, data_layout);
}

typedef struct {
  int rank;
  int is_unsigned;
} integer_conversion_t;

static integer_conversion_t
promoted_integer_conversion(const psx_type_t *type,
                            const ag_data_layout_t *data_layout) {
  int rank = ps_type_integer_rank(type);
  integer_conversion_t result = {
      .rank = rank < 3 ? 3 : rank,
      .is_unsigned = ps_type_integer_promotion_is_unsigned_for_data_layout(
          type, data_layout),
  };
  if (result.rank < 3) result.rank = 3;
  return result;
}

static integer_conversion_t
usual_integer_conversion(const psx_type_t *lhs, const psx_type_t *rhs,
                         const ag_data_layout_t *data_layout) {
  integer_conversion_t left = promoted_integer_conversion(lhs, data_layout);
  integer_conversion_t right = promoted_integer_conversion(rhs, data_layout);
  integer_conversion_t result = {
      .rank = left.rank > right.rank ? left.rank : right.rank,
      .is_unsigned = 0,
  };
  if (left.is_unsigned == right.is_unsigned) {
    result.is_unsigned = left.is_unsigned;
    return result;
  }
  integer_conversion_t unsigned_type = left.is_unsigned ? left : right;
  integer_conversion_t signed_type = left.is_unsigned ? right : left;
  if (unsigned_type.rank >= signed_type.rank) {
    result.is_unsigned = 1;
    return result;
  }
  if (integer_rank_size(signed_type.rank, data_layout) >
      integer_rank_size(unsigned_type.rank, data_layout)) {
    result.is_unsigned = 0;
    return result;
  }
  result.rank = signed_type.rank;
  result.is_unsigned = 1;
  return result;
}

int ps_type_usual_arithmetic_result_is_unsigned_for_data_layout(
    const psx_type_t *lhs, const psx_type_t *rhs,
    const ag_data_layout_t *data_layout) {
  if ((lhs && (lhs->kind == PSX_TYPE_FLOAT ||
               lhs->kind == PSX_TYPE_COMPLEX)) ||
      (rhs && (rhs->kind == PSX_TYPE_FLOAT ||
               rhs->kind == PSX_TYPE_COMPLEX))) {
    return 0;
  }
  return usual_integer_conversion(lhs, rhs, data_layout).is_unsigned;
}

static ag_target_scalar_kind_t floating_target_kind(
    psx_floating_kind_t floating_kind, int is_complex) {
  if (floating_kind == PSX_FLOATING_KIND_LONG_DOUBLE)
    return is_complex ? AG_TARGET_SCALAR_LONG_DOUBLE_COMPLEX
                      : AG_TARGET_SCALAR_LONG_DOUBLE;
  if (floating_kind == PSX_FLOATING_KIND_FLOAT)
    return is_complex ? AG_TARGET_SCALAR_FLOAT_COMPLEX
                      : AG_TARGET_SCALAR_FLOAT;
  return is_complex ? AG_TARGET_SCALAR_DOUBLE_COMPLEX
                    : AG_TARGET_SCALAR_DOUBLE;
}

const psx_type_t *ps_type_usual_arithmetic_result_for_data_layout_in(
    arena_context_t *arena_context, const ag_data_layout_t *data_layout,
    const psx_type_t *lhs, const psx_type_t *rhs,
    psx_floating_kind_t fallback_floating_kind, int force_complex) {
  int result_is_complex =
      force_complex ||
      (lhs && lhs->kind == PSX_TYPE_COMPLEX) ||
      (rhs && rhs->kind == PSX_TYPE_COMPLEX);
  if (result_is_complex) {
    psx_floating_kind_t fp = fallback_floating_kind;
    if (lhs && lhs->floating_kind > fp) fp = lhs->floating_kind;
    if (rhs && rhs->floating_kind > fp) fp = rhs->floating_kind;
    if (fp == PSX_FLOATING_KIND_NONE) fp = PSX_FLOATING_KIND_DOUBLE;
    return ps_type_new_floating_in(arena_context, fp, 1);
  }

  if ((lhs && lhs->kind == PSX_TYPE_FLOAT) ||
      (rhs && rhs->kind == PSX_TYPE_FLOAT) ||
      fallback_floating_kind != PSX_FLOATING_KIND_NONE) {
    psx_floating_kind_t fp = fallback_floating_kind;
    if (lhs && lhs->floating_kind > fp) fp = lhs->floating_kind;
    if (rhs && rhs->floating_kind > fp) fp = rhs->floating_kind;
    if (fp == PSX_FLOATING_KIND_NONE) fp = PSX_FLOATING_KIND_DOUBLE;
    return ps_type_new_floating_in(arena_context, fp, 0);
  }

  integer_conversion_t result = usual_integer_conversion(lhs, rhs, data_layout);
  psx_integer_kind_t result_kind =
      result.rank >= 5 ? PSX_INTEGER_KIND_LONG_LONG
      : result.rank >= 4 ? PSX_INTEGER_KIND_LONG
                         : PSX_INTEGER_KIND_INT;
  return ps_type_new_integer_kind_in(
      arena_context, result_kind, result.is_unsigned, 0);
}

const psx_type_t *ps_type_binary_result_for_data_layout_in(
    arena_context_t *arena_context, const ag_data_layout_t *data_layout,
    psx_type_binary_op_t op, const psx_type_t *lhs, const psx_type_t *rhs) {
  if (op == PSX_TYPE_BINARY_COMMA)
    return ps_type_clone_in(arena_context, rhs);
  if (op == PSX_TYPE_BINARY_COMPARE || op == PSX_TYPE_BINARY_LOGICAL)
    return ps_type_new_integer_kind_in(
        arena_context, PSX_INTEGER_KIND_INT, 0, 0);
  if (op == PSX_TYPE_BINARY_SHL || op == PSX_TYPE_BINARY_SHR)
    return ps_type_usual_arithmetic_result_for_data_layout_in(
        arena_context, data_layout, lhs, NULL, PSX_FLOATING_KIND_NONE, 0);

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
      return ps_type_new_integer_kind_in(
          arena_context, PSX_INTEGER_KIND_LONG, 0, 0);
    if (lhs_pointer)
      return lhs->kind == PSX_TYPE_ARRAY
                 ? ps_type_decay_array_in(arena_context, lhs)
                 : ps_type_clone_in(arena_context, lhs);
  }
  return ps_type_usual_arithmetic_result_for_data_layout_in(
      arena_context, data_layout, lhs, rhs, PSX_FLOATING_KIND_NONE,
      (lhs && lhs->kind == PSX_TYPE_COMPLEX) ||
          (rhs && rhs->kind == PSX_TYPE_COMPLEX));
}

const psx_type_t *ps_type_conditional_result_for_data_layout_in(
    arena_context_t *arena_context, const ag_data_layout_t *data_layout,
    const psx_type_t *then_type, const psx_type_t *else_type) {
  int then_is_void = then_type && then_type->kind == PSX_TYPE_VOID;
  int else_is_void = else_type && else_type->kind == PSX_TYPE_VOID;
  if (then_is_void || else_is_void) {
    if (!then_is_void || !else_is_void) return NULL;
    return ps_type_clone_in(arena_context, then_type);
  }
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
  return ps_type_usual_arithmetic_result_for_data_layout_in(
      arena_context, data_layout, then_type, else_type, PSX_FLOATING_KIND_NONE,
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
  function_type->has_function_prototype = 1;
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
    int array_len, int is_vla) {
  psx_type_t *type = ps_type_new_in(arena_context, PSX_TYPE_ARRAY);
  if (!type) return NULL;
  type->base = base;
  type->array_len = array_len;
  type->is_vla = is_vla ? 1 : 0;
  return type;
}

int ps_type_complete_array(psx_type_t *type, int array_len) {
  if (!type || type->kind != PSX_TYPE_ARRAY || type->is_vla ||
      array_len <= 0 || !type->base) return 0;
  if (type->array_len > 0 && type->array_len != array_len) return 0;
  type->array_len = array_len;
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
  for (int i = dim_count - 1; i >= 0; i--) {
    int len = dims[i];
    result = ps_type_new_array_in(arena_context, child, len, 0);
    child = result;
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
    int param_count, int is_variadic, int has_prototype) {
  if (!op || op->kind != PSX_DECL_OP_FUNCTION || param_count < 0)
    return 0;
  op->function_param_types = NULL;
  op->function_param_count = param_count;
  op->function_is_variadic = is_variadic ? 1 : 0;
  op->function_has_prototype = has_prototype ? 1 : 0;
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
            op->function_is_variadic,
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
  return ps_declarator_shape_append_shape_in(
      arena_context, dst, src);
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
      type = ps_type_new_array_in(
          arena_context, type, op->array_len, op->is_vla_array);
    } else if (op->kind == PSX_DECL_OP_FUNCTION) {
      type = ps_type_new_function_in(arena_context, type);
      if (op->has_canonical_function_params) {
        ps_type_set_function_params_in(
            arena_context, type, op->function_param_types,
            op->function_param_count, op->function_is_variadic);
        type->has_function_prototype =
            op->function_has_prototype ? 1 : 0;
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

token_kind_t ps_type_tag_token_kind(const psx_type_t *type) {
  if (!type) return TK_EOF;
  if (type->kind == PSX_TYPE_STRUCT) return TK_STRUCT;
  if (type->kind == PSX_TYPE_UNION) return TK_UNION;
  if (type->kind == PSX_TYPE_INTEGER &&
      type->integer_kind == PSX_INTEGER_KIND_ENUM)
    return TK_ENUM;
  return TK_EOF;
}

tk_float_kind_t ps_type_floating_token_kind(const psx_type_t *type) {
  if (!type) return TK_FLOAT_KIND_NONE;
  switch (type->floating_kind) {
    case PSX_FLOATING_KIND_FLOAT: return TK_FLOAT_KIND_FLOAT;
    case PSX_FLOATING_KIND_DOUBLE: return TK_FLOAT_KIND_DOUBLE;
    case PSX_FLOATING_KIND_LONG_DOUBLE: return TK_FLOAT_KIND_LONG_DOUBLE;
    case PSX_FLOATING_KIND_NONE:
    default:
      return TK_FLOAT_KIND_NONE;
  }
}

psx_type_t *ps_type_new_tag_in(
    arena_context_t *arena_context, token_kind_t tag_kind,
    char *tag_name, int tag_len, int tag_scope_depth_p1) {
  psx_type_t *type = ps_type_new_in(
      arena_context, ps_type_kind_from_tag_kind(tag_kind));
  if (!type) return NULL;
  type->tag_name = tag_name;
  type->tag_len = tag_len;
  type->tag_scope_depth_p1 = tag_scope_depth_p1;
  return type;
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
    case PSX_TYPE_COMPLEX:
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
      a->is_plain_char != b->is_plain_char) {
    return 0;
  }
  switch (a->kind) {
    case PSX_TYPE_BOOL:
    case PSX_TYPE_INTEGER:
      if ((a->integer_kind == PSX_INTEGER_KIND_ENUM ||
           b->integer_kind == PSX_INTEGER_KIND_ENUM) &&
          !ps_type_tag_identity_matches(a, b))
        return 0;
      return a->integer_kind == b->integer_kind;
    case PSX_TYPE_FLOAT:
    case PSX_TYPE_COMPLEX:
      return a->floating_kind == b->floating_kind;
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
      if (a->integer_kind == PSX_INTEGER_KIND_ENUM ||
          b->integer_kind == PSX_INTEGER_KIND_ENUM) {
        return ps_type_tag_identity_matches(a, b);
      }
      return a->is_unsigned == b->is_unsigned &&
             a->is_plain_char == b->is_plain_char &&
             ps_type_integer_rank(a) == ps_type_integer_rank(b);
    case PSX_TYPE_FLOAT:
    case PSX_TYPE_COMPLEX:
      return a->floating_kind == b->floating_kind;
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
                               const canonical_sig_path_t *path,
                               const ag_data_layout_t *data_layout) {
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
      int rank = ps_type_integer_rank(type);
      if (rank <= 0) rank = 3;
      unsigned int bits =
          (unsigned int)(integer_rank_size(rank, data_layout) * 8);
      if (type->integer_kind == PSX_INTEGER_KIND_ENUM) {
        canonical_sig_lit(w, "e{");
        canonical_sig_uint(w, (unsigned int)(type->tag_len > 0 ? type->tag_len : 0));
        canonical_sig_lit(w, ":");
        if (type->tag_len > 0)
          canonical_sig_bytes(w, type->tag_name, (size_t)type->tag_len);
        canonical_sig_lit(w, "}");
      } else if (type->is_plain_char) {
        canonical_sig_lit(w, "c");
        canonical_sig_uint(w, bits);
      } else if (type->integer_kind == PSX_INTEGER_KIND_LONG_LONG) {
        canonical_sig_lit(w, type->is_unsigned ? "ull" : "ll");
        canonical_sig_uint(w, bits);
      } else if (type->integer_kind == PSX_INTEGER_KIND_LONG) {
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
      canonical_sig_uint(
          w, (unsigned int)(ag_data_layout_scalar_size(
                                data_layout,
                                floating_target_kind(type->floating_kind, 0)) *
                            8));
      return;
    case PSX_TYPE_COMPLEX:
      canonical_sig_lit(w, "x");
      canonical_sig_uint(
          w, (unsigned int)(ag_data_layout_scalar_size(
                                data_layout,
                                floating_target_kind(type->floating_kind, 1)) *
                            8));
      return;
    case PSX_TYPE_POINTER:
      canonical_sig_lit(w, "p<");
      canonical_sig_type(w, type->base, &current_path, data_layout);
      canonical_sig_lit(w, ">");
      return;
    case PSX_TYPE_ARRAY:
      canonical_sig_lit(w, "a");
      canonical_sig_uint(w, (unsigned int)(type->array_len > 0 ? type->array_len : 0));
      canonical_sig_lit(w, "<");
      canonical_sig_type(w, type->base, &current_path, data_layout);
      canonical_sig_lit(w, ">");
      return;
    case PSX_TYPE_FUNCTION:
      if (type->param_count < 0 ||
          (type->param_count > 0 && !type->param_types)) {
        w->failed = 1;
        return;
      }
      canonical_sig_type(w, type->base, &current_path, data_layout);
      canonical_sig_lit(w, "(");
      for (int i = 0; i < type->param_count; i++) {
        if (i > 0) canonical_sig_lit(w, ",");
        canonical_sig_type(w, type->param_types[i], &current_path, data_layout);
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

int ps_type_format_canonical_signature_for_data_layout(
    const psx_type_t *type, const ag_data_layout_t *data_layout, char *out,
    size_t out_size) {
  canonical_sig_writer_t writer = {out, out_size, 0, 0};
  if (!ag_data_layout_is_valid(data_layout))
    writer.failed = 1;
  if (!writer.failed)
    canonical_sig_type(&writer, type, NULL, data_layout);
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
  ps_type_normalize_scalar_identity(type);
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
    normalized = decayed;
  } else if (normalized.kind == PSX_TYPE_INTEGER) {
    if (normalized.integer_kind == PSX_INTEGER_KIND_NONE)
      normalized.integer_kind = PSX_INTEGER_KIND_INT;
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

int ps_type_pointer_depth(const psx_type_t *type) {
  int depth = 0;
  while (type && type->kind == PSX_TYPE_POINTER) {
    depth++;
    type = type->base;
  }
  return depth;
}

int psx_record_member_decl_is_tag_aggregate(
    const psx_record_member_decl_t *member) {
  const psx_type_t *type = member
                               ? ps_type_array_leaf_type(
                                     psx_record_member_decl_type(member))
                               : NULL;
  return ps_type_is_tag_aggregate(type);
}

int psx_record_member_decl_is_unnamed_struct(
    const psx_record_member_decl_t *member) {
  const psx_type_t *type = member
                               ? ps_type_array_leaf_type(
                                     psx_record_member_decl_type(member))
                               : NULL;
  return member && member->len <= 0 && type &&
         type->kind == PSX_TYPE_STRUCT;
}

int psx_record_member_decl_is_unnamed_union(
    const psx_record_member_decl_t *member) {
  const psx_type_t *type = member
                               ? ps_type_array_leaf_type(
                                     psx_record_member_decl_type(member))
                               : NULL;
  return member && member->len <= 0 && type &&
         type->kind == PSX_TYPE_UNION;
}

int psx_record_member_decl_is_unnamed_aggregate(
    const psx_record_member_decl_t *member) {
  return psx_record_member_decl_is_unnamed_struct(member) ||
         psx_record_member_decl_is_unnamed_union(member);
}

tk_float_kind_t psx_record_member_decl_fp_kind(
    const psx_record_member_decl_t *member) {
  const psx_type_t *type = member
                               ? ps_type_array_leaf_type(
                                     psx_record_member_decl_type(member))
                               : NULL;
  if (!type ||
      (type->kind != PSX_TYPE_FLOAT && type->kind != PSX_TYPE_COMPLEX))
    return TK_FLOAT_KIND_NONE;
  return ps_type_floating_token_kind(type);
}

int psx_record_member_decl_is_bool(
    const psx_record_member_decl_t *member) {
  const psx_type_t *type = member
                               ? ps_type_array_leaf_type(
                                     psx_record_member_decl_type(member))
                               : NULL;
  return type && type->kind == PSX_TYPE_BOOL;
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

void ps_type_remove_all_qualifiers_recursive(psx_type_t *type) {
  if (!type) return;
  type->qualifiers = PSX_TYPE_QUALIFIER_NONE;
  ps_type_remove_all_qualifiers_recursive(psx_type_owned_base_mut(type));
  for (int i = 0; i < type->param_count; i++)
    ps_type_remove_all_qualifiers_recursive(
        psx_type_owned_param_mut(type, i));
}
