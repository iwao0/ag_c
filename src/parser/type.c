#include "type.h"
#include "arena.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static void type_sync_array_stride_metadata_from_base(psx_type_t *type);

psx_type_t *psx_type_new(psx_type_kind_t kind) {
  psx_type_t *type = arena_alloc(sizeof(psx_type_t));
  type->kind = kind;
  type->scalar_kind = TK_EOF;
  type->tag_kind = TK_EOF;
  return type;
}

psx_type_t *psx_type_new_integer(token_kind_t scalar_kind, int size, int is_unsigned) {
  psx_type_t *type = psx_type_new(scalar_kind == TK_BOOL ? PSX_TYPE_BOOL : PSX_TYPE_INTEGER);
  type->scalar_kind = scalar_kind;
  type->size = size;
  type->align = size > 0 ? size : 1;
  if (type->align > 8) type->align = 8;
  type->is_unsigned = is_unsigned ? 1 : 0;
  if (scalar_kind == TK_CHAR) type->is_plain_char = 1;
  return type;
}

psx_type_t *psx_type_new_float(tk_float_kind_t fp_kind, int size) {
  psx_type_t *type = psx_type_new(PSX_TYPE_FLOAT);
  type->fp_kind = fp_kind;
  type->size = size;
  type->align = size > 0 ? size : 1;
  if (type->align > 8) type->align = 8;
  if (fp_kind == TK_FLOAT_KIND_LONG_DOUBLE) type->is_long_double = 1;
  return type;
}

psx_type_t *psx_type_new_pointer(psx_type_t *base, int deref_size) {
  psx_type_t *type = psx_type_new(PSX_TYPE_POINTER);
  type->base = base;
  type->size = 8;
  type->align = 8;
  type->deref_size = deref_size;
  type->pointer_qual_levels = 1;
  if (base) {
    type->base_deref_size = ps_type_deref_size(base);
    if (type->base_deref_size <= 0) type->base_deref_size = ps_type_sizeof(base);
    type->pointee_fp_kind = base->fp_kind;
  }
  return type;
}

static psx_type_t *type_from_legacy_param_code(
    unsigned short fp_mask, unsigned short int_mask, int index) {
  unsigned short fp_code = (fp_mask >> (2 * index)) & 3u;
  if (fp_code == 1u)
    return psx_type_new_float(TK_FLOAT_KIND_FLOAT, 4);
  if (fp_code == 2u)
    return psx_type_new_float(TK_FLOAT_KIND_DOUBLE, 8);

  unsigned short int_code = (int_mask >> (2 * index)) & 3u;
  if (int_code == 1u)
    return psx_type_new_integer(TK_EOF, 4, 0);
  if (int_code == 2u)
    return psx_type_new_integer(TK_EOF, 8, 0);
  if (int_code == 3u) {
    psx_type_t *void_type = psx_type_new(PSX_TYPE_VOID);
    void_type->scalar_kind = TK_VOID;
    return psx_type_new_pointer(void_type, 1);
  }
  return NULL;
}

static void type_sync_function_params_from_signature(
    psx_type_t *function_type, psx_decl_funcptr_sig_t sig) {
  if (!function_type || function_type->kind != PSX_TYPE_FUNCTION) return;
  psx_funcptr_signature_t legacy = sig.function.callable.signature;
  int param_count = legacy.nargs_fixed;
  for (int i = 0; i < 8; i++) {
    unsigned int shift = (unsigned int)(2 * i);
    if (((legacy.param_fp_mask >> shift) & 3u) != 0 ||
        ((legacy.param_int_mask >> shift) & 3u) != 0) {
      param_count = i + 1;
    }
  }
  function_type->param_count = param_count;
  function_type->is_variadic_function = legacy.is_variadic ? 1 : 0;
  int tracked = function_type->param_count > 16
                    ? 16 : function_type->param_count;
  for (int i = 0; i < tracked; i++) {
    if (!function_type->param_types[i]) {
      function_type->param_types[i] = type_from_legacy_param_code(
          legacy.param_fp_mask, legacy.param_int_mask, i);
    }
  }
}

psx_type_t *psx_type_new_function(psx_type_t *return_type,
                                  psx_decl_funcptr_sig_t sig) {
  psx_type_t *type = psx_type_new(PSX_TYPE_FUNCTION);
  type->base = return_type;
  type->funcptr_sig = ps_decl_funcptr_sig_clone(sig);
  return type;
}

void psx_type_set_function_params(psx_type_t *function_type,
                                  psx_type_t *const *param_types,
                                  int param_count, int is_variadic) {
  if (!function_type || function_type->kind != PSX_TYPE_FUNCTION) return;
  if (param_count < 0) param_count = 0;
  function_type->param_count = param_count;
  function_type->is_variadic_function = is_variadic ? 1 : 0;
  for (int i = 0; i < 16; i++) {
    function_type->param_types[i] =
        i < param_count && param_types ? psx_type_clone(param_types[i]) : NULL;
  }
}

const psx_type_t *psx_type_find_function(const psx_type_t *type) {
  while (type) {
    if (type->kind == PSX_TYPE_FUNCTION) return type;
    if (type->kind != PSX_TYPE_POINTER && type->kind != PSX_TYPE_ARRAY)
      return NULL;
    type = type->base;
  }
  return NULL;
}

void psx_type_complete_function_params(psx_type_t *type) {
  if (!type) return;
  psx_decl_funcptr_sig_t sig = type->funcptr_sig;
  if (!ps_decl_funcptr_sig_has_payload(sig))
    sig = ps_type_funcptr_signature(type);
  while (type && type->kind != PSX_TYPE_FUNCTION) {
    if (type->kind != PSX_TYPE_POINTER && type->kind != PSX_TYPE_ARRAY)
      return;
    type = type->base;
  }
  if (!type || type->kind != PSX_TYPE_FUNCTION) return;
  type_sync_function_params_from_signature(type, sig);
}

static psx_funcptr_type_shape_t type_function_shape_from_canonical(
    const psx_type_t *function);

static psx_funcptr_signature_t type_function_signature_from_canonical(
    const psx_type_t *function) {
  psx_funcptr_signature_t signature = {0};
  if (!function || function->kind != PSX_TYPE_FUNCTION) return signature;
  signature.is_variadic = function->is_variadic_function ? 1 : 0;
  signature.nargs_fixed = function->param_count;
  for (int i = 0; i < function->param_count && i < 8; i++) {
    const psx_type_t *param = function->param_types[i];
    if (!param) continue;
    if (param->kind == PSX_TYPE_FLOAT || param->kind == PSX_TYPE_COMPLEX) {
      unsigned short code =
          param->fp_kind == TK_FLOAT_KIND_FLOAT ? 1u : 2u;
      signature.param_fp_mask |= (unsigned short)(code << (2 * i));
    } else if (param->kind == PSX_TYPE_POINTER ||
               param->kind == PSX_TYPE_ARRAY ||
               param->kind == PSX_TYPE_FUNCTION) {
      signature.param_int_mask |= (unsigned short)(3u << (2 * i));
    } else if (param->kind == PSX_TYPE_BOOL ||
               param->kind == PSX_TYPE_INTEGER) {
      unsigned short code = ps_type_sizeof(param) > 4 ? 2u : 1u;
      signature.param_int_mask |= (unsigned short)(code << (2 * i));
    }
  }
  return signature;
}

static const psx_type_t *type_array_leaf(const psx_type_t *type) {
  while (type && type->kind == PSX_TYPE_ARRAY) type = type->base;
  return type;
}

static psx_ret_pointee_array_t type_return_pointee_array(
    const psx_type_t *type) {
  if (!type || type->kind != PSX_TYPE_ARRAY) return (psx_ret_pointee_array_t){0};
  int first_dim = type->array_len;
  int second_dim = 0;
  const psx_type_t *inner = type->base;
  if (inner && inner->kind == PSX_TYPE_ARRAY) second_dim = inner->array_len;
  const psx_type_t *leaf = type_array_leaf(type);
  int elem_size = ps_type_sizeof(leaf);
  return psx_ret_pointee_array_make(first_dim, second_dim, elem_size);
}

