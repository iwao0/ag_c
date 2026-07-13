#include "type.h"
#include "arena.h"
#include "tag_member_public.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int type_tag_identity_matches(
    const psx_type_t *a, const psx_type_t *b) {
  if (!a || !b || a->tag_kind != b->tag_kind || a->tag_len != b->tag_len)
    return 0;
  if (a->tag_len > 0 &&
      (!a->tag_name || !b->tag_name ||
       strncmp(a->tag_name, b->tag_name, (size_t)a->tag_len) != 0))
    return 0;
  return a->tag_scope_depth_p1 == 0 || b->tag_scope_depth_p1 == 0 ||
         a->tag_scope_depth_p1 == b->tag_scope_depth_p1;
}

psx_type_t *ps_type_new(psx_type_kind_t kind) {
  psx_type_t *type = arena_alloc(sizeof(psx_type_t));
  type->kind = kind;
  type->scalar_kind = TK_EOF;
  type->tag_kind = TK_EOF;
  return type;
}

void ps_type_normalize_integer_identity(psx_type_t *type) {
  if (!type) return;
  if (type->kind == PSX_TYPE_INTEGER) {
    if (type->tag_kind == TK_ENUM) type->scalar_kind = TK_ENUM;
    else if (type->size == 1) type->scalar_kind = TK_CHAR;
    else if (type->size == 2) type->scalar_kind = TK_SHORT;
    else if (type->size == 8) type->scalar_kind = TK_LONG;
    else type->scalar_kind = TK_INT;
  }
  ps_type_normalize_integer_identity(type->base);
  if (type->kind == PSX_TYPE_FUNCTION) {
    for (int i = 0; i < type->param_count && i < 16; i++)
      ps_type_normalize_integer_identity(type->param_types[i]);
  }
}

psx_type_t *ps_type_new_integer(token_kind_t scalar_kind, int size, int is_unsigned) {
  psx_type_t *type = ps_type_new(scalar_kind == TK_BOOL ? PSX_TYPE_BOOL : PSX_TYPE_INTEGER);
  type->scalar_kind = scalar_kind;
  type->size = size;
  type->align = size > 0 ? size : 1;
  if (type->align > 8) type->align = 8;
  type->is_unsigned = is_unsigned ? 1 : 0;
  if (scalar_kind == TK_CHAR) type->is_plain_char = 1;
  return type;
}

psx_type_t *ps_type_new_enum(char *tag_name, int tag_len,
                              int tag_scope_depth_p1, int size) {
  psx_type_t *type = ps_type_new_integer(
      TK_ENUM, size > 0 ? size : 4, 0);
  type->tag_kind = TK_ENUM;
  type->tag_name = tag_name;
  type->tag_len = tag_len;
  type->tag_scope_depth_p1 = tag_scope_depth_p1;
  return type;
}

psx_type_t *ps_type_new_float(tk_float_kind_t fp_kind, int size) {
  psx_type_t *type = ps_type_new(PSX_TYPE_FLOAT);
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

int ps_type_integer_promotion_is_unsigned(const psx_type_t *type) {
  if (!type || (type->kind != PSX_TYPE_BOOL &&
                type->kind != PSX_TYPE_INTEGER)) {
    return 0;
  }
  return ps_type_sizeof(type) >= 4 && ps_type_is_unsigned(type);
}

psx_type_t *ps_type_usual_arithmetic_result(
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
    psx_type_t *type = ps_type_new(PSX_TYPE_COMPLEX);
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
    psx_type_t *type = ps_type_new_float(
        fp, fp == TK_FLOAT_KIND_FLOAT ? 4 : 8);
    if ((lhs && lhs->is_long_double) || (rhs && rhs->is_long_double))
      type->is_long_double = 1;
    return type;
  }

  int lhs_size = type_integer_promotion_size(lhs);
  int rhs_size = type_integer_promotion_size(rhs);
  int lhs_unsigned = ps_type_integer_promotion_is_unsigned(lhs);
  int rhs_unsigned = ps_type_integer_promotion_is_unsigned(rhs);
  int result_unsigned;
  if (lhs_unsigned == rhs_unsigned) {
    result_unsigned = lhs_unsigned;
  } else {
    int unsigned_size = lhs_unsigned ? lhs_size : rhs_size;
    int signed_size = lhs_unsigned ? rhs_size : lhs_size;
    result_unsigned = unsigned_size >= signed_size;
  }
  int size = lhs_size > rhs_size ? lhs_size : rhs_size;
  psx_type_t *type = ps_type_new_integer(TK_EOF, size, result_unsigned);
  type->is_long_long =
      (lhs && lhs->is_long_long) || (rhs && rhs->is_long_long);
  return type;
}

psx_type_t *ps_type_binary_result(
    psx_type_binary_op_t op, const psx_type_t *lhs,
    const psx_type_t *rhs) {
  if (op == PSX_TYPE_BINARY_COMMA)
    return ps_type_clone(rhs);
  if (op == PSX_TYPE_BINARY_COMPARE || op == PSX_TYPE_BINARY_LOGICAL)
    return ps_type_new_integer(TK_INT, 4, 0);
  if (op == PSX_TYPE_BINARY_SHL || op == PSX_TYPE_BINARY_SHR)
    return ps_type_usual_arithmetic_result(
        lhs, NULL, TK_FLOAT_KIND_NONE, 0);

  int lhs_pointer = ps_type_is_pointer(lhs);
  int rhs_pointer = ps_type_is_pointer(rhs);
  if (op == PSX_TYPE_BINARY_ADD && lhs_pointer != rhs_pointer)
    return ps_type_clone(lhs_pointer ? lhs : rhs);
  if (op == PSX_TYPE_BINARY_SUB) {
    if (lhs_pointer && rhs_pointer)
      return ps_type_new_integer(TK_LONG, 8, 0);
    if (lhs_pointer) return ps_type_clone(lhs);
  }
  return ps_type_usual_arithmetic_result(
      lhs, rhs, TK_FLOAT_KIND_NONE,
      (lhs && lhs->kind == PSX_TYPE_COMPLEX) ||
          (rhs && rhs->kind == PSX_TYPE_COMPLEX));
}

