#include "type.h"
#include "arena.h"

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
    type->base_deref_size = psx_type_deref_size(base);
    if (type->base_deref_size <= 0) type->base_deref_size = psx_type_sizeof(base);
    type->pointee_fp_kind = base->fp_kind;
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
  if (base) type->pointee_fp_kind = base->fp_kind;
  return type;
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

int psx_type_sizeof(const psx_type_t *type) {
  if (!type) return 0;
  if (type->kind == PSX_TYPE_VOID || type->kind == PSX_TYPE_FUNCTION) return 0;
  if (type->kind == PSX_TYPE_POINTER) return 8;
  return type->size;
}

int psx_type_deref_size(const psx_type_t *type) {
  if (!type) return 0;
  if (type->deref_size > 0) return type->deref_size;
  if (type->kind == PSX_TYPE_POINTER || type->kind == PSX_TYPE_ARRAY) {
    int s = psx_type_sizeof(type->base);
    return s > 0 ? s : type->elem_size;
  }
  return 0;
}

int psx_type_is_pointer(const psx_type_t *type) {
  if (!type) return 0;
  return type->kind == PSX_TYPE_POINTER || type->kind == PSX_TYPE_ARRAY;
}

int psx_type_is_unsigned(const psx_type_t *type) {
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

int psx_type_is_tag_aggregate(const psx_type_t *type) {
  if (!type) return 0;
  return type->kind == PSX_TYPE_STRUCT || type->kind == PSX_TYPE_UNION;
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

void psx_type_copy_pointer_metadata(psx_type_t *dst, const psx_type_t *src) {
  if (!dst || !src) return;
  dst->deref_size = src->deref_size;
  dst->base_deref_size = src->base_deref_size;
  dst->pointer_qual_levels = src->pointer_qual_levels;
  dst->pointer_const_qual_mask = src->pointer_const_qual_mask;
  dst->pointer_volatile_qual_mask = src->pointer_volatile_qual_mask;
  dst->pointee_fp_kind = src->pointee_fp_kind;
  dst->funcptr_sig = src->funcptr_sig;
  dst->vla_row_stride_frame_off = src->vla_row_stride_frame_off;
  dst->vla_strides_remaining = src->vla_strides_remaining;
  dst->ptr_array_pointee_bytes = src->ptr_array_pointee_bytes;
  dst->outer_stride = src->outer_stride;
  dst->mid_stride = src->mid_stride;
  dst->extra_strides_count = src->extra_strides_count;
  for (int i = 0; i < 5; i++) dst->extra_strides[i] = src->extra_strides[i];
}