static psx_funcptr_return_shape_t type_return_shape_from_canonical(
    const psx_type_t *type, psx_funcptr_returned_func_t *out_returned_funcptr) {
  psx_funcptr_return_shape_t ret = {0};
  if (out_returned_funcptr)
    *out_returned_funcptr = (psx_funcptr_returned_func_t){0};
  if (!type) return ret;

  if (type->kind == PSX_TYPE_POINTER) {
    const psx_type_t *pointee = type->base;
    if (pointee && pointee->kind == PSX_TYPE_FUNCTION) {
      if (out_returned_funcptr) {
        *out_returned_funcptr = psx_funcptr_returned_func_from_type_shape(
            type_function_shape_from_canonical(pointee));
        *out_returned_funcptr =
            psx_funcptr_returned_func_mark(*out_returned_funcptr);
      }
      return ret;
    }
    ret.is_data_pointer = 1;
    if (pointee && pointee->kind == PSX_TYPE_ARRAY)
      ret.pointee_array = type_return_pointee_array(pointee);
    const psx_type_t *leaf = type_array_leaf(pointee);
    while (leaf && leaf->kind == PSX_TYPE_POINTER) leaf = leaf->base;
    if (leaf && leaf->kind == PSX_TYPE_FLOAT)
      ret.pointee_fp_kind = leaf->fp_kind;
    return ret;
  }

  switch (type->kind) {
    case PSX_TYPE_VOID:
      ret.is_void = 1;
      break;
    case PSX_TYPE_BOOL:
    case PSX_TYPE_INTEGER:
      ret.int_width = ps_type_sizeof(type) >= 8 ? 8 : 4;
      break;
    case PSX_TYPE_FLOAT:
      ret.fp_kind = type->fp_kind;
      break;
    case PSX_TYPE_COMPLEX:
      ret.fp_kind = type->fp_kind;
      ret.is_complex = 1;
      break;
    default:
      break;
  }
  return ret;
}

static psx_funcptr_type_shape_t type_function_shape_from_canonical(
    const psx_type_t *function) {
  psx_funcptr_type_shape_t shape = {0};
  if (!function || function->kind != PSX_TYPE_FUNCTION) return shape;
  shape.callable.signature = function->param_count > 0 ||
                                     function->is_variadic_function
                                 ? type_function_signature_from_canonical(function)
                                 : function->funcptr_sig.function.callable.signature;
  shape.callable.return_shape = type_return_shape_from_canonical(
      function->base, &shape.returned_funcptr);
  return shape;
}

psx_decl_funcptr_sig_t ps_type_funcptr_signature(const psx_type_t *type) {
  const psx_type_t *function = psx_type_find_function(type);
  if (function) {
    psx_decl_funcptr_sig_t sig = {0};
    sig.function = type_function_shape_from_canonical(function);
    return sig;
  }
  return type ? ps_decl_funcptr_sig_clone(type->funcptr_sig)
              : (psx_decl_funcptr_sig_t){0};
}

static psx_type_t *type_return_from_funcptr_shape(
    psx_type_t *base, psx_funcptr_type_shape_t shape) {
  psx_funcptr_return_shape_t ret = shape.callable.return_shape;
  psx_type_t *return_type = base;
  if (ret.is_data_pointer && return_type &&
      return_type->kind == PSX_TYPE_POINTER) {
    return_type = return_type->base;
  }
  if ((ret.is_data_pointer || psx_ret_pointee_array_has_dims(ret.pointee_array)) &&
      ret.pointee_fp_kind != TK_FLOAT_KIND_NONE) {
    return_type = psx_type_new_float(
        ret.pointee_fp_kind,
        ret.pointee_fp_kind == TK_FLOAT_KIND_FLOAT ? 4 : 8);
  } else if (!ret.is_data_pointer && ret.fp_kind != TK_FLOAT_KIND_NONE) {
    return_type = psx_type_new_float(
        ret.fp_kind, ret.fp_kind == TK_FLOAT_KIND_FLOAT ? 4 : 8);
  } else if (!ret.is_data_pointer && ret.is_void) {
    return_type = psx_type_new(PSX_TYPE_VOID);
    return_type->scalar_kind = TK_VOID;
  } else if (!ret.is_data_pointer && ret.int_width > 0 &&
             (!return_type || return_type->kind == PSX_TYPE_INTEGER ||
              return_type->kind == PSX_TYPE_BOOL)) {
    int is_unsigned = return_type ? return_type->is_unsigned : 0;
    return_type = psx_type_new_integer(TK_EOF, ret.int_width, is_unsigned);
  }
  psx_ret_pointee_array_t ret_array = ret.pointee_array;
  if (psx_ret_pointee_array_has_dims(ret_array)) {
    while (return_type && return_type->kind == PSX_TYPE_ARRAY)
      return_type = return_type->base;
    if (return_type &&
        (return_type->kind == PSX_TYPE_INTEGER ||
         return_type->kind == PSX_TYPE_BOOL) &&
        ret_array.elem_size > 0 &&
        ps_type_sizeof(return_type) != ret_array.elem_size) {
      return_type = psx_type_new_integer(
          return_type->kind == PSX_TYPE_BOOL ? TK_BOOL : return_type->scalar_kind,
          ret_array.elem_size, return_type->is_unsigned);
    }
    return_type = psx_type_wrap_ret_pointee_array_base(return_type, ret_array);
  }
  if (shape.callable.return_shape.is_data_pointer) {
    int deref_size = ps_type_sizeof(return_type);
    if (deref_size <= 0) deref_size = 8;
    return_type = psx_type_new_pointer(return_type, deref_size);
    return_type->base_deref_size = deref_size;
    if (psx_ret_pointee_array_has_dims(ret_array)) {
      return_type->funcptr_sig.function.callable.return_shape.pointee_array =
          ret_array;
      psx_type_sync_pointer_to_array_metadata_from_base(return_type);
    }
  }
  if (psx_funcptr_returned_func_has_payload(shape.returned_funcptr)) {
    psx_funcptr_type_shape_t returned_shape =
        psx_funcptr_returned_func_as_type_shape(shape.returned_funcptr);
    psx_decl_funcptr_sig_t returned_sig = {0};
    returned_sig.function = psx_funcptr_type_shape_clone(returned_shape);
    psx_type_t *returned_function = psx_type_new_function(
        type_return_from_funcptr_shape(base, returned_shape), returned_sig);
    return_type = psx_type_new_pointer(returned_function, 0);
    return_type->funcptr_sig = ps_decl_funcptr_sig_clone(returned_sig);
  }
  return return_type;
}

psx_type_t *psx_type_new_funcptr(psx_decl_funcptr_sig_t sig,
                                 int object_pointer_levels) {
  if (!ps_decl_funcptr_sig_has_payload(sig)) return NULL;
  if (object_pointer_levels < 1) object_pointer_levels = 1;
  psx_type_t *return_type = psx_type_new_funcptr_return_type(sig);
  psx_type_t *function = psx_type_new_function(return_type, sig);
  psx_type_t *type = psx_type_new_pointer(function, 0);
  type->funcptr_sig = ps_decl_funcptr_sig_clone(sig);
  for (int level = 1; level < object_pointer_levels; level++) {
    type = psx_type_new_pointer(type, 8);
    type->funcptr_sig = ps_decl_funcptr_sig_clone(sig);
  }
  return type;
}

psx_type_t *psx_type_new_funcptr_return_type(psx_decl_funcptr_sig_t sig) {
  if (!ps_decl_funcptr_sig_has_payload(sig)) return NULL;
  return type_return_from_funcptr_shape(NULL, sig.function);
}