psx_type_t *ps_type_conditional_result(
    const psx_type_t *then_type, const psx_type_t *else_type) {
  if (ps_type_is_pointer(then_type)) return ps_type_clone(then_type);
  if (ps_type_is_pointer(else_type)) return ps_type_clone(else_type);
  if (then_type && else_type && then_type->kind == else_type->kind &&
      ps_type_is_tag_aggregate(then_type))
    return ps_type_clone(then_type);
  return ps_type_usual_arithmetic_result(
      then_type, else_type, TK_FLOAT_KIND_NONE,
      (then_type && then_type->kind == PSX_TYPE_COMPLEX) ||
          (else_type && else_type->kind == PSX_TYPE_COMPLEX));
}

psx_type_t *ps_type_new_pointer(psx_type_t *base, int deref_size) {
  psx_type_t *type = ps_type_new(PSX_TYPE_POINTER);
  type->base = base;
  type->size = 8;
  type->align = 8;
  type->deref_size = deref_size;
  type->pointer_qual_levels = 1;
  return type;
}

psx_type_t *ps_type_new_function(psx_type_t *return_type) {
  psx_type_t *type = ps_type_new(PSX_TYPE_FUNCTION);
  type->base = return_type;
  return type;
}

void ps_type_set_function_params(psx_type_t *function_type,
                                  psx_type_t *const *param_types,
                                  int param_count, int is_variadic) {
  if (!function_type || function_type->kind != PSX_TYPE_FUNCTION) return;
  if (param_count < 0) param_count = 0;
  function_type->param_count = param_count;
  function_type->is_variadic_function = is_variadic ? 1 : 0;
  for (int i = 0; i < 16; i++) {
    function_type->param_types[i] =
        i < param_count && param_types ? ps_type_clone(param_types[i]) : NULL;
  }
}

const psx_type_t *ps_type_find_function(const psx_type_t *type) {
  while (type) {
    if (type->kind == PSX_TYPE_FUNCTION) return type;
    if (type->kind != PSX_TYPE_POINTER && type->kind != PSX_TYPE_ARRAY)
      return NULL;
    type = type->base;
  }
  return NULL;
}

const psx_type_t *ps_type_function_return_type(const psx_type_t *type) {
  const psx_type_t *function = ps_type_find_function(type);
  return function ? function->base : NULL;
}

psx_type_t *psx_type_new_storage_object(
    int object_size, int elem_size, int is_array,
    tk_float_kind_t fp_kind, int is_unsigned,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    int tag_scope_depth_p1, int is_pointer) {
  int value_size = elem_size > 0 ? elem_size : object_size;
  if (value_size <= 0) value_size = 1;

  psx_type_t *value = NULL;
  if (tag_kind == TK_STRUCT || tag_kind == TK_UNION) {
    value = ps_type_new_tag(tag_kind, tag_name, tag_len,
                             tag_scope_depth_p1, value_size);
  } else if (tag_kind == TK_ENUM) {
    value = ps_type_new_enum(
        tag_name, tag_len, tag_scope_depth_p1, value_size);
  } else if (fp_kind != TK_FLOAT_KIND_NONE) {
    value = ps_type_new_float(fp_kind, value_size);
  } else {
    value = ps_type_new_integer(TK_EOF, value_size, is_unsigned);
  }

  psx_type_t *element = value;
  if (is_pointer) {
    element = ps_type_new_pointer(value, value_size);
  }
  if (!is_array) return element;

  int storage_elem_size = is_pointer ? 8 : elem_size;
  if (storage_elem_size <= 0) storage_elem_size = ps_type_sizeof(element);
  if (storage_elem_size <= 0) storage_elem_size = 1;
  int array_len = object_size > 0 && object_size % storage_elem_size == 0
                      ? object_size / storage_elem_size
                      : 0;
  psx_type_t *array = ps_type_new_array(
      element, array_len, object_size, storage_elem_size, 0);
  return array;
}

static unsigned int pointer_mask_for_subtree(unsigned int mask,
                                             int total_levels,
                                             int subtree_levels) {
  if (total_levels <= subtree_levels) return mask;
  int shift = total_levels - subtree_levels;
  if (shift >= 32) return 0;
  return mask >> shift;
}

psx_type_t *ps_type_wrap_pointer_levels(psx_type_t *base, int levels,
                                          int top_deref_size,
                                          unsigned int const_mask,
                                          unsigned int volatile_mask) {
  if (levels <= 0) return base;
  psx_type_t *type = base;
  const psx_type_t *leaf = base;
  while (leaf && (leaf->kind == PSX_TYPE_POINTER ||
                  leaf->kind == PSX_TYPE_ARRAY))
    leaf = leaf->base;
  int deep_base_size = ps_type_sizeof(leaf);
  if (deep_base_size <= 0) deep_base_size = ps_type_sizeof(base);
  for (int level = 1; level <= levels; level++) {
    int deref_size = 8;
    if (levels == 1) {
      deref_size = top_deref_size;
    } else if (level == 1) {
      deref_size = deep_base_size;
    } else if (level == levels && top_deref_size > 0) {
      deref_size = top_deref_size;
    }
    if (deref_size <= 0) deref_size = (level == 1) ? ps_type_sizeof(type) : 8;
    if (deref_size <= 0) deref_size = 8;
    psx_type_t *ptr = ps_type_new_pointer(type, deref_size);
    ptr->pointer_qual_levels = level;
    ptr->pointer_const_qual_mask =
        pointer_mask_for_subtree(const_mask, levels, level);
    ptr->pointer_volatile_qual_mask =
        pointer_mask_for_subtree(volatile_mask, levels, level);
    type = ptr;
  }
  return type;
}

psx_type_t *ps_type_new_array(psx_type_t *base, int array_len, int size, int elem_size, int is_vla) {
  psx_type_t *type = ps_type_new(PSX_TYPE_ARRAY);
  type->base = base;
  type->array_len = array_len;
  type->size = size;
  type->align = base && base->align > 0 ? base->align : 1;
  type->elem_size = elem_size;
  type->deref_size = elem_size;
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
  type->elem_size = child_size;
  type->deref_size = child_size;
  return 1;
}

psx_type_t *ps_type_clone(const psx_type_t *src) {
  if (!src) return NULL;
  psx_type_t *dst = ps_type_new(src->kind);
  *dst = *src;
  dst->base = ps_type_clone(src->base);
  for (int i = 0; i < src->param_count && i < 16; i++)
    dst->param_types[i] = ps_type_clone(src->param_types[i]);
  return dst;
}

