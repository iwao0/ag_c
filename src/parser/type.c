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
  int deep_base_size = base_deref_size > 0 ? base_deref_size : psx_type_sizeof(base);
  for (int level = 1; level <= levels; level++) {
    int deref_size = 8;
    if (levels == 1) {
      deref_size = top_deref_size;
    } else if (level == 1) {
      deref_size = deep_base_size;
    } else if (level == levels && top_deref_size > 0) {
      deref_size = top_deref_size;
    }
    if (deref_size <= 0) deref_size = (level == 1) ? psx_type_sizeof(type) : 8;
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

psx_type_t *psx_type_new_runtime_vla_row_view(
    const psx_type_t *source, int row_size, int elem_size,
    int row_stride_frame_off, int strides_remaining) {
  if (!source) return NULL;
  const psx_type_t *element = source;
  if (source->kind == PSX_TYPE_POINTER && source->base)
    element = source->base;
  if (element->kind == PSX_TYPE_ARRAY) return (psx_type_t *)element;
  if (elem_size <= 0) elem_size = psx_type_sizeof(element);
  if (elem_size <= 0) elem_size = psx_type_deref_size(element);
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
  row->funcptr_sig = psx_decl_funcptr_sig_clone(source->funcptr_sig);
  row->vla_row_stride_frame_off = row_stride_frame_off;
  row->vla_strides_remaining = strides_remaining;
  return row;
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
         psx_type_is_tag_aggregate(type->base);
}

static int type_carries_ptr_array_pointee_after_deref(const psx_type_t *type) {
  return type_is_array_of_pointer_to_array(type) ||
         type_is_array_of_tag_aggregate(type);
}

int psx_type_carries_ptr_array_pointee_after_deref(const psx_type_t *type) {
  return type_carries_ptr_array_pointee_after_deref(type);
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
  return psx_type_sizeof(cur);
}

int psx_type_pointer_view_base_deref_size(
    const psx_type_t *type, int allow_sizeof_base_fallback) {
  if (!psx_type_is_pointer_view_type(type)) return 0;
  int structurally_known = 0;
  int base_deref_size =
      type_structural_base_deref_size(type, &structurally_known);
  if (structurally_known) return base_deref_size;
  base_deref_size = type->base_deref_size;
  if (base_deref_size <= 0 && type->kind == PSX_TYPE_ARRAY)
    base_deref_size = psx_type_deref_size(type);
  if (base_deref_size <= 0 && allow_sizeof_base_fallback && type->base)
    base_deref_size = psx_type_sizeof(type->base);
  return base_deref_size;
}

int psx_type_pointer_view_structural_base_deref_size(const psx_type_t *type) {
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
    int size = psx_type_sizeof(type->base);
    if (size <= 0) size = psx_type_deref_size(type);
    return size;
  }
  if (type->kind == PSX_TYPE_ARRAY) {
    if (!type->base) return 0;
    if (structurally_known) *structurally_known = 1;
    if (!type_carries_ptr_array_pointee_after_deref(type)) return 0;
    return psx_type_sizeof(type);
  }
  return 0;
}

int psx_type_pointer_view_ptr_array_pointee_bytes(const psx_type_t *type) {
  int structurally_known = 0;
  int bytes =
      type_structural_ptr_array_pointee_bytes(type, &structurally_known);
  if (structurally_known) return bytes > 0 ? bytes : 0;
  return type && type->ptr_array_pointee_bytes > 0
             ? type->ptr_array_pointee_bytes
             : 0;
}

int psx_type_pointer_view_structural_ptr_array_pointee_bytes(
    const psx_type_t *type) {
  int structurally_known = 0;
  int bytes =
      type_structural_ptr_array_pointee_bytes(type, &structurally_known);
  return structurally_known && bytes > 0 ? bytes : 0;
}