psx_type_t *psx_type_attach_funcptr_signature(
    psx_type_t *object_type, psx_decl_funcptr_sig_t sig) {
  if (!object_type || !ps_decl_funcptr_sig_has_payload(sig)) return object_type;
  psx_type_canonicalize_flat_pointer_to_array(object_type);

  psx_type_t *cur = object_type;
  psx_type_t *pointers[16] = {0};
  int pointer_count = 0;
  psx_type_t *last_array = NULL;
  while (cur && (cur->kind == PSX_TYPE_POINTER || cur->kind == PSX_TYPE_ARRAY)) {
    cur->funcptr_sig = ps_decl_funcptr_sig_clone(sig);
    if (cur->kind == PSX_TYPE_POINTER && pointer_count < 16)
      pointers[pointer_count++] = cur;
    if (cur->kind == PSX_TYPE_ARRAY) last_array = cur;
    cur = cur->base;
  }
  if (last_array && last_array->base &&
      last_array->base->kind != PSX_TYPE_POINTER &&
      (object_type->kind == PSX_TYPE_ARRAY ||
       !sig.function.callable.return_shape.is_data_pointer)) {
    psx_type_t *return_type = type_return_from_funcptr_shape(
        last_array->base, sig.function);
    psx_type_t *function = psx_type_new_function(return_type, sig);
    psx_type_t *element_pointer = psx_type_new_pointer(function, 0);
    element_pointer->funcptr_sig = ps_decl_funcptr_sig_clone(sig);
    last_array->base = element_pointer;
    last_array->elem_size = 8;
    last_array->deref_size = 8;
    return object_type;
  }
  int function_pointer_index = pointer_count - 1;
  if (sig.function.callable.return_shape.is_data_pointer &&
      function_pointer_index > 0) {
    function_pointer_index--;
  }
  psx_type_t *function_pointer =
      function_pointer_index >= 0 ? pointers[function_pointer_index] : NULL;
  if (!function_pointer) return object_type;
  if (function_pointer->base && function_pointer->base->kind == PSX_TYPE_FUNCTION) {
    function_pointer->base->funcptr_sig = ps_decl_funcptr_sig_clone(sig);
    return object_type;
  }

  psx_type_t *return_type = type_return_from_funcptr_shape(
      function_pointer->base, sig.function);
  function_pointer->base = psx_type_new_function(return_type, sig);
  function_pointer->deref_size = 0;
  return object_type;
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
    value = psx_type_new_tag(tag_kind, tag_name, tag_len,
                             tag_scope_depth_p1, value_size);
  } else if (fp_kind != TK_FLOAT_KIND_NONE) {
    value = psx_type_new_float(fp_kind, value_size);
  } else {
    value = psx_type_new_integer(TK_EOF, value_size, is_unsigned);
  }

  psx_type_t *element = value;
  if (is_pointer) {
    element = psx_type_new_pointer(value, value_size);
    element->base_deref_size = value_size;
  }
  if (!is_array) return element;

  int storage_elem_size = is_pointer ? 8 : elem_size;
  if (storage_elem_size <= 0) storage_elem_size = ps_type_sizeof(element);
  if (storage_elem_size <= 0) storage_elem_size = 1;
  int array_len = object_size > 0 && object_size % storage_elem_size == 0
                      ? object_size / storage_elem_size
                      : 0;
  psx_type_t *array = psx_type_new_array(
      element, array_len, object_size, storage_elem_size, 0);
  array->base_deref_size = value_size;
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

psx_type_t *psx_type_wrap_pointer_levels(psx_type_t *base, int levels,
                                          int top_deref_size,
                                          int base_deref_size,
                                          unsigned int const_mask,
                                          unsigned int volatile_mask) {
  if (levels <= 0) return base;
  psx_type_t *type = base;
  int deep_base_size = base_deref_size > 0 ? base_deref_size : ps_type_sizeof(base);
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
    psx_type_t *ptr = psx_type_new_pointer(type, deref_size);
    ptr->pointer_qual_levels = level;
    ptr->base_deref_size = deep_base_size;
    ptr->pointer_const_qual_mask =
        pointer_mask_for_subtree(const_mask, levels, level);
    ptr->pointer_volatile_qual_mask =
        pointer_mask_for_subtree(volatile_mask, levels, level);
    type = ptr;
  }
  return type;
}

psx_type_t *psx_type_new_array(psx_type_t *base, int array_len, int size, int elem_size, int is_vla) {
  psx_type_t *type = psx_type_new(PSX_TYPE_ARRAY);
  type->base = base;
  type->array_len = array_len;
  type->size = size;
  type->align = base && base->align > 0 ? base->align : 1;
  type->elem_size = elem_size;
  type->deref_size = elem_size;
  type->is_vla = is_vla ? 1 : 0;
  if (base) {
    type->pointee_fp_kind = base->pointee_fp_kind != TK_FLOAT_KIND_NONE
                                ? base->pointee_fp_kind
                                : base->fp_kind;
  }
  return type;
}

int psx_type_complete_array(psx_type_t *type, int array_len) {
  if (!type || type->kind != PSX_TYPE_ARRAY || type->is_vla ||
      array_len <= 0 || !type->base) return 0;
  int child_size = ps_type_sizeof(type->base);
  if (child_size <= 0 || array_len > INT_MAX / child_size) return 0;
  if (type->array_len > 0 && type->array_len != array_len) return 0;
  type->array_len = array_len;
  type->size = array_len * child_size;
  type->elem_size = child_size;
  type->deref_size = child_size;
  type_sync_array_stride_metadata_from_base(type);
  return 1;
}

psx_type_t *psx_type_clone(const psx_type_t *src) {
  if (!src) return NULL;
  psx_type_t *dst = psx_type_new(src->kind);
  *dst = *src;
  dst->base = psx_type_clone(src->base);
  for (int i = 0; i < src->param_count && i < 16; i++)
    dst->param_types[i] = psx_type_clone(src->param_types[i]);
  dst->funcptr_sig = ps_decl_funcptr_sig_clone(src->funcptr_sig);
  return dst;
}

psx_type_t *psx_type_clone_persistent(const psx_type_t *src) {
  if (!src) return NULL;
  psx_type_t *dst = calloc(1, sizeof(psx_type_t));
  if (!dst) return NULL;
  *dst = *src;
  dst->base = psx_type_clone_persistent(src->base);
  for (int i = 0; i < src->param_count && i < 16; i++)
    dst->param_types[i] = psx_type_clone_persistent(src->param_types[i]);
  dst->funcptr_sig = ps_decl_funcptr_sig_clone(src->funcptr_sig);
  return dst;
}

static psx_type_t *type_build_array_size_chain(
    psx_type_t *base, const int *sizes, int size_count, int leaf_size) {
  if (!base || !sizes || size_count <= 0 || leaf_size <= 0) return base;
  psx_type_t *type = base;
  for (int i = size_count - 1; i >= 0; i--) {
    int total_size = sizes[i];
    int child_size = i + 1 < size_count ? sizes[i + 1]
                                        : ps_type_sizeof(type);
    if (child_size <= 0) child_size = leaf_size;
    if (total_size <= 0 || child_size <= 0 || total_size < child_size)
      continue;
    int array_len = total_size / child_size;
    if (array_len <= 0) array_len = 1;
    psx_type_t *array =
        psx_type_new_array(type, array_len, total_size, child_size, 0);
    array->base_deref_size = leaf_size;
    array->outer_stride = child_size;
    array->mid_stride = i + 2 < size_count ? sizes[i + 2] : 0;
    int extra_count = 0;
    for (int j = i + 3; j < size_count && extra_count < 5; j++)
      array->extra_strides[extra_count++] = sizes[j];
    array->extra_strides_count = (unsigned char)extra_count;
    type = array;
  }
  return type;
}