psx_type_t *ps_type_clone_persistent(const psx_type_t *src) {
  if (!src) return NULL;
  psx_type_t *dst = calloc(1, sizeof(psx_type_t));
  if (!dst) return NULL;
  *dst = *src;
  dst->base = ps_type_clone_persistent(src->base);
  for (int i = 0; i < src->param_count && i < 16; i++)
    dst->param_types[i] = ps_type_clone_persistent(src->param_types[i]);
  return dst;
}

static psx_type_t *type_build_array_dim_chain(psx_type_t *base,
                                               const int *dims,
                                               int dim_count,
                                               int leaf_size) {
  if (!base || !dims || dim_count <= 0 || leaf_size <= 0) return base;
  psx_type_t *result = base;
  int child_size = ps_type_sizeof(base);
  if (child_size <= 0) child_size = leaf_size;
  for (int i = dim_count - 1; i >= 0; i--) {
    int len = dims[i];
    if (len <= 0) return base;
    int size = child_size * len;
    psx_type_t *array = ps_type_new_array(result, len, size,
                                            child_size, 0);
    result = array;
    child_size = size;
  }
  return result;
}

psx_type_t *ps_type_wrap_array_dims(psx_type_t *base,
                                     const int *dims, int dim_count) {
  if (!base || !dims || dim_count <= 0) return base;
  psx_type_t *result = base;
  int child_size = ps_type_sizeof(base);
  if (child_size <= 0) child_size = 1;
  for (int i = dim_count - 1; i >= 0; i--) {
    int len = dims[i];
    int size = len > 0 ? len * child_size : 0;
    psx_type_t *array = ps_type_new_array(
        result, len, size, child_size, 0);
    result = array;
    child_size = size;
  }
  return result;
}

void ps_declarator_shape_init(psx_declarator_shape_t *shape) {
  if (!shape) return;
  *shape = (psx_declarator_shape_t){0};
}

static psx_declarator_op_t *declarator_shape_append(
    psx_declarator_shape_t *shape, psx_declarator_op_kind_t kind) {
  if (!shape || shape->count < 0 || shape->count >= 24) return NULL;
  psx_declarator_op_t *op = &shape->ops[shape->count++];
  *op = (psx_declarator_op_t){0};
  op->kind = kind;
  return op;
}

int ps_declarator_shape_append_pointer(
    psx_declarator_shape_t *shape, int is_const_qualified,
    int is_volatile_qualified) {
  psx_declarator_op_t *op =
      declarator_shape_append(shape, PSX_DECL_OP_POINTER);
  if (!op) return 0;
  op->is_const_qualified = is_const_qualified ? 1u : 0u;
  op->is_volatile_qualified = is_volatile_qualified ? 1u : 0u;
  return 1;
}

int ps_declarator_shape_append_pointer_levels(
    psx_declarator_shape_t *shape, int levels,
    unsigned int const_mask, unsigned int volatile_mask) {
  if (!shape || levels < 0) return 0;
  for (int level = 0; level < levels; level++) {
    if (!ps_declarator_shape_append_pointer(
            shape, level < 32 && (const_mask & (1u << level)),
            level < 32 && (volatile_mask & (1u << level)))) {
      return 0;
    }
  }
  return 1;
}

int ps_declarator_shape_append_array(
    psx_declarator_shape_t *shape, int array_len) {
  return ps_declarator_shape_append_array_ex(shape, array_len, 0);
}

int ps_declarator_shape_append_array_ex(
    psx_declarator_shape_t *shape, int array_len, int is_incomplete) {
  psx_declarator_op_t *op =
      declarator_shape_append(shape, PSX_DECL_OP_ARRAY);
  if (!op) return 0;
  op->array_len = array_len;
  op->is_incomplete_array = is_incomplete ? 1u : 0u;
  return 1;
}

int ps_declarator_shape_append_vla_array(
    psx_declarator_shape_t *shape) {
  psx_declarator_op_t *op =
      declarator_shape_append(shape, PSX_DECL_OP_ARRAY);
  if (!op) return 0;
  op->is_vla_array = 1;
  return 1;
}

int psx_declarator_shape_append_array_dims(
    psx_declarator_shape_t *shape, const int *dims, int dim_count) {
  if (!shape || dim_count < 0 || (dim_count > 0 && !dims)) return 0;
  for (int i = 0; i < dim_count; i++) {
    if (!ps_declarator_shape_append_array(shape, dims[i])) return 0;
  }
  return 1;
}

int ps_declarator_shape_append_function(psx_declarator_shape_t *shape) {
  return declarator_shape_append(shape, PSX_DECL_OP_FUNCTION) != NULL;
}

