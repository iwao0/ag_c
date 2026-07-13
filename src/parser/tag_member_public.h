#ifndef PARSER_TAG_MEMBER_PUBLIC_H
#define PARSER_TAG_MEMBER_PUBLIC_H

#include "core.h"
#include "type.h"
#include <stdbool.h>

/* struct/union メンバの全属性を 1 回のクエリで取得する統合 API
 * (docs/code_refactoring_2026 Phase A1)。
 *
 * 既存の 5 つに分散した getter (`_at` / `_bf` / `_fp_kind` / `_is_bool` / `_count`)
 * の wrapper として実装され、呼び出し側で `(tag_kind, tag_name, tag_len)` の
 * 3-tuple を毎回繰り返し渡す冗長性を解消する。
 *
 * 取得失敗 (member 不存在) なら false。bitfield/fp_kind/is_bool は 0 で
 * 初期化されるので、struct メンバが bitfield でないとき bit_width=0 等を返す。 */
typedef struct tag_member_info_t {
  char *name;
  int len;
  int offset;
  int type_size;
  int deref_size;
  int array_len;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int is_tag_pointer;
  int pointer_qual_levels;
  int bit_width;
  int bit_offset;
  int bit_is_signed;
  tk_float_kind_t fp_kind;
  int is_bool;
  int is_unsigned;
  psx_type_t *decl_type;
} tag_member_info_t;

static inline const psx_type_t *ps_tag_member_decl_type(
    const tag_member_info_t *m) {
  return m ? m->decl_type : NULL;
}

static inline psx_type_t *ps_tag_member_decl_type_mut(tag_member_info_t *m) {
  return m ? m->decl_type : NULL;
}

static inline void ps_tag_member_set_decl_type(tag_member_info_t *m,
                                                psx_type_t *decl_type) {
  if (m) m->decl_type = decl_type;
}

static inline psx_decl_funcptr_sig_t ps_tag_member_funcptr_sig(
    const tag_member_info_t *m) {
  if (!m) return (psx_decl_funcptr_sig_t){0};
  const psx_type_t *decl_type = ps_tag_member_decl_type(m);
  return decl_type ? ps_type_funcptr_signature(decl_type)
                   : (psx_decl_funcptr_sig_t){0};
}

static inline const psx_type_t *ps_tag_member_decl_value_type(
    const tag_member_info_t *m) {
  const psx_type_t *type = ps_tag_member_decl_type(m);
  while (type && type->kind == PSX_TYPE_ARRAY) type = type->base;
  return type;
}

static inline int ps_tag_member_decl_value_size(const tag_member_info_t *m) {
  const psx_type_t *type = ps_tag_member_decl_value_type(m);
  if (type) {
    int size = ps_type_sizeof(type);
    if (size > 0) return size;
  }
  return 0;
}

static inline int ps_tag_member_decl_storage_size(const tag_member_info_t *m) {
  if (!m) return 0;
  const psx_type_t *decl_type = ps_tag_member_decl_type(m);
  if (decl_type) {
    int size = ps_type_sizeof(decl_type);
    if (size > 0) return size;
  }
  return 0;
}

static inline int ps_tag_member_decl_array_count(const tag_member_info_t *m) {
  if (!m) return 0;
  const psx_type_t *decl_type = ps_tag_member_decl_type(m);
  if (decl_type && decl_type->kind == PSX_TYPE_ARRAY) {
    int elem_size = ps_tag_member_decl_value_size(m);
    int total_size = ps_tag_member_decl_storage_size(m);
    if (elem_size > 0 && total_size > 0 && total_size % elem_size == 0)
      return total_size / elem_size;
    if (decl_type->array_len > 0) return decl_type->array_len;
  }
  return 0;
}

static inline int ps_tag_member_decl_array_dim_count(const tag_member_info_t *m) {
  if (!m) return 0;
  const psx_type_t *decl_type = ps_tag_member_decl_type(m);
  if (decl_type) {
    const psx_type_t *type = decl_type;
    int count = 0;
    while (type && type->kind == PSX_TYPE_ARRAY && count < 8) {
      if (type->array_len <= 0) break;
      count++;
      type = type->base;
    }
    if (count > 0) return count;
    return 0;
  }
  return 0;
}

