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
  int outer_stride;
  /* 3 次元以上の配列メンバ (`char c[2][2][3]` / `int t[2][2][2]`) の中間段ストライド
   * (1 段目 subscript 後の要素サイズ = 残り次元の総バイト数 / arr_dims[1])。
   * 2D は 0 (outer_stride / elem_size の 2 段で済む)。3D 以上で inner_deref_size に
   * 載せて多段 subscript を正しくスケールする。 */
  int mid_stride;
  /* 多次元 char 配列メンバ (`char c[2][2][3]`) の各次元サイズ。arr_ndim 段だけ
   * 有効。グローバル brace init の再帰展開で 1 段ずつ消費する。0 = 非多次元 char
   * (従来通り outer_stride のみで運用)。 */
  int arr_dims[8];
  int arr_ndim;
  /* array-of-pointer-to-array メンバ (`int (*p[M])[N]`) の各要素ポインタが指す配列の
   * 全バイト数 (= N * elem)。0 = 通常のポインタ配列。`s.p[i]` の subscript 結果に
   * pointer-to-array 情報を carry し、`(*s.p[i])[j]` が正しいストライドで添字できるよう
   * build_subscript_deref / build_unary_deref_node に伝える。 */
  int ptr_array_pointee_bytes;
  int is_funcptr;
  psx_decl_funcptr_sig_t funcptr_sig;
  psx_type_t *decl_type;
} tag_member_info_t;

static inline psx_decl_funcptr_sig_t psx_ctx_tag_member_funcptr_sig(
    const tag_member_info_t *m) {
  if (!m) return (psx_decl_funcptr_sig_t){0};
  if (m->decl_type && psx_decl_funcptr_sig_has_payload(m->decl_type->funcptr_sig))
    return psx_decl_funcptr_sig_clone(m->decl_type->funcptr_sig);
  return m->is_funcptr ? psx_decl_funcptr_sig_clone(m->funcptr_sig)
                       : (psx_decl_funcptr_sig_t){0};
}

static inline void psx_ctx_tag_member_set_funcptr_sig(
    tag_member_info_t *m, psx_decl_funcptr_sig_t sig) {
  if (!m) return;
  m->funcptr_sig = psx_decl_funcptr_sig_clone(sig);
  m->is_funcptr = psx_decl_funcptr_sig_has_payload(sig) ? 1 : 0;
}

static inline const psx_type_t *psx_tag_member_decl_value_type(
    const tag_member_info_t *m) {
  if (!m || !m->decl_type) return NULL;
  const psx_type_t *type = m->decl_type;
  while (type && type->kind == PSX_TYPE_ARRAY) type = type->base;
  return type;
}

static inline int psx_tag_member_decl_value_size(const tag_member_info_t *m) {
  const psx_type_t *type = psx_tag_member_decl_value_type(m);
  if (type) {
    int size = psx_type_sizeof(type);
    if (size > 0) return size;
  }
  return m ? m->type_size : 0;
}

static inline int psx_tag_member_decl_storage_size(const tag_member_info_t *m) {
  if (!m) return 0;
  if (m->decl_type) {
    int size = psx_type_sizeof(m->decl_type);
    if (size > 0) return size;
  }
  int count = m->array_len > 0 ? m->array_len : 1;
  return m->type_size * count;
}

static inline int psx_tag_member_decl_array_count(const tag_member_info_t *m) {
  if (!m) return 0;
  if (m->decl_type && m->decl_type->kind == PSX_TYPE_ARRAY) {
    int elem_size = psx_tag_member_decl_value_size(m);
    int total_size = psx_tag_member_decl_storage_size(m);
    if (elem_size > 0 && total_size > 0 && total_size % elem_size == 0)
      return total_size / elem_size;
    if (m->decl_type->array_len > 0) return m->decl_type->array_len;
  }
  return m->array_len;
}

static inline tk_float_kind_t psx_tag_member_decl_fp_kind(
    const tag_member_info_t *m) {
  const psx_type_t *type = psx_tag_member_decl_value_type(m);
  if (type) {
    return (type->kind == PSX_TYPE_FLOAT || type->kind == PSX_TYPE_COMPLEX)
               ? type->fp_kind
               : TK_FLOAT_KIND_NONE;
  }
  return m ? m->fp_kind : TK_FLOAT_KIND_NONE;
}

static inline int psx_tag_member_decl_is_bool(const tag_member_info_t *m) {
  const psx_type_t *type = psx_tag_member_decl_value_type(m);
  if (type) return type->kind == PSX_TYPE_BOOL;
  return m && m->is_bool;
}

static inline int psx_tag_member_decl_is_pointer(const tag_member_info_t *m) {
  const psx_type_t *type = psx_tag_member_decl_value_type(m);
  if (type) return type->kind == PSX_TYPE_POINTER;
  return m && m->is_tag_pointer;
}

static inline int psx_tag_member_decl_outer_stride(const tag_member_info_t *m) {
  if (!m) return 0;
  if (m->decl_type && m->decl_type->outer_stride > 0)
    return m->decl_type->outer_stride;
  if (m->decl_type && m->decl_type->kind == PSX_TYPE_ARRAY)
    return psx_type_deref_size(m->decl_type);
  return m->outer_stride;
}

static inline void psx_tag_member_decl_tag_identity(
    const tag_member_info_t *m, token_kind_t *out_kind, char **out_name,
    int *out_len, int *out_is_pointer) {
  token_kind_t kind = m ? m->tag_kind : TK_EOF;
  char *name = m ? m->tag_name : NULL;
  int len = m ? m->tag_len : 0;
  int is_pointer = m && m->is_tag_pointer;
  if (m && m->decl_type) {
    const psx_type_t *type = m->decl_type;
    while (type && type->kind == PSX_TYPE_ARRAY) type = type->base;
    is_pointer = type && type->kind == PSX_TYPE_POINTER ? 1 : 0;
    if (type && type->kind == PSX_TYPE_POINTER && type->base) type = type->base;
    if (psx_type_is_tag_aggregate(type)) {
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

bool psx_ctx_get_tag_member_info(token_kind_t kind, char *name, int len, int index,
                                  tag_member_info_t *out);
bool psx_ctx_find_tag_member_info(token_kind_t kind, char *name, int len,
                                   char *member_name, int member_len,
                                   tag_member_info_t *out);
bool psx_ctx_get_tag_member_info_at_scope(token_kind_t kind, char *name, int len,
                                          int scope_depth, int index,
                                          tag_member_info_t *out);
bool psx_ctx_find_tag_member_info_at_scope(token_kind_t kind, char *name, int len,
                                           int scope_depth,
                                           char *member_name, int member_len,
                                           tag_member_info_t *out);
int psx_ctx_get_tag_member_count(token_kind_t kind, char *name, int len);
int psx_ctx_get_tag_member_count_at_scope(token_kind_t kind, char *name, int len, int scope_depth);
int psx_ctx_get_tag_scope_depth(token_kind_t kind, char *name, int len);

#endif