int psx_type_legacy_flat_pointer_ptr_array_pointee_bytes(
    const psx_type_t *type, int sidecar_ptr_array_pointee_bytes) {
  if (!type || type->kind != PSX_TYPE_POINTER || type->base ||
      sidecar_ptr_array_pointee_bytes <= 0 ||
      type->ptr_array_pointee_bytes <= 0 ||
      type->ptr_array_pointee_bytes != type->outer_stride ||
      sidecar_ptr_array_pointee_bytes != type->ptr_array_pointee_bytes) {
    return 0;
  }
  return type->ptr_array_pointee_bytes;
}

int psx_type_legacy_flat_pointer_stride_matches(
    const psx_type_t *type, int sidecar_outer_stride,
    int sidecar_mid_stride) {
  return type && type->kind == PSX_TYPE_POINTER && !type->base &&
         type->ptr_array_pointee_bytes <= 0 &&
         type->outer_stride > 0 && type->mid_stride > 0 &&
         sidecar_outer_stride == type->outer_stride &&
         sidecar_mid_stride == type->mid_stride;
}

static int psx_type_legacy_flat_pointer_shape_matches_sidecar(
    const psx_type_t *type, int sidecar_ptr_array_pointee_bytes,
    int sidecar_outer_stride, int sidecar_mid_stride) {
  if (!type || type->kind != PSX_TYPE_POINTER || type->base) return 0;
  if (type->ptr_array_pointee_bytes <= 0 &&
      type->outer_stride <= 0 && type->mid_stride <= 0) {
    return 1;
  }
  return psx_type_legacy_flat_pointer_ptr_array_pointee_bytes(
             type, sidecar_ptr_array_pointee_bytes) > 0 ||
         psx_type_legacy_flat_pointer_stride_matches(
             type, sidecar_outer_stride, sidecar_mid_stride);
}

int psx_type_pointer_view_ptr_array_pointee_bytes_with_sidecar(
    const psx_type_t *type, int sidecar_ptr_array_pointee_bytes) {
  int structurally_known = 0;
  int bytes = type_structural_ptr_array_pointee_bytes(type, &structurally_known);
  if (structurally_known) return bytes > 0 ? bytes : 0;
  return psx_type_legacy_flat_pointer_ptr_array_pointee_bytes(
      type, sidecar_ptr_array_pointee_bytes);
}

int psx_type_pointer_view_array_deref_size_with_sidecar(
    const psx_type_t *type, int sidecar_ptr_array_pointee_bytes,
    int sidecar_outer_stride, int sidecar_mid_stride) {
  if (!psx_type_is_pointer_view_type(type)) return 0;
  int ptr_array_pointee_bytes =
      psx_type_pointer_view_ptr_array_pointee_bytes_with_sidecar(
          type, sidecar_ptr_array_pointee_bytes);
  if (type->kind == PSX_TYPE_POINTER && ptr_array_pointee_bytes > 0) {
    int base_size = type->base ? psx_type_sizeof(type->base) : 0;
    if (base_size > 0 && type->base_deref_size > 0 &&
        base_size > type->base_deref_size) {
      return base_size;
    }
    return ptr_array_pointee_bytes;
  }
  if (psx_type_legacy_flat_pointer_stride_matches(
          type, sidecar_outer_stride, sidecar_mid_stride)) {
    return type->outer_stride;
  }
  if (type->kind == PSX_TYPE_POINTER && type->base &&
      type->base->kind == PSX_TYPE_ARRAY) {
    int base_size = psx_type_sizeof(type->base);
    if (base_size > 0) return base_size;
    if (type->outer_stride > 0) return type->outer_stride;
  }
  if (type->kind == PSX_TYPE_ARRAY && type->outer_stride > 0)
    return type->outer_stride;
  return 0;
}