static int type_normalize_row_sizes(int object_size, const int *row_sizes,
                                    int row_size_count, int *sizes,
                                    int include_object_size) {
  int count = 0;
  if (include_object_size && object_size > 0) sizes[count++] = object_size;
  for (int i = 0; i < row_size_count && count < 8; i++) {
    int size = row_sizes ? row_sizes[i] : 0;
    if (size <= 0) continue;
    if (count > 0 && size >= sizes[count - 1]) continue;
    sizes[count++] = size;
  }
  return count;
}

static void type_sync_array_stride_metadata_from_base(psx_type_t *type) {
  for (psx_type_t *owner = type;
       owner && owner->kind == PSX_TYPE_ARRAY; owner = owner->base) {
    int strides[7] = {0};
    int count = 0;
    for (const psx_type_t *array = owner;
         array && array->kind == PSX_TYPE_ARRAY && count < 7;
         array = array->base) {
      int stride = ps_type_deref_size(array);
      if (stride <= 0 && array->base) stride = ps_type_sizeof(array->base);
      if (stride <= 0) break;
      strides[count++] = stride;
    }
    owner->outer_stride = count > 0 ? strides[0] : 0;
    owner->mid_stride = count > 1 ? strides[1] : 0;
    int extra_count = count > 2 ? count - 2 : 0;
    if (extra_count > 5) extra_count = 5;
    owner->extra_strides_count = (unsigned char)extra_count;
    for (int i = 0; i < extra_count; i++)
      owner->extra_strides[i] = strides[i + 2];
    for (int i = extra_count; i < 5; i++) owner->extra_strides[i] = 0;
  }
}

psx_type_t *psx_type_rebuild_array_shape(psx_type_t *type, int object_size,
                                          const int *row_sizes,
                                          int row_size_count, int leaf_size) {
  if (!type || !row_sizes || row_size_count <= 0 || leaf_size <= 0)
    return type;

  int sizes[8] = {0};
  if (type->kind == PSX_TYPE_ARRAY) {
    psx_type_t *old_outer = type;
    psx_type_t *base = type;
    while (base && base->kind == PSX_TYPE_ARRAY) base = base->base;
    int size_count = type_normalize_row_sizes(
        object_size, row_sizes, row_size_count, sizes, 1);
    psx_type_t *rebuilt =
        type_build_array_size_chain(base, sizes, size_count, leaf_size);
    if (rebuilt && rebuilt->kind == PSX_TYPE_ARRAY) {
      psx_type_copy_common_qualifiers(rebuilt, old_outer);
      rebuilt->fp_kind = old_outer->fp_kind;
      rebuilt->pointee_fp_kind = old_outer->pointee_fp_kind;
      rebuilt->funcptr_sig =
          ps_decl_funcptr_sig_clone(old_outer->funcptr_sig);
      rebuilt->type_sig = old_outer->type_sig;
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
  int size_count = type_normalize_row_sizes(
      object_size, row_sizes, row_size_count, sizes, 0);
  psx_type_t *rebuilt =
      type_build_array_size_chain(base, sizes, size_count, leaf_size);
  if (!rebuilt || rebuilt == base) return type;
  owner->base = rebuilt;
  owner->deref_size = ps_type_sizeof(rebuilt);
  const psx_type_t *array_leaf = rebuilt;
  while (array_leaf && array_leaf->kind == PSX_TYPE_ARRAY)
    array_leaf = array_leaf->base;
  if (array_leaf && array_leaf->kind == PSX_TYPE_POINTER) {
    int base_deref_size =
        ps_type_pointer_view_structural_base_deref_size(array_leaf);
    owner->base_deref_size = base_deref_size > 0
                                 ? base_deref_size
                                 : array_leaf->base_deref_size;
  } else {
    owner->base_deref_size = leaf_size;
  }
  psx_type_sync_pointer_to_array_metadata_from_base(owner);
  owner->outer_stride = size_count > 0 ? sizes[0] : 0;
  owner->mid_stride = size_count > 1 ? sizes[1] : 0;
  int extra_count = size_count > 2 ? size_count - 2 : 0;
  if (extra_count > 5) extra_count = 5;
  owner->extra_strides_count = (unsigned char)extra_count;
  for (int i = 0; i < extra_count; i++)
    owner->extra_strides[i] = sizes[i + 2];
  for (int i = extra_count; i < 5; i++) owner->extra_strides[i] = 0;
  if (array_leaf && array_leaf->kind == PSX_TYPE_POINTER) {
    owner->ptr_array_pointee_bytes = size_count > 0 ? sizes[0] : 0;
  } else if (size_count > 1) {
    owner->ptr_array_pointee_bytes = 0;
  }
  return type;
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
    psx_type_t *array = psx_type_new_array(result, len, size,
                                            child_size, 0);
    array->base_deref_size = leaf_size;
    array->outer_stride = child_size;
    result = array;
    child_size = size;
  }
  type_sync_array_stride_metadata_from_base(result);
  return result;
}

psx_type_t *psx_type_wrap_array_dims(psx_type_t *base,
                                     const int *dims, int dim_count) {
  if (!base || !dims || dim_count <= 0) return base;
  psx_type_t *result = base;
  int child_size = ps_type_sizeof(base);
  if (child_size <= 0) child_size = 1;
  for (int i = dim_count - 1; i >= 0; i--) {
    int len = dims[i];
    int size = len > 0 ? len * child_size : 0;
    psx_type_t *array = psx_type_new_array(
        result, len, size, child_size, 0);
    array->base_deref_size = ps_type_sizeof(base);
    if (array->base_deref_size <= 0) array->base_deref_size = child_size;
    array->outer_stride = child_size;
    result = array;
    child_size = size;
  }
  type_sync_array_stride_metadata_from_base(result);
  return result;
}

void psx_declarator_shape_init(psx_declarator_shape_t *shape) {
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

int psx_declarator_shape_append_pointer(
    psx_declarator_shape_t *shape, int is_const_qualified,
    int is_volatile_qualified) {
  psx_declarator_op_t *op =
      declarator_shape_append(shape, PSX_DECL_OP_POINTER);
  if (!op) return 0;
  op->is_const_qualified = is_const_qualified ? 1u : 0u;
  op->is_volatile_qualified = is_volatile_qualified ? 1u : 0u;
  return 1;
}

int psx_declarator_shape_append_pointer_levels(
    psx_declarator_shape_t *shape, int levels,
    unsigned int const_mask, unsigned int volatile_mask) {
  if (!shape || levels < 0) return 0;
  for (int level = 0; level < levels; level++) {
    if (!psx_declarator_shape_append_pointer(
            shape, level < 32 && (const_mask & (1u << level)),
            level < 32 && (volatile_mask & (1u << level)))) {
      return 0;
    }
  }
  return 1;
}

int psx_declarator_shape_append_array(
    psx_declarator_shape_t *shape, int array_len) {
  return psx_declarator_shape_append_array_ex(shape, array_len, 0);
}

int psx_declarator_shape_append_array_ex(
    psx_declarator_shape_t *shape, int array_len, int is_incomplete) {
  psx_declarator_op_t *op =
      declarator_shape_append(shape, PSX_DECL_OP_ARRAY);
  if (!op) return 0;
  op->array_len = array_len;
  op->is_incomplete_array = is_incomplete ? 1u : 0u;
  return 1;
}

int psx_declarator_shape_append_vla_array(
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
    if (!psx_declarator_shape_append_array(shape, dims[i])) return 0;
  }
  return 1;
}

int psx_declarator_shape_append_function(
    psx_declarator_shape_t *shape, psx_decl_funcptr_sig_t sig) {
  psx_declarator_op_t *op =
      declarator_shape_append(shape, PSX_DECL_OP_FUNCTION);
  if (!op) return 0;
  op->funcptr_sig = ps_decl_funcptr_sig_clone(sig);
  return 1;
}

int psx_declarator_shape_append_shape(
    psx_declarator_shape_t *shape, const psx_declarator_shape_t *suffix) {
  if (!shape || !suffix || suffix->count < 0) return 0;
  for (int i = 0; i < suffix->count; i++) {
    const psx_declarator_op_t *op = &suffix->ops[i];
    int appended = 0;
    if (op->kind == PSX_DECL_OP_POINTER) {
      appended = psx_declarator_shape_append_pointer(
          shape, op->is_const_qualified, op->is_volatile_qualified);
    } else if (op->kind == PSX_DECL_OP_ARRAY) {
      if (op->is_vla_array)
        appended = psx_declarator_shape_append_vla_array(shape);
      else
        appended = psx_declarator_shape_append_array_ex(
            shape, op->array_len, op->is_incomplete_array);
    } else if (op->kind == PSX_DECL_OP_FUNCTION) {
      appended = psx_declarator_shape_append_function(shape, op->funcptr_sig);
    }
    if (!appended) return 0;
  }
  return 1;
}

int psx_declarator_shape_count_ops(
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

static void type_sync_declarator_derived_metadata(psx_type_t *type) {
  if (!type) return;
  type_sync_declarator_derived_metadata(type->base);
  if (type->kind == PSX_TYPE_ARRAY)
    type_sync_array_stride_metadata_from_base(type);
  else if (type->kind == PSX_TYPE_POINTER)
    psx_type_sync_pointer_to_array_metadata_from_base(type);
}

psx_type_t *psx_type_apply_declarator_shape(
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
      type = psx_type_new_pointer(type, deref_size);
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
      type = psx_type_new_array(type, op->array_len, total_size,
                                elem_size, op->is_vla_array);
    } else if (op->kind == PSX_DECL_OP_FUNCTION) {
      type = psx_type_new_function(type, op->funcptr_sig);
    }
  }
  type_sync_pointer_compat_qual_masks(type);
  type_sync_declarator_derived_metadata(type);
  psx_decl_funcptr_sig_t sig = ps_type_funcptr_signature(type);
  if (ps_decl_funcptr_sig_has_payload(sig))
    type = psx_type_attach_funcptr_signature(type, sig);
  return type;
}

