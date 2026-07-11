#include "type_name.h"
#include "semantic_ctx.h"
#include <string.h>

void psx_type_name_init(psx_type_name_t *name) {
  if (!name) return;
  memset(name, 0, sizeof(*name));
  name->base_kind = TK_EOF;
  name->tag_kind = TK_EOF;
  name->fp_kind = TK_FLOAT_KIND_NONE;
}

void psx_type_normalize_integer_identity(psx_type_t *type) {
  if (!type) return;
  if (type->kind == PSX_TYPE_INTEGER) {
    if (type->tag_kind == TK_ENUM) type->scalar_kind = TK_ENUM;
    else if (type->size == 1) type->scalar_kind = TK_CHAR;
    else if (type->size == 2) type->scalar_kind = TK_SHORT;
    else if (type->size == 8) type->scalar_kind = TK_LONG;
    else type->scalar_kind = TK_INT;
  }
  psx_type_normalize_integer_identity(type->base);
  if (type->kind == PSX_TYPE_FUNCTION) {
    for (int i = 0; i < type->param_count && i < 16; i++)
      psx_type_normalize_integer_identity(type->param_types[i]);
  }
}

static int integer_size(token_kind_t kind, int fallback) {
  switch (kind) {
    case TK_BOOL:
    case TK_CHAR: return 1;
    case TK_SHORT: return 2;
    case TK_LONG: return 8;
    case TK_INT:
    case TK_SIGNED:
    case TK_UNSIGNED:
    case TK_ENUM: return 4;
    default: return fallback > 0 ? fallback : 4;
  }
}

static void apply_pointer_qualifiers(psx_type_t *type, int levels,
                                     unsigned int const_mask,
                                     unsigned int volatile_mask) {
  int lexical_level = levels;
  for (psx_type_t *cur = type;
       cur && cur->kind == PSX_TYPE_POINTER && lexical_level > 0;
       cur = cur->base, lexical_level--) {
    unsigned int bit = 1u << (lexical_level - 1);
    cur->is_const_qualified = (const_mask & bit) ? 1 : 0;
    cur->is_volatile_qualified = (volatile_mask & bit) ? 1 : 0;
  }
}

static psx_type_t *build_base(const psx_type_name_t *name) {
  if (name->canonical_base) return psx_type_clone(name->canonical_base);
  if (name->tag_kind == TK_STRUCT || name->tag_kind == TK_UNION) {
    psx_type_t *type = psx_type_new_tag(
        name->tag_kind, name->tag_name, name->tag_len,
        0, name->base_size);
    type->aggregate_definition = psx_ctx_get_tag_definition(
        name->tag_kind, name->tag_name, name->tag_len);
    return type;
  }
  if (name->base_kind == TK_VOID) {
    psx_type_t *type = psx_type_new(PSX_TYPE_VOID);
    type->scalar_kind = TK_VOID;
    return type;
  }
  if (name->is_complex) {
    psx_type_t *type = psx_type_new(PSX_TYPE_COMPLEX);
    type->fp_kind = name->fp_kind != TK_FLOAT_KIND_NONE
                        ? name->fp_kind
                        : TK_FLOAT_KIND_DOUBLE;
    int component_size = name->base_size > 0 ? name->base_size
                         : type->fp_kind == TK_FLOAT_KIND_FLOAT ? 4 : 8;
    type->size = component_size * 2;
    type->align = type->size >= 8 ? 8 : type->size;
    type->is_long_double = name->is_long_double ? 1 : 0;
    return type;
  }
  if (name->fp_kind != TK_FLOAT_KIND_NONE ||
      name->base_kind == TK_FLOAT || name->base_kind == TK_DOUBLE) {
    tk_float_kind_t fp = name->fp_kind != TK_FLOAT_KIND_NONE
                             ? name->fp_kind
                             : name->base_kind == TK_FLOAT
                                   ? TK_FLOAT_KIND_FLOAT
                                   : TK_FLOAT_KIND_DOUBLE;
    psx_type_t *type =
        psx_type_new_float(fp, fp == TK_FLOAT_KIND_FLOAT ? 4 : 8);
    type->is_long_double = name->is_long_double ? 1 : 0;
    return type;
  }
  token_kind_t scalar =
      name->base_kind == TK_SIGNED ? TK_INT : name->base_kind;
  psx_type_t *type = psx_type_new_integer(
      scalar, integer_size(name->base_kind, name->base_size),
      name->is_unsigned);
  type->is_long_long = name->is_long_long ? 1 : 0;
  type->is_plain_char = name->is_plain_char ? 1 : 0;
  return type;
}