int psx_type_pointer_view_stride_sync_allowed_with_sidecar(
    const psx_type_t *type, int sidecar_ptr_array_pointee_bytes,
    int sidecar_outer_stride, int sidecar_mid_stride) {
  if (!type) return 0;
  if (type->kind == PSX_TYPE_ARRAY) return 1;
  if (type->kind != PSX_TYPE_POINTER) return 0;
  if (psx_type_pointer_view_vla_row_stride_frame_off(type) != 0) return 1;
  if (type->base && type->base->kind == PSX_TYPE_ARRAY) return 1;
  if (type->base && psx_type_is_tag_aggregate(type->base) &&
      type->outer_stride > 0 &&
      type->outer_stride == psx_type_deref_size(type)) {
    return 1;
  }
  if (psx_type_pointer_view_structural_ptr_array_pointee_bytes(type) > 0)
    return 1;
  return psx_type_legacy_flat_pointer_ptr_array_pointee_bytes(
             type, sidecar_ptr_array_pointee_bytes) > 0 ||
         psx_type_legacy_flat_pointer_stride_matches(
             type, sidecar_outer_stride, sidecar_mid_stride);
}

int psx_type_pointer_view_raw_array_shape_allowed(const psx_type_t *type) {
  if (!psx_type_pointer_view_stride_sync_allowed_with_sidecar(type, 0, 0, 0))
    return 0;
  if (psx_type_pointer_view_stride_metadata(type, NULL, NULL, NULL, NULL))
    return 0;
  int vla_row_stride_frame_off =
      psx_type_pointer_view_vla_row_stride_frame_off(type);
  return type->kind == PSX_TYPE_ARRAY || vla_row_stride_frame_off != 0 ||
         (type->kind == PSX_TYPE_POINTER && type->base &&
          psx_type_is_tag_aggregate(type->base) && type->outer_stride > 0 &&
          type->outer_stride == psx_type_deref_size(type));
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
  if (psx_type_pointer_view_stride_metadata(src, &inner_stride, &next_stride,
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

static int type_legacy_flat_pointer_base_deref_size(
    const psx_type_t *type, int sidecar_base_deref_size,
    int sidecar_ptr_array_pointee_bytes, int sidecar_outer_stride,
    int sidecar_mid_stride) {
  if (!type || type->kind != PSX_TYPE_POINTER || type->base ||
      type->base_deref_size <= 0 ||
      sidecar_base_deref_size != type->base_deref_size) {
    return 0;
  }
  if (!psx_type_legacy_flat_pointer_shape_matches_sidecar(
          type, sidecar_ptr_array_pointee_bytes, sidecar_outer_stride,
          sidecar_mid_stride)) {
    return 0;
  }
  return type->base_deref_size;
}

int psx_type_pointer_view_base_deref_size_with_sidecar(
    const psx_type_t *type, int sidecar_base_deref_size,
    int sidecar_ptr_array_pointee_bytes, int sidecar_outer_stride,
    int sidecar_mid_stride) {
  int base_deref_size =
      psx_type_pointer_view_structural_base_deref_size(type);
  if (base_deref_size > 0) return base_deref_size;
  return type_legacy_flat_pointer_base_deref_size(
      type, sidecar_base_deref_size, sidecar_ptr_array_pointee_bytes,
      sidecar_outer_stride, sidecar_mid_stride);
}

psx_type_t *psx_type_wrap_ret_pointee_array_base(
    psx_type_t *base, psx_ret_pointee_array_t ret_array) {
  if (!base || !psx_ret_pointee_array_has_dims(ret_array)) return base;
  int elem_size = ret_array.elem_size > 0 ? ret_array.elem_size
                                          : psx_type_sizeof(base);
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
  int ptr_array_pointee_bytes =
      psx_type_pointer_view_structural_ptr_array_pointee_bytes(type);
  type->ptr_array_pointee_bytes =
      ptr_array_pointee_bytes > 0 ? ptr_array_pointee_bytes : 0;
  int inner_stride = 0;
  int next_stride = 0;
  int extra_strides[5] = {0};
  int extra_count = 0;
  if (psx_type_pointer_view_stride_metadata(type, &inner_stride, &next_stride,
                                            extra_strides, &extra_count)) {
    type->outer_stride = inner_stride;
    type->mid_stride = next_stride;
    type->extra_strides_count = (unsigned char)extra_count;
    for (int i = 0; i < extra_count && i < 5; i++)
      type->extra_strides[i] = extra_strides[i];
    for (int i = extra_count; i < 5; i++) type->extra_strides[i] = 0;
  } else {
    type->outer_stride = 0;
    type->mid_stride = 0;
    type->extra_strides_count = 0;
    for (int i = 0; i < 5; i++) type->extra_strides[i] = 0;
  }
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
  int elem_size = psx_type_sizeof(type->base);
  if (elem_size <= 0) elem_size = psx_type_deref_size(type->base);
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
                            : psx_type_deref_size(base);
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
    int stride = psx_type_deref_size(cur);
    if (stride <= 0 && cur->base) stride = psx_type_sizeof(cur->base);
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

static int type_array_outer_element_size(const psx_type_t *type) {
  if (!type || type->kind != PSX_TYPE_ARRAY) return 0;
  int total_size = psx_type_sizeof(type);
  if (total_size > 0 && type->array_len > 0 &&
      (total_size % type->array_len) == 0) {
    return total_size / type->array_len;
  }
  if (type->elem_size > 0) return type->elem_size;
  return psx_type_deref_size(type);
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
  int deref_size = psx_type_deref_size(type);
  int child_stride = type->base && type->base->kind == PSX_TYPE_ARRAY
                         ? psx_type_deref_size(type->base)
                         : 0;
  if (child_stride <= 0 && type->base && type->base->kind == PSX_TYPE_ARRAY)
    child_stride = psx_type_sizeof(type->base->base);

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
                                  ? psx_type_deref_size(type->base->base)
                                  : 0;
      if (grandchild_stride <= 0 && type->base && type->base->kind == PSX_TYPE_ARRAY &&
          type->base->base && type->base->base->kind == PSX_TYPE_ARRAY)
        grandchild_stride = psx_type_sizeof(type->base->base->base);
      if (raw_next > 0 && grandchild_stride > 0 && raw_next == grandchild_stride)
        next = raw_next;
    }
  } else if (keep_outer_row_stride && deref_size > 0) {
    inner = deref_size;
  } else if (child_stride > 0) {
    inner = child_stride;
    if (type->base->base && type->base->base->kind == PSX_TYPE_ARRAY) {
      next = psx_type_deref_size(type->base->base);
      if (next <= 0) next = psx_type_sizeof(type->base->base->base);
      const psx_type_t *cur = type->base->base->base;
      while (cur && cur->kind == PSX_TYPE_ARRAY && extra_count < 5) {
        int stride = psx_type_deref_size(cur);
        if (stride <= 0) stride = psx_type_sizeof(cur->base);
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

int psx_type_pointer_view_stride_metadata(const psx_type_t *type,
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
    if (!type->base || type->base->kind != PSX_TYPE_ARRAY) return 0;
    array = type;
  }
  if (!array) return 0;
  return type_array_stride_metadata(array, inner_stride, next_stride,
                                    extra_strides, extra_strides_count);
}

static int type_pointer_view_vla_runtime_stride_metadata(
    const psx_type_t *type, int count, int *inner_stride, int *next_stride) {
  if (type->kind == PSX_TYPE_ARRAY && type->is_vla &&
      type->vla_row_stride_frame_off == 0 && type->deref_size > 0 &&
      type->outer_stride > type->deref_size && type->mid_stride <= 0 &&
      count <= 0) {
    if (inner_stride) *inner_stride = type->deref_size;
    return 1;
  }
  if (type->kind == PSX_TYPE_POINTER && type->vla_row_stride_frame_off != 0 &&
      type->outer_stride <= 0 && type->mid_stride <= 0 && count <= 0) {
    int inner = type->base_deref_size > 0 ? type->base_deref_size
                                          : psx_type_deref_size(type->base);
    if (inner <= 0) inner = type->deref_size;
    if (inner <= 0) return 0;
    if (inner_stride) *inner_stride = inner;
    if (next_stride) *next_stride = type->vla_strides_remaining > 0 ? inner : 0;
    return 1;
  }
  return 0;
}

static int type_pointer_view_legacy_raw_stride_metadata(
    const psx_type_t *type, int count, int *inner_stride, int *next_stride,
    int *extra_strides, int *extra_strides_count) {
  if (type->kind == PSX_TYPE_POINTER && type->base &&
      type->base->kind != PSX_TYPE_ARRAY &&
      type->vla_row_stride_frame_off == 0) {
    return 0;
  }
  if (type->kind == PSX_TYPE_POINTER && type->ptr_array_pointee_bytes <= 0 &&
      type->vla_row_stride_frame_off == 0 && type->deref_size > 0 &&
      type->outer_stride > type->deref_size && type->mid_stride <= 0 &&
      count <= 0) {
    if (inner_stride) *inner_stride = type->deref_size;
    return 1;
  }
  if (type->kind == PSX_TYPE_POINTER && type->base &&
      type->base->kind == PSX_TYPE_ARRAY && type->ptr_array_pointee_bytes <= 0) {
    int base_size = psx_type_sizeof(type->base);
    if (base_size > 0 && type->outer_stride >= base_size) {
      int inner = type->mid_stride > 0 ? type->mid_stride
                                       : psx_type_deref_size(type->base);
      if (inner <= 0) inner = psx_type_deref_size(type);
      if (inner <= 0) return 0;
      int next = 0;
      int shifted_count = 0;
      if (count > 0) {
        next = type->extra_strides[0];
        shifted_count = count - 1;
      }
      if (next <= 0 && type->mid_stride > 0)
        next = psx_type_deref_size(type->base);
      if (inner_stride) *inner_stride = inner;
      if (next_stride) *next_stride = next;
      if (extra_strides_count) *extra_strides_count = shifted_count;
      if (extra_strides) {
        for (int i = 0; i < shifted_count && i < 5; i++)
          extra_strides[i] = type->extra_strides[i + 1];
        for (int i = shifted_count; i < 5; i++) extra_strides[i] = 0;
      }
      return 1;
    }
  }
  if (type->kind == PSX_TYPE_POINTER && type->ptr_array_pointee_bytes <= 0 &&
      type->deref_size > 0 && type->outer_stride > 0 &&
      type->deref_size > type->outer_stride) {
    if (inner_stride) *inner_stride = type->outer_stride;
    if (next_stride) *next_stride = type->mid_stride;
    if (extra_strides_count) *extra_strides_count = count;
    if (extra_strides) {
      for (int i = 0; i < count; i++) extra_strides[i] = type->extra_strides[i];
      for (int i = count; i < 5; i++) extra_strides[i] = 0;
    }
    return 1;
  }
  if (type->kind == PSX_TYPE_POINTER && type->ptr_array_pointee_bytes <= 0 &&
      type->outer_stride > 0 && type->mid_stride > 0) {
    int inner = type->mid_stride;
    int next = 0;
    int shifted_count = 0;
    if (count > 0) {
      next = type->extra_strides[0];
      shifted_count = count - 1;
    }
    if (next <= 0) {
      if (type->deref_size > 0 && type->deref_size < inner) {
        next = type->deref_size;
      } else {
        next = type->base_deref_size > 0 ? type->base_deref_size
                                         : psx_type_sizeof(type->base);
      }
    }
    if (inner_stride) *inner_stride = inner;
    if (next_stride) *next_stride = next;
    if (extra_strides_count) *extra_strides_count = shifted_count;
    if (extra_strides) {
      for (int i = 0; i < shifted_count && i < 5; i++)
        extra_strides[i] = type->extra_strides[i + 1];
      for (int i = shifted_count; i < 5; i++) extra_strides[i] = 0;
    }
    return 1;
  }
  if (type->ptr_array_pointee_bytes > 0 && type->base_deref_size > 0 &&
      type->vla_row_stride_frame_off == 0 && type->mid_stride > 0) {
    int inner = type->mid_stride;
    if (type->kind == PSX_TYPE_POINTER && type->base &&
        type->base->kind == PSX_TYPE_ARRAY) {
      int array_elem_stride = type_array_outer_element_size(type->base);
      if (array_elem_stride > 0) inner = array_elem_stride;
    }
    int next = 0;
    int shifted_count = 0;
    if (count > 0) {
      next = type->extra_strides[0];
      shifted_count = count - 1;
    }
    if (next <= 0) {
      if (type->kind == PSX_TYPE_POINTER && type->base &&
          type->base->kind == PSX_TYPE_ARRAY &&
          type->base->base && type->base->base->kind == PSX_TYPE_ARRAY) {
        int nested_elem_stride = type_array_outer_element_size(type->base->base);
        if (nested_elem_stride > 0) next = nested_elem_stride;
      }
      if (next <= 0 && type->deref_size > 0 && type->deref_size < inner) {
        next = type->deref_size;
      } else if (type->base && type->base->kind == PSX_TYPE_POINTER) {
        int pointer_elem_size = psx_type_sizeof(type->base);
        next = pointer_elem_size > 0 ? pointer_elem_size : 8;
      } else if (next <= 0) {
        next = type->base_deref_size;
      }
    }
    if (inner_stride) *inner_stride = inner;
    if (next_stride) *next_stride = next;
    if (extra_strides_count) *extra_strides_count = shifted_count;
    if (extra_strides) {
      for (int i = 0; i < shifted_count && i < 5; i++)
        extra_strides[i] = type->extra_strides[i + 1];
      for (int i = shifted_count; i < 5; i++) extra_strides[i] = 0;
    }
    return 1;
  }
  if (type->ptr_array_pointee_bytes > 0 && type->base_deref_size > 0 &&
      type->outer_stride >= type->ptr_array_pointee_bytes &&
      type->mid_stride <= 0 && count <= 0) {
    int elem_stride = type->base_deref_size;
    if (type->base && type->base->kind == PSX_TYPE_POINTER) {
      int pointer_elem_size = psx_type_sizeof(type->base);
      if (pointer_elem_size > 0) elem_stride = pointer_elem_size;
    } else if (type->base && type->base->kind == PSX_TYPE_ARRAY) {
      int array_elem_stride = type_array_outer_element_size(type->base);
      if (array_elem_stride > 0) elem_stride = array_elem_stride;
    }
    if (inner_stride) *inner_stride = elem_stride;
    return 1;
  }
  if (type->kind == PSX_TYPE_ARRAY && type->base &&
      type->base->kind == PSX_TYPE_ARRAY &&
      type->outer_stride > 0 && type->mid_stride <= 0 && count <= 0) {
    int next = psx_type_deref_size(type->base);
    if (inner_stride) *inner_stride = type->outer_stride;
    if (next_stride) *next_stride = next > 0 ? next : 0;
    return 1;
  }
  if (type->kind == PSX_TYPE_ARRAY && type->base &&
      type->base->kind != PSX_TYPE_ARRAY && !type->is_vla) {
    int elem_stride = psx_type_deref_size(type);
    if (elem_stride <= 0 && type->base) elem_stride = psx_type_sizeof(type->base);
    if (elem_stride <= 0 || type->outer_stride != elem_stride ||
        type->mid_stride > 0 || count > 0) {
      return 0;
    }
  }
  if (type->outer_stride <= 0 && type->mid_stride <= 0 && count <= 0) return 0;
  if (inner_stride) *inner_stride = type->outer_stride;
  if (next_stride) *next_stride = type->mid_stride;
  if (extra_strides_count) *extra_strides_count = count;
  if (extra_strides) {
    for (int i = 0; i < count; i++) extra_strides[i] = type->extra_strides[i];
    for (int i = count; i < 5; i++) extra_strides[i] = 0;
  }
  return 1;
}

int psx_type_pointer_view_legacy_stride_metadata(const psx_type_t *type,
                                                 int *inner_stride,
                                                 int *next_stride,
                                                 int *extra_strides,
                                                 int *extra_strides_count) {
  type_clear_stride_outputs(inner_stride, next_stride, extra_strides,
                            extra_strides_count);
  if (!psx_type_is_pointer_view_type(type)) return 0;
  int count = type->extra_strides_count;
  if (count < 0) count = 0;
  if (count > 5) count = 5;
  if (type_pointer_view_vla_runtime_stride_metadata(type, count, inner_stride,
                                                    next_stride)) {
    return 1;
  }
  return type_pointer_view_legacy_raw_stride_metadata(
      type, count, inner_stride, next_stride, extra_strides,
      extra_strides_count);
}

int psx_type_pointer_view_effective_stride_metadata(const psx_type_t *type,
                                                    int *inner_stride,
                                                    int *next_stride,
                                                    int *extra_strides,
                                                    int *extra_strides_count) {
  if (psx_type_pointer_view_stride_metadata(type, inner_stride, next_stride,
                                            extra_strides,
                                            extra_strides_count)) {
    return 1;
  }
  return psx_type_pointer_view_legacy_stride_metadata(
      type, inner_stride, next_stride, extra_strides, extra_strides_count);
}

int psx_type_pointer_view_mid_stride(const psx_type_t *type) {
  if (!psx_type_is_pointer_view_type(type)) return 0;
  int structural_next_stride = 0;
  if (psx_type_pointer_view_stride_metadata(type, NULL, &structural_next_stride,
                                            NULL, NULL)) {
    return structural_next_stride;
  }
  if (type->kind == PSX_TYPE_POINTER) {
    if (!type->base) return 0;
    int ptr_array_pointee_bytes =
        psx_type_pointer_view_structural_ptr_array_pointee_bytes(type);
    if (ptr_array_pointee_bytes <= 0 && type->base->kind != PSX_TYPE_ARRAY)
      return 0;
    if (type->mid_stride > 0) return type->mid_stride;
    if (type->base->kind == PSX_TYPE_ARRAY) {
      if (type->base->mid_stride > 0) return type->base->mid_stride;
      if (type->base->base && type->base->base->kind == PSX_TYPE_ARRAY)
        return psx_type_deref_size(type->base);
    }
    return 0;
  }
  if (type->kind == PSX_TYPE_ARRAY) {
    if (type->mid_stride > 0) return type->mid_stride;
    if (type->base && type->base->kind == PSX_TYPE_ARRAY)
      return psx_type_deref_size(type);
  }
  return 0;
}

int psx_type_pointer_view_outer_stride_with_sidecar(
    const psx_type_t *type, int sidecar_ptr_array_pointee_bytes,
    int sidecar_outer_stride, int sidecar_mid_stride) {
  if (!psx_type_is_pointer_view_type(type)) return 0;
  if (type->kind == PSX_TYPE_POINTER) {
    int bytes = psx_type_pointer_view_ptr_array_pointee_bytes_with_sidecar(
        type, sidecar_ptr_array_pointee_bytes);
    if (bytes > 0) return bytes;
  }
  int inner_stride = 0;
  if (psx_type_pointer_view_stride_metadata(type, &inner_stride, NULL,
                                            NULL, NULL) &&
      inner_stride > 0) {
    return inner_stride;
  }
  if (type->kind == PSX_TYPE_POINTER && type->base) return 0;
  if (type->kind == PSX_TYPE_POINTER && !type->base) {
    return psx_type_legacy_flat_pointer_stride_matches(
               type, sidecar_outer_stride, sidecar_mid_stride)
               ? type->outer_stride
               : 0;
  }
  if (type->outer_stride > 0) return type->outer_stride;
  if (type->kind == PSX_TYPE_ARRAY) return psx_type_deref_size(type);
  return 0;
}

int psx_type_pointer_view_mid_stride_with_sidecar(
    const psx_type_t *type, int sidecar_outer_stride,
    int sidecar_mid_stride) {
  const psx_type_t *cur = type;
  while (cur) {
    if (cur->kind == PSX_TYPE_POINTER && !cur->base) {
      return psx_type_legacy_flat_pointer_stride_matches(
                 cur, sidecar_outer_stride, sidecar_mid_stride)
                 ? cur->mid_stride
                 : 0;
    }
    int mid_stride = psx_type_pointer_view_mid_stride(cur);
    if (mid_stride > 0) return mid_stride;
    cur = cur->base;
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

static int type_structural_pointer_qual_levels(const psx_type_t *type) {
  if (!type || type->kind != PSX_TYPE_POINTER || !type->base) return 0;
  int levels = psx_type_pointer_depth(type);
  return levels > 0 ? levels : 1;
}

int psx_type_pointer_view_qual_levels(const psx_type_t *type) {
  if (!psx_type_is_pointer_view_type(type)) return 0;
  int structural_levels = type_structural_pointer_qual_levels(type);
  if (structural_levels > 0) return structural_levels;
  int levels = type->pointer_qual_levels;
  if (levels <= 0 && type->kind == PSX_TYPE_POINTER)
    levels = 1;
  return levels;
}

int psx_type_pointer_view_structural_qual_levels(const psx_type_t *type) {
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

unsigned int psx_type_pointer_view_qual_mask(const psx_type_t *type,
                                             int is_volatile) {
  if (!psx_type_is_pointer_view_type(type)) return 0;
  int structural_levels = type_structural_pointer_qual_levels(type);
  if (structural_levels > 0)
    return type_structural_pointer_qual_mask(type, is_volatile);
  unsigned int mask = is_volatile ? type->pointer_volatile_qual_mask
                                  : type->pointer_const_qual_mask;
  return mask;
}

unsigned int psx_type_pointer_view_structural_qual_mask(
    const psx_type_t *type, int is_volatile) {
  if (!psx_type_is_pointer_view_type(type)) return 0;
  int structural_levels = type_structural_pointer_qual_levels(type);
  if (structural_levels <= 0) return 0;
  return type_structural_pointer_qual_mask(type, is_volatile);
}

int psx_type_pointer_view_quals_with_sidecar(
    const psx_type_t *type, int sidecar_pointer_levels,
    unsigned int sidecar_const_mask, unsigned int sidecar_volatile_mask,
    int sidecar_ptr_array_pointee_bytes, int sidecar_outer_stride,
    int sidecar_mid_stride, int *levels, unsigned int *const_mask,
    unsigned int *volatile_mask) {
  int structural_levels = psx_type_pointer_view_structural_qual_levels(type);
  if (structural_levels > 0) {
    if (levels) *levels = structural_levels;
    if (const_mask)
      *const_mask = psx_type_pointer_view_structural_qual_mask(type, 0);
    if (volatile_mask)
      *volatile_mask = psx_type_pointer_view_structural_qual_mask(type, 1);
    return 1;
  }
  if (!psx_type_legacy_flat_pointer_shape_matches_sidecar(
          type, sidecar_ptr_array_pointee_bytes, sidecar_outer_stride,
          sidecar_mid_stride)) {
    return 0;
  }
  if (sidecar_pointer_levels <= 0 || !type ||
      type->pointer_qual_levels <= 0 ||
      sidecar_pointer_levels != type->pointer_qual_levels ||
      sidecar_const_mask != type->pointer_const_qual_mask ||
      sidecar_volatile_mask != type->pointer_volatile_qual_mask) {
    return 0;
  }
  if (levels) *levels = type->pointer_qual_levels;
  if (const_mask) *const_mask = type->pointer_const_qual_mask;
  if (volatile_mask) *volatile_mask = type->pointer_volatile_qual_mask;
  return 1;
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
  dst->funcptr_sig = psx_decl_funcptr_sig_clone(src->funcptr_sig);
  dst->vla_row_stride_frame_off = src->vla_row_stride_frame_off;
  dst->vla_strides_remaining = src->vla_strides_remaining;
  dst->ptr_array_pointee_bytes = src->ptr_array_pointee_bytes;
  dst->outer_stride = src->outer_stride;
  dst->mid_stride = src->mid_stride;
  dst->extra_strides_count = src->extra_strides_count;
  for (int i = 0; i < 5; i++) dst->extra_strides[i] = src->extra_strides[i];
}