psx_type_t *psx_type_adjust_parameter_type(psx_type_t *type) {
  if (!type) return NULL;
  if (type->kind == PSX_TYPE_ARRAY) {
    int deref_size = type->base ? ps_type_sizeof(type->base) : type->elem_size;
    psx_type_t *adjusted = psx_type_new_pointer(type->base, deref_size);
    adjusted->base_deref_size = type->base_deref_size;
    return adjusted;
  }
  if (type->kind == PSX_TYPE_FUNCTION)
    return psx_type_new_pointer(type, 0);
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
    psx_type_t *replacement = psx_type_clone(canonical_base);
    psx_type_copy_common_qualifiers(replacement, derived);
    if (derived->type_sig) replacement->type_sig = derived->type_sig;
    *rebased = 1;
    return replacement;
  }
  psx_type_t *copy = psx_type_clone(derived);
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
      rebuilt->pointee_fp_kind = old_outer->pointee_fp_kind;
      rebuilt->funcptr_sig =
          ps_decl_funcptr_sig_clone(old_outer->funcptr_sig);
      rebuilt->type_sig = old_outer->type_sig;
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
  const psx_type_t *array_leaf = rebuilt;
  while (array_leaf && array_leaf->kind == PSX_TYPE_ARRAY)
    array_leaf = array_leaf->base;
  if (array_leaf && array_leaf->kind == PSX_TYPE_POINTER) {
    int base_deref_size =
        ps_type_pointer_view_structural_base_deref_size(array_leaf);
    owner->base_deref_size = base_deref_size > 0
                                 ? base_deref_size
                                 : array_leaf->base_deref_size;
  } else {
    owner->base_deref_size = leaf_size;
  }
  psx_type_sync_pointer_to_array_metadata_from_base(owner);
  return type;
}

psx_type_t *psx_type_wrap_pointer_base_array(psx_type_t *type,
                                              int array_len) {
  psx_type_t *root = type;
  while (type && type->kind == PSX_TYPE_ARRAY) type = type->base;
  if (!type || type->kind != PSX_TYPE_POINTER || !type->base ||
      array_len <= 0) {
    return root;
  }
  int child_size = ps_type_sizeof(type->base);
  if (child_size <= 0) child_size = ps_type_deref_size(type->base);
  if (child_size <= 0) return type;
  int total_size = array_len * child_size;
  psx_type_t *array = psx_type_new_array(
      type->base, array_len, total_size, child_size, 0);
  array->base_deref_size = type->base_deref_size;
  array->outer_stride = child_size;
  type->base = array;
  type->deref_size = total_size;
  psx_type_sync_pointer_to_array_metadata_from_base(type);
  return root;
}

psx_type_t *psx_type_apply_pointer_derivation(psx_type_t *type,
                                               int pointer_levels,
                                               int top_deref_size,
                                               int base_deref_size,
                                               int ptr_array_pointee_bytes,
                                               unsigned int const_mask,
                                               unsigned int volatile_mask) {
  if (!type) return NULL;

  psx_type_t **owner = &type;
  while (*owner && (*owner)->kind == PSX_TYPE_ARRAY)
    owner = &(*owner)->base;

  int existing_levels = 0;
  for (psx_type_t *view = *owner;
       view && (view->kind == PSX_TYPE_POINTER ||
                view->kind == PSX_TYPE_ARRAY);
       view = view->base) {
    if (view->kind == PSX_TYPE_POINTER) existing_levels++;
  }
  int wanted_levels = pointer_levels > 0 ? pointer_levels : 1;
  if (existing_levels == 0) {
    if (pointer_levels <= 0 && ptr_array_pointee_bytes <= 0) {
      type->base_deref_size = base_deref_size;
      return type;
    }
    int deref_size = top_deref_size > 0 ? top_deref_size
                                         : ps_type_sizeof(*owner);
    if (deref_size <= 0) deref_size = 8;
    *owner = psx_type_wrap_pointer_levels(
        *owner, wanted_levels, deref_size, base_deref_size,
        const_mask, volatile_mask);
  } else if (wanted_levels > existing_levels) {
    int missing = wanted_levels - existing_levels;
    int deref_size = top_deref_size > 0 ? top_deref_size : 8;
    *owner = psx_type_wrap_pointer_levels(
        *owner, missing, deref_size, base_deref_size,
        const_mask, volatile_mask);
  }

  psx_type_t *pointer = type;
  while (pointer && pointer->kind == PSX_TYPE_ARRAY)
    pointer = pointer->base;
  while (pointer && pointer->kind == PSX_TYPE_POINTER &&
         pointer->base && pointer->base->kind == PSX_TYPE_POINTER) {
    pointer = pointer->base;
  }
  if (!pointer) return type;
  if (ptr_array_pointee_bytes > 0 && pointer->base &&
      (pointer->base->kind != PSX_TYPE_ARRAY ||
       ps_type_sizeof(pointer->base) != ptr_array_pointee_bytes)) {
    psx_type_t *array_leaf = pointer->base;
    while (array_leaf && array_leaf->kind == PSX_TYPE_ARRAY)
      array_leaf = array_leaf->base;
    int child_size = ps_type_sizeof(array_leaf);
    if (array_leaf && base_deref_size > 0 &&
        (array_leaf->kind == PSX_TYPE_INTEGER ||
         array_leaf->kind == PSX_TYPE_BOOL ||
         array_leaf->kind == PSX_TYPE_FLOAT)) {
      child_size = base_deref_size;
    }
    if (child_size <= 0) child_size = base_deref_size;
    if (child_size > 0 &&
        ptr_array_pointee_bytes >= child_size &&
        ptr_array_pointee_bytes % child_size == 0) {
      pointer->base = psx_type_new_array(
          array_leaf, ptr_array_pointee_bytes / child_size,
          ptr_array_pointee_bytes, child_size, 0);
      pointer->base->base_deref_size = base_deref_size;
    }
  }
  int effective_deref = ptr_array_pointee_bytes > 0
                            ? ptr_array_pointee_bytes
                            : top_deref_size;
  if (effective_deref > 0) pointer->deref_size = effective_deref;
  if (base_deref_size > 0) pointer->base_deref_size = base_deref_size;
  pointer->ptr_array_pointee_bytes = ptr_array_pointee_bytes;
  if (ptr_array_pointee_bytes > 0)
    pointer->outer_stride = ptr_array_pointee_bytes;
  int level = 0;
  for (psx_type_t *view = type;
       view && (view->kind == PSX_TYPE_POINTER ||
                view->kind == PSX_TYPE_ARRAY);
       view = view->base) {
    if (view->kind != PSX_TYPE_POINTER) continue;
    if (base_deref_size > 0) view->base_deref_size = base_deref_size;
    if (base_deref_size > 0 && view->base &&
        view->base->kind != PSX_TYPE_POINTER &&
        view->base->kind != PSX_TYPE_ARRAY) {
      view->deref_size = base_deref_size;
    }
    view->pointer_const_qual_mask =
        level < 32 ? const_mask >> level : 0;
    view->pointer_volatile_qual_mask =
        level < 32 ? volatile_mask >> level : 0;
    level++;
  }

  psx_type_t *leaf = type;
  while (leaf && (leaf->kind == PSX_TYPE_POINTER ||
                  leaf->kind == PSX_TYPE_ARRAY)) {
    leaf = leaf->base;
  }
  if (leaf && base_deref_size > 0 &&
      (leaf->kind == PSX_TYPE_INTEGER || leaf->kind == PSX_TYPE_BOOL ||
       leaf->kind == PSX_TYPE_FLOAT)) {
    leaf->size = base_deref_size;
    leaf->align = base_deref_size > 8 ? 8 : base_deref_size;
  }
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
    psx_type_t *row = psx_type_new(PSX_TYPE_ARRAY);
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

  psx_type_t *row = psx_type_new_array((psx_type_t *)element, array_len,
                                       row_size, elem_size, 1);
  row->base_deref_size = source->base_deref_size > 0
                             ? source->base_deref_size
                             : elem_size;
  row->outer_stride = elem_size;
  row->pointee_fp_kind = source->pointee_fp_kind;
  row->funcptr_sig = ps_decl_funcptr_sig_clone(source->funcptr_sig);
  row->vla_row_stride_frame_off = row_stride_frame_off;
  row->vla_strides_remaining = strides_remaining;
  return row;
}

