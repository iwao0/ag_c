#ifndef PARSER_TAG_MEMBER_PUBLIC_H
#define PARSER_TAG_MEMBER_PUBLIC_H

#include "core.h"
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
} tag_member_info_t;

static inline psx_decl_funcptr_sig_t psx_ctx_tag_member_funcptr_sig(
    const tag_member_info_t *m) {
  if (!m) return (psx_decl_funcptr_sig_t){0};
  return m->is_funcptr ? m->funcptr_sig : (psx_decl_funcptr_sig_t){0};
}

static inline void psx_ctx_tag_member_set_funcptr_sig(
    tag_member_info_t *m, psx_decl_funcptr_sig_t sig) {
  if (!m) return;
  m->funcptr_sig = sig;
  m->is_funcptr = psx_decl_funcptr_sig_has_payload(sig) ? 1 : 0;
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
