#ifndef PARSER_SEMANTIC_CTX_H
#define PARSER_SEMANTIC_CTX_H

#include "core.h"
#include "function_public.h"
#include "tag_member_public.h"
#include "type.h"
#include "../tokenizer/token.h"
#include <stdbool.h>

void psx_ctx_reset_function_scope(void);
void psx_ctx_reset_translation_unit_scope(void);
void psx_ctx_reset_function_names(void);
/* 各 parse 開始時に呼ぶソフトリセット: 関数情報は残し、診断フラグ (is_defined / nargs_set_once
 * / ret_set_once / param_categories) のみクリア。 */
void psx_ctx_reset_function_diag_state(void);
/* タグの完全型定義状態をソフトリセット (member_count を 0 に戻す)。 */
void psx_ctx_reset_tag_diag_state(void);
void psx_ctx_enter_block_scope(void);
void psx_ctx_leave_block_scope(void);
void psx_ctx_register_goto_ref(char *name, int len, token_t *tok);
void psx_ctx_register_label_def(char *name, int len, token_t *tok);
void psx_ctx_validate_goto_refs(void);
void psx_ctx_record_unsupported_gnu_extension_warning(const token_t *tok, const char *name);
void psx_ctx_emit_deferred_parser_warnings(void);

bool psx_ctx_has_tag_type(token_kind_t kind, char *name, int len);
void psx_ctx_define_tag_type(token_kind_t kind, char *name, int len);
void psx_ctx_define_tag_type_with_members(token_kind_t kind, char *name, int len, int member_count);
void psx_ctx_define_tag_type_with_layout(token_kind_t kind, char *name, int len,
                                         int member_count, int tag_size, int tag_align);
int psx_ctx_get_tag_size(token_kind_t kind, char *name, int len);
int psx_ctx_get_tag_align(token_kind_t kind, char *name, int len);
/* 現在見えている tag とそのメンバを file scope に昇格する。関数内 static aggregate を
 * global lowering した後も codegen が匿名タグのレイアウトを参照できるようにする。 */
void psx_ctx_promote_tag_to_file_scope(token_kind_t kind, char *name, int len);
/* (tag_kind, tag_name, tag_len) で識別される tag に、メンバ記述子 *m を追加/上書きする。
 * m->decl_type は正本として必須。レイアウトcacheはdecl_typeから同期する。 */
void psx_ctx_add_tag_member(token_kind_t tag_kind, char *tag_name, int tag_len,
                            const tag_member_info_t *m);
/* enum 定数を登録する。重複なら 0、新規なら 1 を返す。
 * 呼び出し元で 0 のとき診断を出す。 */
int psx_ctx_define_enum_const(char *name, int len, long long value);
bool psx_ctx_find_enum_const(char *name, int len, long long *out_value);
/* typedef の型記述子。define/find はこの 1 構造体で受け渡す (旧 _ex/_ex2/_ex3 の
 * フィールド堆積をまとめたもの)。array_dims は最大 8 次元、array_dims[0] が最も外側。
 * pointer_levels / scope_depth は別 API・内部管理なのでここには含めない。 */
typedef struct {
  token_kind_t base_kind;
  int elem_size;
  tk_float_kind_t fp_kind;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int is_pointer;
  int sizeof_size;
  int pointee_const_qualified;
  int pointee_volatile_qualified;
  int is_unsigned;
  int is_long_double;          // typedef した型自体が long double スカラか
  int is_array;                 // typedef した型が配列型 (`typedef int row_t[3]`) か
  int array_first_dim;          // 最外側 `[N]` の N (多次元の mid_stride 計算用)
  int array_dim_count;          // 配列次元数 (0 = 非配列/未知)
  int array_dims[8];            // 各次元サイズ。array_dims[0] が最外側
  int is_funcptr;               // `typedef struct S * (*fty)()` 等の関数ポインタ typedef
  psx_decl_funcptr_sig_t funcptr_sig;
  psx_type_t *decl_type;
} psx_typedef_info_t;

static inline const psx_type_t *psx_ctx_typedef_decl_type(
    const psx_typedef_info_t *info) {
  return info ? info->decl_type : NULL;
}