static inline int ps_tag_member_decl_array_dim(const tag_member_info_t *m,
                                                int index) {
  if (!m || index < 0 || index >= 8) return 0;
  const psx_type_t *decl_type = ps_tag_member_decl_type(m);
  if (decl_type) {
    const psx_type_t *type = decl_type;
    int cur = 0;
    while (type && type->kind == PSX_TYPE_ARRAY && cur <= index) {
      if (type->array_len <= 0) break;
      if (cur == index) return type->array_len;
      cur++;
      type = type->base;
    }
    return 0;
  }
  return 0;
}

static inline int ps_tag_member_decl_array_dims(const tag_member_info_t *m,
                                                 int *dims, int max_dims) {
  if (!dims || max_dims <= 0) return ps_tag_member_decl_array_dim_count(m);
  int n = ps_tag_member_decl_array_dim_count(m);
  if (n > max_dims) n = max_dims;
  for (int i = 0; i < n; i++)
    dims[i] = ps_tag_member_decl_array_dim(m, i);
  for (int i = n; i < max_dims; i++)
    dims[i] = 0;
  return n;
}

static inline int ps_tag_member_decl_deref_size(const tag_member_info_t *m) {
  if (!m) return 0;
  const psx_type_t *decl_type = ps_tag_member_decl_type(m);
  if (decl_type) {
    if (decl_type->kind == PSX_TYPE_POINTER) {
      int size = ps_type_pointer_view_structural_base_deref_size(decl_type);
      if (size > 0) return size;
    }
    int size = ps_type_deref_size(decl_type);
    if (size > 0) return size;
  }
  return 0;
}

static inline tk_float_kind_t ps_tag_member_decl_fp_kind(
    const tag_member_info_t *m) {
  const psx_type_t *type = ps_tag_member_decl_value_type(m);
  if (type) {
    return (type->kind == PSX_TYPE_FLOAT || type->kind == PSX_TYPE_COMPLEX)
               ? type->fp_kind
               : TK_FLOAT_KIND_NONE;
  }
  return TK_FLOAT_KIND_NONE;
}

static inline int ps_tag_member_decl_is_bool(const tag_member_info_t *m) {
  const psx_type_t *type = ps_tag_member_decl_value_type(m);
  if (type) return type->kind == PSX_TYPE_BOOL;
  return 0;
}

static inline int ps_tag_member_decl_is_unsigned(const tag_member_info_t *m) {
  const psx_type_t *type = ps_tag_member_decl_value_type(m);
  if (type) return ps_type_is_unsigned(type);
  return 0;
}

static inline int ps_tag_member_decl_is_pointer(const tag_member_info_t *m) {
  const psx_type_t *type = ps_tag_member_decl_value_type(m);
  if (type) return type->kind == PSX_TYPE_POINTER;
  return 0;
}

static inline int ps_tag_member_decl_pointer_qual_levels(
    const tag_member_info_t *m) {
  const psx_type_t *type = ps_tag_member_decl_value_type(m);
  if (type && type->kind == PSX_TYPE_POINTER) {
    return ps_type_pointer_view_structural_qual_levels(type);
  }
  return 0;
}

static inline int ps_tag_member_decl_ptr_array_bytes_from_type(
    const tag_member_info_t *m, const psx_type_t *type) {
  if (!type) return 0;
  (void)m;
  return ps_type_pointer_view_structural_ptr_array_pointee_bytes(type);
}

static inline int ps_tag_member_decl_outer_stride(const tag_member_info_t *m) {
  if (!m) return 0;
  const psx_type_t *decl_type = ps_tag_member_decl_type(m);
  if (decl_type) {
    int bytes = ps_type_pointer_view_structural_ptr_array_pointee_bytes(
        decl_type);
    if (bytes > 0) return bytes;
    int stride = 0;
    if (ps_type_pointer_view_stride_metadata(
            decl_type, &stride, NULL, NULL, NULL))
      return stride;
  }
  return 0;
}

static inline int ps_tag_member_decl_mid_stride(const tag_member_info_t *m) {
  if (!m) return 0;
  const psx_type_t *decl_type = ps_tag_member_decl_type(m);
  if (decl_type) {
    int stride = 0;
    if (ps_type_pointer_view_stride_metadata(
            decl_type, NULL, &stride, NULL, NULL))
      return stride;
  }
  return 0;
}