psx_type_t *psx_type_name_build(const psx_type_name_t *name) {
  if (!name) return NULL;
  psx_type_t *base = build_base(name);
  if (!base) return NULL;
  psx_ctx_attach_aggregate_definitions(base);
  if (!name->canonical_base) {
    base->is_const_qualified = name->pointee_const ? 1 : 0;
    base->is_volatile_qualified = name->pointee_volatile ? 1 : 0;
  }

  int levels = name->pointer_levels;
  if (name->canonical_base && levels > 0) {
    int size = ps_type_sizeof(base);
    if (size <= 0) size = 8;
    psx_type_t *type = psx_type_wrap_pointer_levels(
        base, levels, size, size, name->pointer_const_mask,
        name->pointer_volatile_mask);
    apply_pointer_qualifiers(type, levels, name->pointer_const_mask,
                             name->pointer_volatile_mask);
    type->type_sig = name->type_sig;
    return type;
  }

  if (name->canonicalize_function &&
      ps_decl_funcptr_sig_has_payload(name->funcptr_sig) && levels > 0 &&
      name->array_dim_count == 0) {
    psx_type_t *type = psx_type_new_funcptr(name->funcptr_sig, levels);
    apply_pointer_qualifiers(type, levels, name->pointer_const_mask,
                             name->pointer_volatile_mask);
    type->type_sig = name->type_sig;
    return type;
  }

  psx_type_t *type = base;
  int deep_base_size = name->base_size > 0 ? name->base_size
                                            : ps_type_sizeof(base);
  if (deep_base_size <= 0) deep_base_size = 8;
  int pointer_element_array =
      name->array_dim_count > 0 && name->pointer_array_pointee_bytes > 0 &&
      name->pointer_array_element_is_pointer;
  if (pointer_element_array) {
    type = psx_type_new_pointer(base, deep_base_size);
    type->base_deref_size = deep_base_size;
  }
  for (int i = name->array_dim_count - 1; i >= 0; i--) {
    int dim = name->array_dims[i];
    if (dim <= 0) continue;
    int elem_size = ps_type_sizeof(type);
    if (elem_size <= 0) elem_size = deep_base_size;
    type = psx_type_new_array(type, dim, elem_size * dim, elem_size, 0);
    if (pointer_element_array) {
      type->base_deref_size = deep_base_size;
      type->outer_stride = elem_size;
      if (type->base && type->base->kind == PSX_TYPE_ARRAY) {
        type->mid_stride = type->base->outer_stride;
        type->extra_strides_count = type->base->extra_strides_count;
        for (int j = 0; j < 5; j++)
          type->extra_strides[j] = type->base->extra_strides[j];
      }
      type->ptr_array_pointee_bytes = ps_type_sizeof(type);
    }
  }
  if (name->is_unspecified_array && levels == 0) {
    int elem_size = ps_type_sizeof(type);
    type = psx_type_new_array(type, 0, 0, elem_size, 0);
  }

  if (levels > 0) {
    if (name->array_dim_count == 0 &&
        name->pointer_array_pointee_bytes <= 0) {
      int top_size = name->pointer_deref_size > 0
                         ? name->pointer_deref_size
                         : levels >= 2 ? 8 : ps_type_sizeof(type);
      int base_size = name->pointer_base_deref_size > 0
                          ? name->pointer_base_deref_size
                          : deep_base_size;
      type = psx_type_wrap_pointer_levels(
          type, levels, top_size, base_size, name->pointer_const_mask,
          name->pointer_volatile_mask);
    } else {
      for (int level = 1; level <= levels; level++) {
        int deref_size = level == 1 ? ps_type_sizeof(type) : 8;
        if (deref_size <= 0) deref_size = level == 1 ? deep_base_size : 8;
        psx_type_t *pointer = psx_type_new_pointer(type, deref_size);
        pointer->pointer_qual_levels = level;
        pointer->base_deref_size = deep_base_size;
        if (level == 1 && name->pointer_array_pointee_bytes > 0) {
          pointer->ptr_array_pointee_bytes =
              name->pointer_array_pointee_bytes;
          pointer->outer_stride = name->pointer_array_pointee_bytes;
          if (type && type->kind == PSX_TYPE_ARRAY) {
            pointer->mid_stride = type->outer_stride;
            pointer->extra_strides_count = type->extra_strides_count;
            for (int i = 0; i < 5; i++)
              pointer->extra_strides[i] = type->extra_strides[i];
          }
        }
        type = pointer;
      }
    }
    apply_pointer_qualifiers(type, levels, name->pointer_const_mask,
                             name->pointer_volatile_mask);
  }
  if (type && ps_decl_funcptr_sig_has_payload(name->funcptr_sig))
    type->funcptr_sig = ps_decl_funcptr_sig_clone(name->funcptr_sig);
  if (type) type->type_sig = name->type_sig;
  (void)name->array_count;
  return type;
}