psx_type_t *psx_type_new_vla_object_view(
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
    element = psx_type_new_array(
        element, outer_stride / leaf_size,
        outer_stride, leaf_size, 0);
    element->base_deref_size = leaf_size;
  }
  int element_size = ps_type_sizeof(element);
  if (element_size <= 0) element_size = leaf_size;
  psx_type_t *vla = psx_type_new_array(
      element, 0, 0, element_size, 1);
  vla->base_deref_size = leaf_size;
  vla->outer_stride = outer_stride;
  vla->pointee_fp_kind = source->pointee_fp_kind;
  vla->funcptr_sig = ps_decl_funcptr_sig_clone(source->funcptr_sig);
  vla->vla_row_stride_frame_off = row_stride_frame_off;
  vla->vla_strides_remaining = strides_remaining;
  psx_type_copy_common_qualifiers(vla, source);
  return vla;
}

void psx_type_set_vla_runtime_descriptor(
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

void psx_type_copy_vla_runtime_metadata(psx_type_t *dst,
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

void psx_type_set_vla_param_inner_dims(
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

psx_type_kind_t psx_type_kind_from_tag_kind(token_kind_t tag_kind) {
  switch (tag_kind) {
    case TK_STRUCT: return PSX_TYPE_STRUCT;
    case TK_UNION: return PSX_TYPE_UNION;
    default: return PSX_TYPE_INVALID;
  }
}

psx_type_t *psx_type_new_tag(token_kind_t tag_kind, char *tag_name, int tag_len,
                             int tag_scope_depth_p1, int size) {
  psx_type_t *type = psx_type_new(psx_type_kind_from_tag_kind(tag_kind));
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

int psx_type_is_pointer(const psx_type_t *type) {
  if (!type) return 0;
  return type->kind == PSX_TYPE_POINTER || type->kind == PSX_TYPE_ARRAY;
}

int ps_type_is_unsigned(const psx_type_t *type) {
  if (!type) return 0;
  return type->is_unsigned ? 1 : 0;
}

int psx_type_is_scalar(const psx_type_t *type) {
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

int psx_type_shape_matches(const psx_type_t *a, const psx_type_t *b) {
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
  int a_has_sig = ps_decl_funcptr_sig_has_payload(a->funcptr_sig);
  int b_has_sig = ps_decl_funcptr_sig_has_payload(b->funcptr_sig);
  int has_canonical_function =
      psx_type_find_function(a) || psx_type_find_function(b);
  if (!has_canonical_function &&
      (a_has_sig != b_has_sig ||
       (a_has_sig &&
        !psx_funcptr_type_shape_matches(a->funcptr_sig.function,
                                        b->funcptr_sig.function)))) {
    return 0;
  }
  switch (a->kind) {
    case PSX_TYPE_BOOL:
    case PSX_TYPE_INTEGER:
      return a->scalar_kind == b->scalar_kind && a->size == b->size;
    case PSX_TYPE_FLOAT:
    case PSX_TYPE_COMPLEX:
      return a->fp_kind == b->fp_kind && a->size == b->size;
    case PSX_TYPE_POINTER:
      return psx_type_shape_matches(a->base, b->base);
    case PSX_TYPE_ARRAY:
      return a->array_len == b->array_len && a->is_vla == b->is_vla &&
             psx_type_shape_matches(a->base, b->base);
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
          !psx_type_shape_matches(a->base, b->base)) {
        return 0;
      }
      for (int i = 0; i < a->param_count && i < 16; i++) {
        if (!psx_type_shape_matches(a->param_types[i], b->param_types[i]))
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

static psx_decl_funcptr_sig_t type_generic_function_signature(
    const psx_type_t *type, const psx_type_t *function) {
  psx_decl_funcptr_sig_t sig = ps_type_funcptr_signature(type);
  if (function && ps_decl_funcptr_sig_has_payload(function->funcptr_sig)) {
    sig.function = psx_funcptr_type_shape_merge_missing(
        sig.function, function->funcptr_sig.function, 1);
  }
  return sig;
}

int psx_type_generic_matches(const psx_type_t *control,
                             const psx_type_t *association) {
  if (!control || !association) return 0;
  if (control->type_sig && association->type_sig) {
    return strcmp(control->type_sig, association->type_sig) == 0;
  }
  const psx_type_t *control_function = psx_type_find_function(control);
  const psx_type_t *association_function =
      psx_type_find_function(association);
  if (!control_function && !association_function)
    return psx_type_shape_matches(control, association);
  if (!control_function || !association_function) return 0;
  if (!type_derivation_to_function_matches(control, association)) return 0;

  psx_decl_funcptr_sig_t control_sig =
      type_generic_function_signature(control, control_function);
  psx_decl_funcptr_sig_t association_sig =
      type_generic_function_signature(association, association_function);
  if (ps_decl_funcptr_sig_has_payload(control_sig) &&
      ps_decl_funcptr_sig_has_payload(association_sig)) {
    return psx_funcptr_type_shape_matches(control_sig.function,
                                          association_sig.function);
  }
  return psx_type_shape_matches(control_function, association_function);
}

int ps_type_is_tag_aggregate(const psx_type_t *type) {
  if (!type) return 0;
  return type->kind == PSX_TYPE_STRUCT || type->kind == PSX_TYPE_UNION;
}

static int psx_type_is_pointer_view_type(const psx_type_t *type) {
  return type && (type->kind == PSX_TYPE_POINTER || type->kind == PSX_TYPE_ARRAY);
}

int psx_type_pointer_depth(const psx_type_t *type) {
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
      if (type->outer_stride > 0 &&
          (type->ptr_array_pointee_bytes <= 0 ||
           (type->ptr_array_pointee_bytes == type->outer_stride &&
            type->ptr_array_pointee_bytes == type->deref_size))) {
        return 0;
      }
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

int psx_type_pointer_view_raw_array_shape_allowed(const psx_type_t *type) {
  if (!type) return 0;
  if (type->kind != PSX_TYPE_ARRAY && type->kind != PSX_TYPE_POINTER)
    return 0;
  if (type->kind == PSX_TYPE_POINTER &&
      psx_type_pointer_view_vla_row_stride_frame_off(type) == 0 &&
      (!type->base ||
       (type->base->kind != PSX_TYPE_ARRAY &&
        (!ps_type_is_tag_aggregate(type->base) || type->outer_stride <= 0 ||
         type->outer_stride != ps_type_deref_size(type))))) {
    return 0;
  }
  if (ps_type_pointer_view_stride_metadata(type, NULL, NULL, NULL, NULL))
    return 0;
  int vla_row_stride_frame_off =
      psx_type_pointer_view_vla_row_stride_frame_off(type);
  return type->kind == PSX_TYPE_ARRAY || vla_row_stride_frame_off != 0 ||
         (type->kind == PSX_TYPE_POINTER && type->base &&
          ps_type_is_tag_aggregate(type->base) && type->outer_stride > 0 &&
          type->outer_stride == ps_type_deref_size(type));
}

int psx_type_pointer_view_raw_stride_copy_allowed(const psx_type_t *type) {
  if (!psx_type_pointer_view_raw_array_shape_allowed(type)) return 0;
  return psx_type_pointer_view_vla_row_stride_frame_off(type) != 0 ||
         (type->kind == PSX_TYPE_ARRAY && type->is_vla);
}

int psx_type_pointer_view_raw_array_shape_has_hint(const psx_type_t *type) {
  if (!psx_type_pointer_view_raw_array_shape_allowed(type)) return 0;
  return type->outer_stride > 0 || type->mid_stride > 0 ||
         type->extra_strides_count > 0;
}

int psx_type_pointer_view_raw_array_size_hint(const psx_type_t *type) {
  if (!psx_type_pointer_view_raw_array_shape_allowed(type)) return 0;
  return type->outer_stride > 0 ? type->outer_stride : 0;
}

int psx_type_copy_pointer_view_stride_metadata(psx_type_t *dst,
                                               const psx_type_t *src) {
  if (!dst || !src) return 0;
  int inner_stride = 0;
  int next_stride = 0;
  int extra_strides[5] = {0};
  int extra_count = 0;
  if (ps_type_pointer_view_stride_metadata(src, &inner_stride, &next_stride,
                                            extra_strides, &extra_count)) {
    dst->outer_stride = inner_stride;
    dst->mid_stride = next_stride;
    dst->extra_strides_count = (unsigned char)extra_count;
    for (int i = 0; i < 5; i++) dst->extra_strides[i] = extra_strides[i];
    return 1;
  }
  if (!psx_type_pointer_view_raw_stride_copy_allowed(src)) return 0;
  dst->outer_stride = src->outer_stride;
  dst->mid_stride = psx_type_pointer_view_mid_stride(src);
  dst->extra_strides_count = src->extra_strides_count;
  for (int i = 0; i < 5; i++) dst->extra_strides[i] = src->extra_strides[i];
  return 1;
}

psx_type_t *psx_type_wrap_ret_pointee_array_base(
    psx_type_t *base, psx_ret_pointee_array_t ret_array) {
  if (!base || !psx_ret_pointee_array_has_dims(ret_array)) return base;
  int elem_size = ret_array.elem_size > 0 ? ret_array.elem_size
                                          : ps_type_sizeof(base);
  if (elem_size <= 0) return base;
  if (ret_array.second_dim > 0) {
    int inner_size = ret_array.second_dim * elem_size;
    psx_type_t *inner =
        psx_type_new_array(base, ret_array.second_dim, inner_size,
                           elem_size, 0);
    inner->outer_stride = elem_size;
    int outer_size = ret_array.first_dim * inner_size;
    psx_type_t *outer =
        psx_type_new_array(inner, ret_array.first_dim, outer_size,
                           inner_size, 0);
    outer->outer_stride = inner_size;
    outer->mid_stride = elem_size;
    return outer;
  }
  int array_size = ret_array.first_dim * elem_size;
  psx_type_t *array =
      psx_type_new_array(base, ret_array.first_dim, array_size,
                         elem_size, 0);
  array->outer_stride = elem_size;
  return array;
}

void psx_type_sync_pointer_to_array_metadata_from_base(psx_type_t *type) {
  if (!type || type->kind != PSX_TYPE_POINTER) return;
  int structural_base_deref =
      ps_type_pointer_view_structural_base_deref_size(type);
  if (structural_base_deref > 0)
    type->base_deref_size = structural_base_deref;
  int ptr_array_pointee_bytes =
      ps_type_pointer_view_structural_ptr_array_pointee_bytes(type);
  type->ptr_array_pointee_bytes =
      ptr_array_pointee_bytes > 0 ? ptr_array_pointee_bytes : 0;
  int row_sizes[7] = {0};
  int row_count = 0;
  const psx_type_t *array = type->base;
  while (array && array->kind == PSX_TYPE_ARRAY && row_count < 7) {
    int row_size = ps_type_sizeof(array);
    if (row_size <= 0) break;
    row_sizes[row_count++] = row_size;
    array = array->base;
  }
  type->outer_stride = row_count > 0 ? row_sizes[0] : 0;
  type->mid_stride = row_count > 1 ? row_sizes[1] : 0;
  int extra_count = row_count > 2 ? row_count - 2 : 0;
  if (extra_count > 5) extra_count = 5;
  type->extra_strides_count = (unsigned char)extra_count;
  for (int i = 0; i < extra_count; i++)
    type->extra_strides[i] = row_sizes[i + 2];
  for (int i = extra_count; i < 5; i++) type->extra_strides[i] = 0;
}

int psx_type_canonicalize_flat_pointer_to_array(psx_type_t *type) {
  if (!type || type->kind != PSX_TYPE_POINTER || !type->base ||
      type->base->kind == PSX_TYPE_ARRAY ||
      type->base->kind == PSX_TYPE_POINTER) {
    return 0;
  }
  int row_size = type->ptr_array_pointee_bytes > 0
                     ? type->ptr_array_pointee_bytes
                     : ((type->outer_stride > 0 && type->mid_stride > 0)
                            ? type->outer_stride
                            : 0);
  int elem_size = ps_type_sizeof(type->base);
  if (elem_size <= 0) elem_size = ps_type_deref_size(type->base);
  if (elem_size <= 0 && type->base_deref_size > 0)
    elem_size = type->base_deref_size;
  if (row_size <= elem_size || elem_size <= 0) return 0;
  if (type->outer_stride > 0 && type->outer_stride != row_size) return 0;

  int strides[8] = {0};
  int count = 0;
  strides[count++] = row_size;
  if (type->mid_stride > 0 && type->mid_stride < row_size)
    strides[count++] = type->mid_stride;
  for (int i = 0; i < type->extra_strides_count && i < 5 && count < 7; i++) {
    int stride = type->extra_strides[i];
    if (stride > 0 && stride < strides[count - 1])
      strides[count++] = stride;
  }
  if (elem_size < strides[count - 1]) strides[count++] = elem_size;
  if (count < 2) return 0;

  psx_type_t *base = type->base;
  int base_deref_size = type->base_deref_size > 0
                            ? type->base_deref_size
                            : ps_type_deref_size(base);
  for (int i = count - 2; i >= 0; i--) {
    int total_size = strides[i];
    int child_size = strides[i + 1];
    if (total_size <= 0 || child_size <= 0) return 0;
    int array_len = total_size / child_size;
    if (array_len <= 0) array_len = 1;
    psx_type_t *array =
        psx_type_new_array(base, array_len, total_size, child_size, 0);
    array->base_deref_size = base_deref_size;
    array->outer_stride = child_size;
    array->mid_stride = (i + 2 < count) ? strides[i + 2] : 0;
    int extra_count = 0;
    for (int j = i + 3; j < count && extra_count < 5; j++)
      array->extra_strides[extra_count++] = strides[j];
    array->extra_strides_count = (unsigned char)extra_count;
    base = array;
  }

  type->base = base;
  type->deref_size = row_size;
  type->base_deref_size = base_deref_size;
  if (type->pointer_qual_levels <= 0) type->pointer_qual_levels = 1;
  if (type->outer_stride <= 0) type->outer_stride = row_size;
  return 1;
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

int psx_type_array_view_stride_metadata(const psx_type_t *type,
                                        int keep_outer_row_stride,
                                        int *inner_stride,
                                        int *next_stride,
                                        int *extra_strides,
                                        int *extra_strides_count) {
  type_clear_stride_outputs(inner_stride, next_stride, extra_strides,
                            extra_strides_count);
  if (!type || type->kind != PSX_TYPE_ARRAY) return 0;
  int deref_size = ps_type_deref_size(type);
  int child_stride = type->base && type->base->kind == PSX_TYPE_ARRAY
                         ? ps_type_deref_size(type->base)
                         : 0;
  if (child_stride <= 0 && type->base && type->base->kind == PSX_TYPE_ARRAY)
    child_stride = ps_type_sizeof(type->base->base);

  int inner = 0;
  int next = 0;
  int extras[5] = {0};
  int extra_count = 0;
  int raw_inner = type->outer_stride;
  int raw_next = type->mid_stride;
  int raw_matches_outer = deref_size > 0 && raw_inner == deref_size;
  int raw_matches_child = child_stride > 0 && raw_inner == child_stride;
  if (raw_inner > 0 && (raw_matches_outer || raw_matches_child)) {
    inner = raw_inner;
    if (raw_matches_child) {
      int grandchild_stride = type->base && type->base->kind == PSX_TYPE_ARRAY &&
                                      type->base->base &&
                                      type->base->base->kind == PSX_TYPE_ARRAY
                                  ? ps_type_deref_size(type->base->base)
                                  : 0;
      if (grandchild_stride <= 0 && type->base && type->base->kind == PSX_TYPE_ARRAY &&
          type->base->base && type->base->base->kind == PSX_TYPE_ARRAY)
        grandchild_stride = ps_type_sizeof(type->base->base->base);
      if (raw_next > 0 && grandchild_stride > 0 && raw_next == grandchild_stride)
        next = raw_next;
    }
  } else if (keep_outer_row_stride && deref_size > 0) {
    inner = deref_size;
  } else if (child_stride > 0) {
    inner = child_stride;
    if (type->base->base && type->base->base->kind == PSX_TYPE_ARRAY) {
      next = ps_type_deref_size(type->base->base);
      if (next <= 0) next = ps_type_sizeof(type->base->base->base);
      const psx_type_t *cur = type->base->base->base;
      while (cur && cur->kind == PSX_TYPE_ARRAY && extra_count < 5) {
        int stride = ps_type_deref_size(cur);
        if (stride <= 0) stride = ps_type_sizeof(cur->base);
        if (stride <= 0) break;
        extras[extra_count++] = stride;
        cur = cur->base;
      }
    }
  }
  if (inner <= 0 && next <= 0 && extra_count <= 0) return 0;
  if (inner_stride) *inner_stride = inner;
  if (next_stride) *next_stride = next;
  if (extra_strides_count) *extra_strides_count = extra_count;
  if (extra_strides) {
    for (int i = 0; i < extra_count; i++) extra_strides[i] = extras[i];
    for (int i = extra_count; i < 5; i++) extra_strides[i] = 0;
  }
  return 1;
}

int psx_type_decl_array_stride_metadata(const psx_type_t *type,
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

int psx_type_pointer_view_mid_stride(const psx_type_t *type) {
  if (!psx_type_is_pointer_view_type(type)) return 0;
  int structural_next_stride = 0;
  if (ps_type_pointer_view_stride_metadata(type, NULL, &structural_next_stride,
                                            NULL, NULL)) {
    return structural_next_stride;
  }
  if (type->kind == PSX_TYPE_POINTER) {
    if (!type->base) return 0;
    int ptr_array_pointee_bytes =
        ps_type_pointer_view_structural_ptr_array_pointee_bytes(type);
    if (ptr_array_pointee_bytes <= 0 && type->base->kind != PSX_TYPE_ARRAY)
      return 0;
    if (type->mid_stride > 0) return type->mid_stride;
    if (type->base->kind == PSX_TYPE_ARRAY) {
      if (type->base->mid_stride > 0) return type->base->mid_stride;
      if (type->base->base && type->base->base->kind == PSX_TYPE_ARRAY)
        return ps_type_deref_size(type->base);
    }
    return 0;
  }
  if (type->kind == PSX_TYPE_ARRAY) {
    if (type->mid_stride > 0) return type->mid_stride;
    if (type->base && type->base->kind == PSX_TYPE_ARRAY)
      return ps_type_deref_size(type);
  }
  return 0;
}

int psx_type_pointer_view_vla_row_stride_frame_off(const psx_type_t *type) {
  if (!psx_type_is_pointer_view_type(type)) return 0;
  return type->vla_row_stride_frame_off;
}

int psx_type_pointer_view_vla_strides_remaining(const psx_type_t *type) {
  if (!psx_type_is_pointer_view_type(type)) return 0;
  return type->vla_strides_remaining > 0 ? type->vla_strides_remaining : 0;
}

int psx_type_vla_row_stride_src_offset(const psx_type_t *type) {
  return type && type->is_vla ? type->vla_row_stride_src_offset : 0;
}

int psx_type_vla_row_stride_elem_size(const psx_type_t *type) {
  return type && type->is_vla && type->vla_row_stride_elem_size > 0
             ? type->vla_row_stride_elem_size
             : 0;
}

int psx_type_vla_param_inner_dim_count(const psx_type_t *type) {
  return type && type->is_vla ? type->vla_param_inner_dim_count : 0;
}

int psx_type_vla_param_inner_dim_const(const psx_type_t *type, int index) {
  if (!type || !type->is_vla || index < 0 || index >= 7) return 0;
  return type->vla_param_inner_dim_consts[index];
}

int psx_type_vla_param_inner_dim_src_offset(const psx_type_t *type, int index) {
  if (!type || !type->is_vla || index < 0 || index >= 7) return 0;
  return type->vla_param_inner_dim_src_offsets[index];
}

static int type_structural_pointer_qual_levels(const psx_type_t *type) {
  if (!type || type->kind != PSX_TYPE_POINTER || !type->base) return 0;
  int levels = psx_type_pointer_depth(type);
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
  int depth = psx_type_pointer_depth(type);
  if (depth > 0 && depth < 32) mask &= (1u << depth) - 1u;
  return mask;
}

unsigned int psx_type_pointer_view_structural_qual_mask(
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

void psx_type_set_decl_spec_qualifiers(psx_type_t *type,
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
  dst->base_deref_size = src->base_deref_size;
  dst->pointer_qual_levels = src->pointer_qual_levels;
  dst->pointer_const_qual_mask = src->pointer_const_qual_mask;
  dst->pointer_volatile_qual_mask = src->pointer_volatile_qual_mask;
  dst->pointee_fp_kind = src->pointee_fp_kind;
  dst->funcptr_sig = ps_decl_funcptr_sig_clone(src->funcptr_sig);
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
  dst->ptr_array_pointee_bytes = src->ptr_array_pointee_bytes;
  dst->outer_stride = src->outer_stride;
  dst->mid_stride = src->mid_stride;
  dst->extra_strides_count = src->extra_strides_count;
  for (int i = 0; i < 5; i++) dst->extra_strides[i] = src->extra_strides[i];
}