int ps_declarator_shape_append_shape(
    psx_declarator_shape_t *shape, const psx_declarator_shape_t *suffix) {
  if (!shape || !suffix || suffix->count < 0) return 0;
  for (int i = 0; i < suffix->count; i++) {
    const psx_declarator_op_t *op = &suffix->ops[i];
    int appended = 0;
    if (op->kind == PSX_DECL_OP_POINTER) {
      appended = ps_declarator_shape_append_pointer(
          shape, op->is_const_qualified, op->is_volatile_qualified);
    } else if (op->kind == PSX_DECL_OP_ARRAY) {
      if (op->is_vla_array)
        appended = ps_declarator_shape_append_vla_array(shape);
      else
        appended = ps_declarator_shape_append_array_ex(
            shape, op->array_len, op->is_incomplete_array);
    } else if (op->kind == PSX_DECL_OP_FUNCTION) {
      appended = ps_declarator_shape_append_function(shape);
      if (appended) {
        psx_declarator_op_t *copy = &shape->ops[shape->count - 1];
        copy->has_canonical_function_params =
            op->has_canonical_function_params;
        for (int j = 0; j < 16; j++)
          copy->function_param_types[j] = op->function_param_types[j];
        copy->function_param_count = op->function_param_count;
        copy->function_is_variadic = op->function_is_variadic;
      }
    }
    if (!appended) return 0;
  }
  return 1;
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

/* is_const/is_volatileはidentifier-outwardのcanonical構造上の修飾位置。
 * pointer_*_qual_maskは旧APIが使う字句順compat viewなので、各連続pointer
 * chainで順番を反転したlocal bitを保持する。 */
static void type_sync_pointer_compat_qual_masks(psx_type_t *type) {
  while (type) {
    if (type->kind != PSX_TYPE_POINTER) {
      type = type->base;
      continue;
    }
    psx_type_t *chain[24];
    int count = 0;
    psx_type_t *cur = type;
    while (cur && cur->kind == PSX_TYPE_POINTER && count < 24) {
      chain[count++] = cur;
      cur = cur->base;
    }
    for (int i = 0; i < count; i++) {
      const psx_type_t *lexical = chain[count - 1 - i];
      chain[i]->pointer_qual_levels = count - i;
      chain[i]->pointer_const_qual_mask =
          lexical->is_const_qualified ? 1u : 0u;
      chain[i]->pointer_volatile_qual_mask =
          lexical->is_volatile_qualified ? 1u : 0u;
    }
    type = cur;
  }
}

psx_type_t *ps_type_apply_declarator_shape(
    psx_type_t *base, const psx_declarator_shape_t *shape) {
  if (!base || !shape) return base;
  psx_type_t *type = base;
  for (int i = shape->count - 1; i >= 0; i--) {
    const psx_declarator_op_t *op = &shape->ops[i];
    if (op->kind == PSX_DECL_OP_POINTER) {
      int deref_size = ps_type_sizeof(type);
      if (deref_size <= 0) deref_size = ps_type_deref_size(type);
      if (deref_size <= 0) deref_size = type->elem_size;
      if (deref_size <= 0) deref_size = 8;
      type = ps_type_new_pointer(type, deref_size);
      type->is_const_qualified = op->is_const_qualified;
      type->is_volatile_qualified = op->is_volatile_qualified;
      type->pointer_const_qual_mask = op->is_const_qualified ? 1u : 0u;
      type->pointer_volatile_qual_mask = op->is_volatile_qualified ? 1u : 0u;
    } else if (op->kind == PSX_DECL_OP_ARRAY) {
      int elem_size = ps_type_sizeof(type);
      if (elem_size <= 0) elem_size = ps_type_deref_size(type);
      if (elem_size <= 0) elem_size = type->elem_size;
      if (elem_size <= 0) elem_size = 1;
      int total_size = op->array_len > 0 ? op->array_len * elem_size : 0;
      type = ps_type_new_array(type, op->array_len, total_size,
                                elem_size, op->is_vla_array);
    } else if (op->kind == PSX_DECL_OP_FUNCTION) {
      type = ps_type_new_function(type);
      if (op->has_canonical_function_params) {
        ps_type_set_function_params(
            type, op->function_param_types,
            op->function_param_count, op->function_is_variadic);
      }
    }
  }
  type_sync_pointer_compat_qual_masks(type);
  return type;
}

psx_type_t *ps_type_adjust_parameter_type(psx_type_t *type) {
  if (!type) return NULL;
  if (type->kind == PSX_TYPE_ARRAY) {
    int deref_size = type->base ? ps_type_sizeof(type->base) : type->elem_size;
    psx_type_t *adjusted = ps_type_new_pointer(type->base, deref_size);
    return adjusted;
  }
  if (type->kind == PSX_TYPE_FUNCTION)
    return ps_type_new_pointer(type, 0);
  return type;
}

static int type_matches_canonical_base(const psx_type_t *derived,
                                       const psx_type_t *canonical) {
  if (!derived || !canonical) return derived == canonical;
  if (canonical->kind == PSX_TYPE_FUNCTION) {
    if (derived->kind == PSX_TYPE_FUNCTION)
      return type_matches_canonical_base(derived->base, canonical->base);
    return type_matches_canonical_base(derived, canonical->base);
  }
  if (derived->kind != canonical->kind) return 0;
  if (canonical->kind == PSX_TYPE_ARRAY && canonical->array_len > 0 &&
      derived->array_len != canonical->array_len) {
    return 0;
  }
  if (canonical->kind == PSX_TYPE_POINTER ||
      canonical->kind == PSX_TYPE_ARRAY) {
    return type_matches_canonical_base(derived->base, canonical->base);
  }
  if (ps_type_is_tag_aggregate(canonical)) {
    return derived->tag_kind == canonical->tag_kind &&
           derived->tag_name == canonical->tag_name &&
           derived->tag_len == canonical->tag_len;
  }
  if (canonical->kind == PSX_TYPE_INTEGER &&
      (canonical->scalar_kind == TK_ENUM || derived->scalar_kind == TK_ENUM) &&
      !type_tag_identity_matches(derived, canonical)) {
    return 0;
  }
  int canonical_size = ps_type_sizeof(canonical);
  int derived_size = ps_type_sizeof(derived);
  return canonical_size <= 0 || derived_size <= 0 ||
         canonical_size == derived_size;
}

static psx_type_t *type_rebase_declarator_impl(
    const psx_type_t *derived, const psx_type_t *canonical_base,
    int *rebased) {
  if (!derived) return NULL;
  if (!*rebased && type_matches_canonical_base(derived, canonical_base)) {
    psx_type_t *replacement = ps_type_clone(canonical_base);
    psx_type_copy_common_qualifiers(replacement, derived);
    *rebased = 1;
    return replacement;
  }
  psx_type_t *copy = ps_type_clone(derived);
  if (!*rebased && copy->base) {
    copy->base = type_rebase_declarator_impl(
        derived->base, canonical_base, rebased);
  }
  return copy;
}

psx_type_t *psx_type_rebase_declarator(
    const psx_type_t *derived_type, const psx_type_t *canonical_base,
    int *out_rebased) {
  int rebased = 0;
  psx_type_t *result = type_rebase_declarator_impl(
      derived_type, canonical_base, &rebased);
  if (out_rebased) *out_rebased = rebased;
  return result;
}

psx_type_t *psx_type_rebuild_array_dims(psx_type_t *type,
                                        const int *dims, int dim_count,
                                        int leaf_size) {
  if (!type || !dims || dim_count <= 0 || leaf_size <= 0) return type;

  if (type->kind == PSX_TYPE_ARRAY) {
    psx_type_t *old_outer = type;
    psx_type_t *base = type;
    while (base && base->kind == PSX_TYPE_ARRAY) base = base->base;
    psx_type_t *rebuilt =
        type_build_array_dim_chain(base, dims, dim_count, leaf_size);
    if (rebuilt && rebuilt->kind == PSX_TYPE_ARRAY) {
      psx_type_copy_common_qualifiers(rebuilt, old_outer);
      rebuilt->fp_kind = old_outer->fp_kind;
      rebuilt->tag_kind = old_outer->tag_kind;
      rebuilt->tag_name = old_outer->tag_name;
      rebuilt->tag_len = old_outer->tag_len;
      rebuilt->tag_scope_depth_p1 = old_outer->tag_scope_depth_p1;
    }
    return rebuilt;
  }

  if (type->kind != PSX_TYPE_POINTER) return type;
  psx_type_t *owner = type;
  while (owner->base && owner->base->kind == PSX_TYPE_POINTER)
    owner = owner->base;
  psx_type_t *base = owner->base;
  while (base && base->kind == PSX_TYPE_ARRAY) base = base->base;
  psx_type_t *rebuilt =
      type_build_array_dim_chain(base, dims, dim_count, leaf_size);
  if (!rebuilt || rebuilt == base) return type;
  owner->base = rebuilt;
  owner->deref_size = ps_type_sizeof(rebuilt);
  psx_type_sync_pointer_to_array_metadata_from_base(owner);
  return type;
}

psx_type_t *psx_type_new_runtime_vla_row_view(
    const psx_type_t *source, int row_size, int elem_size,
    int row_stride_frame_off, int strides_remaining) {
  if (!source) return NULL;
  const psx_type_t *element = source;
  if (source->kind == PSX_TYPE_POINTER && source->base)
    element = source->base;
  if (element->kind == PSX_TYPE_ARRAY) {
    psx_type_t *row = ps_type_new(PSX_TYPE_ARRAY);
    *row = *element;
    row->vla_row_stride_frame_off = row_stride_frame_off;
    row->vla_strides_remaining = strides_remaining;
    return row;
  }
  if (elem_size <= 0) elem_size = ps_type_sizeof(element);
  if (elem_size <= 0) elem_size = ps_type_deref_size(element);
  if (elem_size <= 0) return NULL;
  if (row_size < elem_size) row_size = elem_size;
  int array_len = row_size / elem_size;
  if (array_len <= 0) array_len = 1;

  psx_type_t *row = ps_type_new_array((psx_type_t *)element, array_len,
                                       row_size, elem_size, 1);
  row->vla_runtime_strides.outer_stride = elem_size;
  row->vla_row_stride_frame_off = row_stride_frame_off;
  row->vla_strides_remaining = strides_remaining;
  return row;
}

psx_type_t *ps_type_new_vla_object_view(
    const psx_type_t *source, int outer_stride,
    int row_stride_frame_off, int strides_remaining) {
  if (!source) return NULL;
  const psx_type_t *leaf = source;
  while (leaf && (leaf->kind == PSX_TYPE_ARRAY ||
                  leaf->kind == PSX_TYPE_POINTER)) {
    leaf = leaf->base;
  }
  if (!leaf) return NULL;
  int leaf_size = ps_type_sizeof(leaf);
  if (leaf_size <= 0) return NULL;

  psx_type_t *element = (psx_type_t *)leaf;
  if (row_stride_frame_off == 0 && outer_stride > leaf_size &&
      outer_stride % leaf_size == 0) {
    element = ps_type_new_array(
        element, outer_stride / leaf_size,
        outer_stride, leaf_size, 0);
  }
  int element_size = ps_type_sizeof(element);
  if (element_size <= 0) element_size = leaf_size;
  psx_type_t *vla = ps_type_new_array(
      element, 0, 0, element_size, 1);
  vla->vla_runtime_strides.outer_stride = outer_stride;
  vla->vla_row_stride_frame_off = row_stride_frame_off;
  vla->vla_strides_remaining = strides_remaining;
  psx_type_copy_common_qualifiers(vla, source);
  return vla;
}

void ps_type_set_vla_runtime_descriptor(
    psx_type_t *type, int row_stride_frame_off, int strides_remaining,
    int row_stride_src_offset, int row_stride_elem_size) {
  if (!type) return;
  type->is_vla = 1;
  type->vla_row_stride_frame_off = row_stride_frame_off;
  type->vla_strides_remaining = strides_remaining;
  type->vla_row_stride_src_offset = row_stride_src_offset;
  type->vla_row_stride_elem_size =
      row_stride_elem_size > 0 ? (short)row_stride_elem_size : 0;
}

void ps_type_copy_vla_runtime_metadata(psx_type_t *dst,
                                        const psx_type_t *src) {
  if (!dst || !src) return;
  dst->is_vla = src->is_vla;
  dst->vla_row_stride_frame_off = src->vla_row_stride_frame_off;
  dst->vla_strides_remaining = src->vla_strides_remaining;
  dst->vla_row_stride_src_offset = src->vla_row_stride_src_offset;
  dst->vla_row_stride_elem_size = src->vla_row_stride_elem_size;
  dst->vla_param_inner_dim_count = src->vla_param_inner_dim_count;
  for (int i = 0; i < 7; i++) {
    dst->vla_param_inner_dim_consts[i] = src->vla_param_inner_dim_consts[i];
    dst->vla_param_inner_dim_src_offsets[i] =
        src->vla_param_inner_dim_src_offsets[i];
  }
}

void ps_type_set_vla_param_inner_dims(
    psx_type_t *type, const int *inner_dim_consts,
    const int *inner_dim_src_offsets, int inner_dim_count) {
  if (!type) return;
  if (inner_dim_count < 0) inner_dim_count = 0;
  if (inner_dim_count > 7) inner_dim_count = 7;
  type->vla_param_inner_dim_count = (unsigned char)inner_dim_count;
  for (int i = 0; i < 7; i++) {
    type->vla_param_inner_dim_consts[i] =
        (i < inner_dim_count && inner_dim_consts)
            ? (short)inner_dim_consts[i]
            : 0;
    type->vla_param_inner_dim_src_offsets[i] =
        (i < inner_dim_count && inner_dim_src_offsets)
            ? inner_dim_src_offsets[i]
            : 0;
  }
}

psx_type_kind_t ps_type_kind_from_tag_kind(token_kind_t tag_kind) {
  switch (tag_kind) {
    case TK_STRUCT: return PSX_TYPE_STRUCT;
    case TK_UNION: return PSX_TYPE_UNION;
    default: return PSX_TYPE_INVALID;
  }
}

psx_type_t *ps_type_new_tag(token_kind_t tag_kind, char *tag_name, int tag_len,
                             int tag_scope_depth_p1, int size) {
  psx_type_t *type = ps_type_new(ps_type_kind_from_tag_kind(tag_kind));
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
  if (type->deref_size > 0) return type->deref_size;
  if (type->kind == PSX_TYPE_POINTER || type->kind == PSX_TYPE_ARRAY) {
    int s = ps_type_sizeof(type->base);
    return s > 0 ? s : type->elem_size;
  }
  return 0;
}

int ps_type_is_pointer(const psx_type_t *type) {
  if (!type) return 0;
  return type->kind == PSX_TYPE_POINTER || type->kind == PSX_TYPE_ARRAY;
}

int ps_type_is_unsigned(const psx_type_t *type) {
  if (!type) return 0;
  return type->is_unsigned ? 1 : 0;
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
  if (a->is_const_qualified != b->is_const_qualified ||
      a->is_volatile_qualified != b->is_volatile_qualified ||
      a->is_atomic != b->is_atomic ||
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
          !type_tag_identity_matches(a, b))
        return 0;
      return a->scalar_kind == b->scalar_kind && a->size == b->size;
    case PSX_TYPE_FLOAT:
    case PSX_TYPE_COMPLEX:
      return a->fp_kind == b->fp_kind && a->size == b->size;
    case PSX_TYPE_POINTER:
      return ps_type_shape_matches(a->base, b->base);
    case PSX_TYPE_ARRAY:
      return a->array_len == b->array_len && a->is_vla == b->is_vla &&
             ps_type_shape_matches(a->base, b->base);
    case PSX_TYPE_STRUCT:
    case PSX_TYPE_UNION:
      return a->tag_kind == b->tag_kind && a->tag_len == b->tag_len &&
             strncmp(a->tag_name ? a->tag_name : "",
                     b->tag_name ? b->tag_name : "",
                     (size_t)a->tag_len) == 0 &&
             (a->tag_scope_depth_p1 == 0 || b->tag_scope_depth_p1 == 0 ||
              a->tag_scope_depth_p1 == b->tag_scope_depth_p1);
    case PSX_TYPE_FUNCTION:
      if (a->param_count != b->param_count ||
          a->is_variadic_function != b->is_variadic_function ||
          !ps_type_shape_matches(a->base, b->base)) {
        return 0;
      }
      for (int i = 0; i < a->param_count && i < 16; i++) {
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

static int type_derivation_to_function_matches(const psx_type_t *a,
                                               const psx_type_t *b) {
  while (a && b && a->kind != PSX_TYPE_FUNCTION &&
         b->kind != PSX_TYPE_FUNCTION) {
    if (a->kind != b->kind ||
        (a->kind != PSX_TYPE_POINTER && a->kind != PSX_TYPE_ARRAY) ||
        a->is_const_qualified != b->is_const_qualified ||
        a->is_volatile_qualified != b->is_volatile_qualified) {
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
                               const psx_type_t *type, int depth) {
  if (!type || depth > 64) {
    w->failed = 1;
    return;
  }
  if (type->is_const_qualified) canonical_sig_lit(w, "k");
  if (type->is_volatile_qualified) canonical_sig_lit(w, "V");
  if (type->is_atomic) canonical_sig_lit(w, "A");

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
      canonical_sig_type(w, type->base, depth + 1);
      canonical_sig_lit(w, ">");
      return;
    case PSX_TYPE_ARRAY:
      canonical_sig_lit(w, "a");
      canonical_sig_uint(w, (unsigned int)(type->array_len > 0 ? type->array_len : 0));
      canonical_sig_lit(w, "<");
      canonical_sig_type(w, type->base, depth + 1);
      canonical_sig_lit(w, ">");
      return;
    case PSX_TYPE_FUNCTION:
      if (type->param_count < 0 || type->param_count > 16) {
        w->failed = 1;
        return;
      }
      canonical_sig_type(w, type->base, depth + 1);
      canonical_sig_lit(w, "(");
      for (int i = 0; i < type->param_count && i < 16; i++) {
        if (i > 0) canonical_sig_lit(w, ",");
        canonical_sig_type(w, type->param_types[i], depth + 1);
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
  canonical_sig_type(&writer, type, 0);
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
  unqualified_control.is_const_qualified = 0;
  unqualified_control.is_volatile_qualified = 0;
  unqualified_association.is_const_qualified = 0;
  unqualified_association.is_volatile_qualified = 0;
  control = &unqualified_control;
  association = &unqualified_association;
  const psx_type_t *control_function = ps_type_find_function(control);
  const psx_type_t *association_function =
      ps_type_find_function(association);
  if (!control_function && !association_function)
    return ps_type_shape_matches(control, association);
  if (!control_function || !association_function) return 0;
  if (!type_derivation_to_function_matches(control, association)) return 0;
  return ps_type_shape_matches(control_function, association_function);
}

psx_type_t *ps_type_generic_control(const psx_type_t *control) {
  psx_type_t *type = ps_type_clone(control);
  if (!type) return NULL;
  if (type->kind == PSX_TYPE_ARRAY) {
    int deref_size = ps_type_sizeof(type->base);
    type = ps_type_new_pointer(type->base, deref_size);
  } else if (type->kind == PSX_TYPE_FUNCTION) {
    type = ps_type_new_pointer(type, 0);
  }
  ps_type_normalize_integer_identity(type);
  return type;
}

int ps_type_generic_select_index(
    const psx_type_t *control, psx_type_t *const *association_types,
    const unsigned char *is_default, int association_count) {
  psx_type_t *normalized = ps_type_generic_control(control);
  if (!normalized || !association_types || association_count <= 0) return -1;
  int default_index = -1;
  for (int i = 0; i < association_count; i++) {
    if (is_default && is_default[i]) {
      if (default_index < 0) default_index = i;
      continue;
    }
    if (ps_type_generic_matches(normalized, association_types[i])) return i;
  }
  return default_index;
}

int ps_type_is_tag_aggregate(const psx_type_t *type) {
  if (!type) return 0;
  return type->kind == PSX_TYPE_STRUCT || type->kind == PSX_TYPE_UNION;
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

static int type_is_pointer_to_array_type(const psx_type_t *type) {
  return type && type->kind == PSX_TYPE_POINTER &&
         type->base && type->base->kind == PSX_TYPE_ARRAY;
}

static int type_is_array_of_pointer_to_array(const psx_type_t *type) {
  return type && type->kind == PSX_TYPE_ARRAY &&
         type_is_pointer_to_array_type(type->base);
}

static int type_is_array_of_tag_aggregate(const psx_type_t *type) {
  return type && type->kind == PSX_TYPE_ARRAY &&
         ps_type_is_tag_aggregate(type->base);
}

static int type_carries_ptr_array_pointee_after_deref(const psx_type_t *type) {
  return type_is_array_of_pointer_to_array(type) ||
         type_is_array_of_tag_aggregate(type);
}

static int type_structural_base_deref_size(const psx_type_t *type,
                                           int *structurally_known) {
  if (structurally_known) *structurally_known = 0;
  if (!psx_type_is_pointer_view_type(type)) return 0;
  const psx_type_t *cur = type;
  while (psx_type_is_pointer_view_type(cur)) {
    if (!cur->base) return 0;
    cur = cur->base;
  }
  if (structurally_known) *structurally_known = 1;
  return ps_type_sizeof(cur);
}

int ps_type_pointer_view_structural_base_deref_size(const psx_type_t *type) {
  int structurally_known = 0;
  int base_deref_size =
      type_structural_base_deref_size(type, &structurally_known);
  return structurally_known && base_deref_size > 0 ? base_deref_size : 0;
}

static int type_structural_ptr_array_pointee_bytes(const psx_type_t *type,
                                                   int *structurally_known) {
  if (structurally_known) *structurally_known = 0;
  if (!psx_type_is_pointer_view_type(type)) return 0;
  if (type->kind == PSX_TYPE_POINTER) {
    if (!type->base) return 0;
    if (type->base->kind != PSX_TYPE_ARRAY) {
      if (structurally_known) *structurally_known = 1;
      return 0;
    }
    if (structurally_known) *structurally_known = 1;
    int size = ps_type_sizeof(type->base);
    if (size <= 0) size = ps_type_deref_size(type);
    return size;
  }
  if (type->kind == PSX_TYPE_ARRAY) {
    if (!type->base) return 0;
    if (structurally_known) *structurally_known = 1;
    if (!type_carries_ptr_array_pointee_after_deref(type)) return 0;
    if (type_is_array_of_pointer_to_array(type)) {
      int size = ps_type_sizeof(type->base->base);
      if (size <= 0) size = ps_type_deref_size(type->base);
      return size;
    }
    return ps_type_sizeof(type);
  }
  return 0;
}

int ps_type_pointer_view_structural_ptr_array_pointee_bytes(
    const psx_type_t *type) {
  int structurally_known = 0;
  int bytes =
      type_structural_ptr_array_pointee_bytes(type, &structurally_known);
  return structurally_known && bytes > 0 ? bytes : 0;
}

int psx_type_copy_runtime_vla_stride_metadata(psx_type_t *dst,
                                              const psx_type_t *src) {
  if (!dst || !src) return 0;
  if (psx_type_pointer_view_vla_row_stride_frame_off(src) == 0 &&
      !(src->kind == PSX_TYPE_ARRAY && src->is_vla)) {
    return 0;
  }
  dst->is_vla = 1;
  dst->vla_runtime_strides = src->vla_runtime_strides;
  return 1;
}

void psx_type_sync_pointer_to_array_metadata_from_base(psx_type_t *type) {
  if (!type || type->kind != PSX_TYPE_POINTER) return;
  if (type->base && type->base->kind == PSX_TYPE_ARRAY) {
    int pointee_size = ps_type_sizeof(type->base);
    if (pointee_size > 0) type->deref_size = pointee_size;
  }
}

static void type_clear_stride_outputs(int *inner_stride, int *next_stride,
                                      int *extra_strides,
                                      int *extra_strides_count) {
  if (inner_stride) *inner_stride = 0;
  if (next_stride) *next_stride = 0;
  if (extra_strides_count) *extra_strides_count = 0;
  if (extra_strides) {
    for (int i = 0; i < 5; i++) extra_strides[i] = 0;
  }
}

static int type_array_stride_metadata(const psx_type_t *array,
                                      int *inner_stride,
                                      int *next_stride,
                                      int *extra_strides,
                                      int *extra_strides_count) {
  type_clear_stride_outputs(inner_stride, next_stride, extra_strides,
                            extra_strides_count);
  if (!array || array->kind != PSX_TYPE_ARRAY) return 0;
  int strides[7] = {0};
  int count = 0;
  const psx_type_t *cur = array;
  while (cur && cur->kind == PSX_TYPE_ARRAY && count < 7) {
    int stride = ps_type_deref_size(cur);
    if (stride <= 0 && cur->base) stride = ps_type_sizeof(cur->base);
    if (stride <= 0) break;
    strides[count++] = stride;
    cur = cur->base;
  }
  if (count <= 0) return 0;
  if (inner_stride) *inner_stride = strides[0];
  if (next_stride) *next_stride = count > 1 ? strides[1] : 0;
  int extra_count = count > 2 ? count - 2 : 0;
  if (extra_count > 5) extra_count = 5;
  if (extra_strides_count) *extra_strides_count = extra_count;
  if (extra_strides) {
    for (int i = 0; i < extra_count; i++) extra_strides[i] = strides[i + 2];
    for (int i = extra_count; i < 5; i++) extra_strides[i] = 0;
  }
  return 1;
}

int ps_type_decl_array_stride_metadata(const psx_type_t *type,
                                        int *outer_stride,
                                        int *mid_stride,
                                        int *extra_strides,
                                        int *extra_strides_count) {
  type_clear_stride_outputs(outer_stride, mid_stride, extra_strides,
                            extra_strides_count);
  if (!type) return 0;
  const psx_type_t *array = NULL;
  if (type->kind == PSX_TYPE_POINTER && type->base &&
      type->base->kind == PSX_TYPE_ARRAY) {
    array = type->base;
  } else if (type->kind == PSX_TYPE_ARRAY && type->base &&
             type->base->kind == PSX_TYPE_ARRAY) {
    array = type->base;
  }
  if (!array) return 0;

  int strides[7] = {0};
  int count = 0;
  for (const psx_type_t *cur = array;
       cur && cur->kind == PSX_TYPE_ARRAY && count < 7;
       cur = cur->base) {
    int stride = ps_type_sizeof(cur);
    if (stride <= 0) break;
    strides[count++] = stride;
  }
  if (count <= 0) return 0;
  if (outer_stride) *outer_stride = strides[0];
  if (mid_stride) *mid_stride = count > 1 ? strides[1] : 0;
  int extra_count = count > 2 ? count - 2 : 0;
  if (extra_count > 5) extra_count = 5;
  if (extra_strides_count) *extra_strides_count = extra_count;
  if (extra_strides) {
    for (int i = 0; i < extra_count; i++)
      extra_strides[i] = strides[i + 2];
    for (int i = extra_count; i < 5; i++) extra_strides[i] = 0;
  }
  return 1;
}

int ps_type_pointer_view_stride_metadata(const psx_type_t *type,
                                          int *inner_stride,
                                          int *next_stride,
                                          int *extra_strides,
                                          int *extra_strides_count) {
  type_clear_stride_outputs(inner_stride, next_stride, extra_strides,
                            extra_strides_count);
  if (!psx_type_is_pointer_view_type(type)) return 0;
  const psx_type_t *array = NULL;
  if (type->kind == PSX_TYPE_POINTER && type->base &&
      type->base->kind == PSX_TYPE_ARRAY) {
    array = type->base;
  } else if (type->kind == PSX_TYPE_ARRAY) {
    int base_carries_nested_shape =
        type->base &&
        (type->base->kind == PSX_TYPE_ARRAY ||
         (type->base->kind == PSX_TYPE_POINTER && type->base->base &&
          type->base->base->kind == PSX_TYPE_ARRAY));
    if (!base_carries_nested_shape) return 0;
    array = type;
  }
  if (!array) return 0;
  return type_array_stride_metadata(array, inner_stride, next_stride,
                                    extra_strides, extra_strides_count);
}

int psx_type_pointer_view_vla_row_stride_frame_off(const psx_type_t *type) {
  if (!psx_type_is_pointer_view_type(type)) return 0;
  return type->vla_row_stride_frame_off;
}

int ps_type_pointer_view_vla_strides_remaining(const psx_type_t *type) {
  if (!psx_type_is_pointer_view_type(type)) return 0;
  return type->vla_strides_remaining > 0 ? type->vla_strides_remaining : 0;
}

int psx_type_vla_row_stride_src_offset(const psx_type_t *type) {
  return type && type->is_vla ? type->vla_row_stride_src_offset : 0;
}

int ps_type_vla_row_stride_elem_size(const psx_type_t *type) {
  return type && type->is_vla && type->vla_row_stride_elem_size > 0
             ? type->vla_row_stride_elem_size
             : 0;
}

int ps_type_vla_param_inner_dim_count(const psx_type_t *type) {
  return type && type->is_vla ? type->vla_param_inner_dim_count : 0;
}

int ps_type_vla_param_inner_dim_const(const psx_type_t *type, int index) {
  if (!type || !type->is_vla || index < 0 || index >= 7) return 0;
  return type->vla_param_inner_dim_consts[index];
}

int ps_type_vla_param_inner_dim_src_offset(const psx_type_t *type, int index) {
  if (!type || !type->is_vla || index < 0 || index >= 7) return 0;
  return type->vla_param_inner_dim_src_offsets[index];
}

static int type_structural_pointer_qual_levels(const psx_type_t *type) {
  if (!type || type->kind != PSX_TYPE_POINTER || !type->base) return 0;
  int levels = ps_type_pointer_depth(type);
  return levels > 0 ? levels : 1;
}

int ps_type_pointer_view_structural_qual_levels(const psx_type_t *type) {
  return type_structural_pointer_qual_levels(type);
}

static unsigned int type_structural_pointer_qual_mask(const psx_type_t *type,
                                                      int is_volatile) {
  if (!type || type->kind != PSX_TYPE_POINTER || !type->base) return 0;
  unsigned int self_mask = is_volatile ? type->pointer_volatile_qual_mask
                                       : type->pointer_const_qual_mask;
  unsigned int mask = self_mask & 1u;
  if (type->base->kind == PSX_TYPE_POINTER)
    mask |= type_structural_pointer_qual_mask(type->base, is_volatile) << 1;
  int depth = ps_type_pointer_depth(type);
  if (depth > 0 && depth < 32) mask &= (1u << depth) - 1u;
  return mask;
}

unsigned int ps_type_pointer_view_structural_qual_mask(
    const psx_type_t *type, int is_volatile) {
  if (!psx_type_is_pointer_view_type(type)) return 0;
  int structural_levels = type_structural_pointer_qual_levels(type);
  if (structural_levels <= 0) return 0;
  return type_structural_pointer_qual_mask(type, is_volatile);
}


void psx_type_copy_common_qualifiers(psx_type_t *dst, const psx_type_t *src) {
  if (!dst || !src) return;
  dst->is_const_qualified = src->is_const_qualified;
  dst->is_volatile_qualified = src->is_volatile_qualified;
  dst->is_atomic = src->is_atomic;
  dst->is_unsigned = src->is_unsigned;
  dst->is_long_long = src->is_long_long;
  dst->is_plain_char = src->is_plain_char;
  dst->is_long_double = src->is_long_double;
}

void ps_type_set_decl_spec_qualifiers(psx_type_t *type,
                                       int is_const_qualified,
                                       int is_volatile_qualified) {
  if (!type) return;
  type->is_const_qualified = is_const_qualified ? 1 : 0;
  type->is_volatile_qualified = is_volatile_qualified ? 1 : 0;

  psx_type_t *value_type = type;
  while (value_type &&
         (value_type->kind == PSX_TYPE_POINTER ||
          value_type->kind == PSX_TYPE_ARRAY)) {
    value_type = value_type->base;
  }
  if (!value_type || value_type->kind == PSX_TYPE_FUNCTION) return;
  value_type->is_const_qualified = is_const_qualified ? 1 : 0;
  value_type->is_volatile_qualified = is_volatile_qualified ? 1 : 0;
}

void psx_type_copy_pointer_metadata(psx_type_t *dst, const psx_type_t *src) {
  if (!dst || !src) return;
  dst->deref_size = src->deref_size;
  dst->pointer_qual_levels = src->pointer_qual_levels;
  dst->pointer_const_qual_mask = src->pointer_const_qual_mask;
  dst->pointer_volatile_qual_mask = src->pointer_volatile_qual_mask;
  dst->vla_row_stride_frame_off = src->vla_row_stride_frame_off;
  dst->vla_strides_remaining = src->vla_strides_remaining;
  dst->vla_row_stride_src_offset = src->vla_row_stride_src_offset;
  dst->vla_row_stride_elem_size = src->vla_row_stride_elem_size;
  dst->vla_param_inner_dim_count = src->vla_param_inner_dim_count;
  for (int i = 0; i < 7; i++) {
    dst->vla_param_inner_dim_consts[i] = src->vla_param_inner_dim_consts[i];
    dst->vla_param_inner_dim_src_offsets[i] =
        src->vla_param_inner_dim_src_offsets[i];
  }
  dst->vla_runtime_strides = src->vla_runtime_strides;
}