static inline psx_type_t *psx_ctx_typedef_decl_type_mut(psx_typedef_info_t *info) {
  return info ? info->decl_type : NULL;
}

static inline void psx_ctx_typedef_set_decl_type(psx_typedef_info_t *info,
                                                 psx_type_t *decl_type) {
  if (info) info->decl_type = decl_type;
}

static inline psx_decl_funcptr_sig_t psx_ctx_typedef_funcptr_sig(
    const psx_typedef_info_t *info) {
  if (!info) return (psx_decl_funcptr_sig_t){0};
  const psx_type_t *decl_type = psx_ctx_typedef_decl_type(info);
  if (decl_type)
    return psx_type_funcptr_signature(decl_type);
  return info->is_funcptr ? psx_decl_funcptr_sig_clone(info->funcptr_sig)
                          : (psx_decl_funcptr_sig_t){0};
}

static inline void psx_ctx_typedef_set_funcptr_sig(psx_typedef_info_t *info,
                                                   psx_decl_funcptr_sig_t sig) {
  if (!info) return;
  info->funcptr_sig = psx_decl_funcptr_sig_clone(sig);
  info->is_funcptr = psx_decl_funcptr_sig_has_payload(sig) ? 1 : 0;
  psx_type_t *decl_type = psx_ctx_typedef_decl_type_mut(info);
  if (decl_type && psx_decl_funcptr_sig_has_payload(sig))
    decl_type->funcptr_sig = psx_decl_funcptr_sig_clone(sig);
}

/* typedef 名を登録する。info->decl_type は正本として必須。
 * 戻り値 1 = 成功 (新規 or 互換な再宣言)、0 = decl_type欠落または型衝突。 */
int psx_ctx_define_typedef_name(char *name, int len, const psx_typedef_info_t *info);
/* typedef 名を引く。見つかれば true を返し *out に記述子を書く。
 * out が NULL のときは存在判定のみ (記述子は書かない)。 */
bool psx_ctx_find_typedef_name(char *name, int len, psx_typedef_info_t *out);
// 多段ポインタ typedef (`typedef int **PP`) のポインタ段数を記録/取得する。
// 単段 (`typedef int *PI`) や未設定は getter が is_pointer から 1 を返す。
void psx_ctx_set_typedef_pointer_levels(char *name, int len, int levels);
// ポインタ段数を返す。非ポインタは 0、is_pointer だが段数未設定なら 1。
int psx_ctx_get_typedef_pointer_levels(char *name, int len);
bool psx_ctx_find_typedef_sizeof(char *name, int len, int *out_sizeof_size);
bool psx_ctx_is_typedef_name_token(token_t *tok);
void psx_ctx_define_function_name(char *name, int len);
void psx_ctx_define_function_name_with_ret(char *name, int len, int ret_struct_size);
int psx_ctx_get_function_ret_struct_size(char *name, int len);
// 関数戻り値の浮動小数点種別 (float/double) を取得する。
// `(int)func()` キャストで FP→int 変換 (fcvtzs) を挿入するために必要。
tk_float_kind_t psx_ctx_get_function_ret_fp_kind(char *name, int len);
// 関数戻り値が _Complex かどうかを保持する。呼び出し側 funcall ノードの is_complex
// 伝播 (HFA 戻り値 d0/d1 の受け取り) に使う。
int psx_ctx_get_function_ret_is_complex(char *name, int len);
// 関数が variadic (`...` を持つ) かどうかと固定引数の個数を保持する。
// Apple ARM64 ABI で variadic 引数を stack に積むため、呼び出し側 codegen が
// `nargs_fixed` を境に register / stack を切り替えるのに使う。
/* 仮引数 i の fp_kind を記録/取得。呼び出し側 IR が int 実引数→double 仮引数
 * の暗黙変換に I2F キャストを挿入するために使う。track は最初の 16 引数まで。 */
void psx_ctx_set_function_param_fp_kind(char *name, int len, int param_idx,
                                         tk_float_kind_t fp_kind);
/* 仮引数 i が整数スカラのときの幅 (4/8、0 = 非整数) を記録/取得。呼び出し側 IR が
 * fp 実引数→整数仮引数の暗黙変換に F2I キャストを挿入するために使う。 */