static inline int ps_tag_member_decl_ptr_array_pointee_bytes(
    const tag_member_info_t *m) {
  if (!m) return 0;
  const psx_type_t *decl_type = ps_tag_member_decl_type(m);
  if (decl_type) {
    const psx_type_t *type = decl_type;
    while (type) {
      int bytes = ps_tag_member_decl_ptr_array_bytes_from_type(m, type);
      if (bytes > 0) return bytes;
      type = type->base;
    }
    return 0;
  }
  return 0;
}

static inline int ps_tag_member_decl_ptr_array_pointee_elem_size(
    const tag_member_info_t *m) {
  if (!m) return 0;
  const psx_type_t *type = ps_tag_member_decl_value_type(m);
  if (type && type->kind == PSX_TYPE_POINTER &&
      type->base && type->base->kind == PSX_TYPE_ARRAY) {
    int elem = 0;
    int total_size = ps_type_sizeof(type->base);
    if (total_size > 0 && type->base->array_len > 0 &&
        (total_size % type->base->array_len) == 0)
      elem = total_size / type->base->array_len;
    if (elem <= 0) elem = ps_type_deref_size(type->base);
    if (elem > 0) return elem;
  }
  int bytes = ps_tag_member_decl_ptr_array_pointee_bytes(m);
  if (type && type->kind == PSX_TYPE_POINTER && type->base && bytes > 0) {
    int elem = ps_type_sizeof(type->base);
    if (elem > 0 && (bytes % elem) == 0) return elem;
  }
  int ndim = ps_tag_member_decl_array_dim_count(m);
  if (bytes <= 0 || ndim <= 0) return 0;
  int count = 1;
  for (int i = 0; i < ndim && i < 8; i++) {
    int dim = ps_tag_member_decl_array_dim(m, i);
    if (dim <= 0) return 0;
    count *= dim;
  }
  if (count <= 0 || (bytes % count) != 0) return 0;
  return bytes / count;
}

static inline void ps_tag_member_decl_tag_identity(
    const tag_member_info_t *m, token_kind_t *out_kind, char **out_name,
    int *out_len, int *out_is_pointer) {
  token_kind_t kind = TK_EOF;
  char *name = NULL;
  int len = 0;
  int is_pointer = 0;
  const psx_type_t *decl_type = ps_tag_member_decl_type(m);
  if (decl_type) {
    const psx_type_t *type = decl_type;
    while (type && type->kind == PSX_TYPE_ARRAY) type = type->base;
    is_pointer = type && type->kind == PSX_TYPE_POINTER ? 1 : 0;
    if (type && type->kind == PSX_TYPE_POINTER && type->base) type = type->base;
    if (ps_type_is_tag_aggregate(type)) {
      kind = type->tag_kind;
      name = type->tag_name;
      len = type->tag_len;
    } else {
      kind = TK_EOF;
      name = NULL;
      len = 0;
    }
  }
  if (out_kind) *out_kind = kind;
  if (out_name) *out_name = name;
  if (out_len) *out_len = len;
  if (out_is_pointer) *out_is_pointer = is_pointer;
}

bool ps_ctx_get_tag_member_info(token_kind_t kind, char *name, int len, int index,
                                  tag_member_info_t *out);
bool ps_ctx_find_tag_member_info(token_kind_t kind, char *name, int len,
                                   char *member_name, int member_len,
                                   tag_member_info_t *out);
bool ps_ctx_get_tag_member_info_at_scope(token_kind_t kind, char *name, int len,
                                          int scope_depth, int index,
                                          tag_member_info_t *out);
bool ps_ctx_find_tag_member_info_at_scope(token_kind_t kind, char *name, int len,
                                           int scope_depth,
                                           char *member_name, int member_len,
                                           tag_member_info_t *out);
int ps_ctx_get_tag_member_count(token_kind_t kind, char *name, int len);
int ps_ctx_get_tag_member_count_at_scope(token_kind_t kind, char *name, int len, int scope_depth);
int ps_ctx_get_tag_scope_depth(token_kind_t kind, char *name, int len);

#endif