void psx_ctx_set_function_param_int_size(char *name, int len, int param_idx, int size);
void psx_ctx_set_function_param_int_unsigned(char *name, int len, int param_idx, int is_unsigned);
void psx_ctx_set_function_variadic(char *name, int len, int is_variadic, int nargs_fixed);
/* 同名関数の再宣言で引数数 / 可変長性が一致するかを track する (C11 6.7p4)。
 * 初回呼び出しは記録、以降は比較。一致なら 1、不一致なら 0。 */
int psx_ctx_track_function_nargs(char *name, int len, int nargs, int is_variadic);

/* 同名関数の再宣言で引数 i のカテゴリが一致するかを track する (C11 6.7p4)。
 * 初回呼び出しは記録、以降は比較。一致なら 1、不一致なら 0。
 * カテゴリは粗粒度 (int width / fp / pointer / struct) で K&R 互換のため厳密型は照合しない。 */
int psx_ctx_track_function_param_category(char *name, int len, int idx, int category);

/* 同名関数の本体定義が初回かどうかを track する (C11 6.9p3)。
 * 初回なら 1 を返して定義済みフラグを立てる、すでに定義済みなら 0。 */
int psx_ctx_track_function_defined(char *name, int len);
/* 戻り値型が void かどうかを問い合わせる。代入や初期化での
 * void 値使用 (C11 6.5.16 制約違反) の検出に使う。 */
bool psx_ctx_is_function_ret_void(char *name, int len);
/* 関数の戻り値型 descriptor を track する。既存と異なる型なら 0 を返す。 */
int psx_ctx_track_function_ret_type_descriptor(char *name, int len,
                                               const psx_type_t *ret_type);
void psx_ctx_set_function_ret_type(char *name, int len, const psx_type_t *ret_type);
const psx_type_t *psx_ctx_get_function_ret_type(char *name, int len);
/* 関数の戻り値がポインタ型 (`int *f(void)` 等) ならば 1 を返す。 */
int psx_ctx_get_function_ret_is_pointer(char *name, int len);
int psx_ctx_get_function_ret_is_funcptr(char *name, int len);
psx_decl_funcptr_sig_t psx_ctx_get_function_ret_funcptr_sig(char *name, int len);
/* 関数の戻り値型トークン (TK_INT / TK_LONG 等)。未登録は TK_EOF。 */
token_kind_t psx_ctx_get_function_ret_token_kind(char *name, int len);
/* 戻り値型の unsigned 性。`unsigned` は TK_INT に潰れるため別管理。 */
int psx_ctx_get_function_ret_is_unsigned(char *name, int len);
/* 戻り値がポインタ型のとき、pointee の const/volatile 修飾を返す。 */
int psx_ctx_get_function_ret_pointee_const(char *name, int len);
int psx_ctx_get_function_ret_pointee_volatile(char *name, int len);
/* 戻り値型が `int (*f())[N]` (配列へのポインタ) のときの先頭次元 N (それ以外 0)。
 * 呼び出し結果 `f()[i]` の行ストライドを N*elem にするのに使う。 */
int psx_ctx_get_function_ret_pointee_array_first_dim(char *name, int len);
int psx_ctx_get_function_ret_pointee_array_second_dim(char *name, int len);
/* 戻り値型のポインタ段数 (`int *g()`=1, `int **g()`=2, 非ポインタ=0)。多段ポインタ戻り
 * `int **g(); **g()` の deref を正しい幅で組むのに使う。 */
int psx_ctx_get_function_ret_pointer_levels(char *name, int len);
void psx_ctx_get_function_ret_tag(char *name, int len, token_kind_t *out_tag_kind,
                                  char **out_tag_name, int *out_tag_len);

bool psx_ctx_is_type_token(token_kind_t kind);
bool psx_ctx_is_tag_keyword(token_kind_t kind);
bool psx_ctx_is_tag_aggregate_kind(token_kind_t kind);
const char *psx_ctx_tag_kind_spelling(token_kind_t kind);
void psx_ctx_get_type_info(token_kind_t kind, bool *is_type_token, int *scalar_size);

#endif
