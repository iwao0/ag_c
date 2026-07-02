#ifndef PARSER_SEMANTIC_CTX_H
#define PARSER_SEMANTIC_CTX_H

#include "ret_pointee_array.h"
#include "../tokenizer/token.h"
#include <stdbool.h>

void psx_ctx_reset_function_scope(void);
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

bool psx_ctx_has_tag_type(token_kind_t kind, char *name, int len);
void psx_ctx_define_tag_type(token_kind_t kind, char *name, int len);
void psx_ctx_define_tag_type_with_members(token_kind_t kind, char *name, int len, int member_count);
int psx_ctx_get_tag_member_count(token_kind_t kind, char *name, int len);
void psx_ctx_define_tag_type_with_layout(token_kind_t kind, char *name, int len, int member_count, int tag_size);
int psx_ctx_get_tag_size(token_kind_t kind, char *name, int len);
void psx_ctx_set_pending_tag_align(int align);
int psx_ctx_get_tag_align(token_kind_t kind, char *name, int len);
/* struct/union メンバの float/double 種別を後付けで設定する。
 * 取得は psx_ctx_get_tag_member_info / _find_tag_member_info 経由。 */
void psx_ctx_set_tag_member_fp_kind(token_kind_t tag_kind, char *tag_name, int tag_len,
                                     char *member_name, int member_len,
                                     tk_float_kind_t fp_kind);
void psx_ctx_set_tag_member_is_bool(token_kind_t tag_kind, char *tag_name, int tag_len,
                                     char *member_name, int member_len, int is_bool);
void psx_ctx_set_tag_member_is_unsigned(token_kind_t tag_kind, char *tag_name, int tag_len,
                                        char *member_name, int member_len, int is_unsigned);
void psx_ctx_set_tag_member_outer_stride(token_kind_t tag_kind, char *tag_name, int tag_len,
                                          char *member_name, int member_len, int outer_stride);
/* 3 次元以上の配列メンバの中間段ストライド (1 段 subscript 後の要素サイズ) を保存する。
 * 3D `char c[2][2][3]` なら 3 (=arr_dims[1]*arr_dims[2]*elem / arr_dims[1])。 */
void psx_ctx_set_tag_member_mid_stride(token_kind_t tag_kind, char *tag_name, int tag_len,
                                       char *member_name, int member_len, int mid_stride);
/* 多次元 char 配列メンバ (`char c[2][2][3]`) の各次元サイズを保存する。
 * dims[0..ndim) を最外側から並べる。ndim<=8。グローバル brace init の再帰
 * 展開で 1 段ずつ消費する (gbrace_ctx_t.sub_dims を介して)。 */
void psx_ctx_set_tag_member_arr_dims(token_kind_t tag_kind, char *tag_name, int tag_len,
                                     char *member_name, int member_len,
                                     const int *dims, int ndim);
/* array-of-pointer-to-array メンバ (`int (*p[M])[N]`) の各要素ポインタが指す配列の
 * 全バイト数 (= N * elem) を保存する。 */
void psx_ctx_set_tag_member_ptr_array_pointee_bytes(token_kind_t tag_kind, char *tag_name, int tag_len,
                                                     char *member_name, int member_len, int bytes);
void psx_ctx_set_tag_member_funcptr_param_fp_mask(token_kind_t tag_kind, char *tag_name, int tag_len,
                                                  char *member_name, int member_len,
                                                  unsigned short mask);
void psx_ctx_set_tag_member_funcptr_param_int_mask(token_kind_t tag_kind, char *tag_name, int tag_len,
                                                   char *member_name, int member_len,
                                                   unsigned short mask);

/* struct/union メンバの全属性を 1 回のクエリで取得する統合 API
 * (docs/code_refactoring_2026 Phase A1)。
 *
 * 既存の 5 つに分散した getter (`_at` / `_bf` / `_fp_kind` / `_is_bool` / `_count`)
 * の wrapper として実装され、呼び出し側で `(tag_kind, tag_name, tag_len)` の
 * 3-tuple を毎回繰り返し渡す冗長性を解消する。
 *
 * 取得失敗 (member 不存在) なら false。bitfield/fp_kind/is_bool は 0 で
 * 初期化されるので、struct メンバが bitfield でないとき bit_width=0 等を返す。 */
typedef struct {
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
  int is_variadic_funcptr;
  short funcptr_nargs_fixed;
  unsigned short funcptr_param_fp_mask;
  unsigned short funcptr_param_int_mask;
  unsigned char funcptr_ret_int_width;
  psx_ret_pointee_array_t funcptr_ret_pointee_array;
  int funcptr_ret_is_void;
  int funcptr_ret_is_pointer;
  int funcptr_ret_is_complex;
} tag_member_info_t;

bool psx_ctx_get_tag_member_info(token_kind_t kind, char *name, int len, int index,
                                  tag_member_info_t *out);
/* 名前検索版の統合 API。`psx_ctx_get_tag_member_info` と対になる。
 * 取得失敗 (member 不存在) なら false。bitfield/fp_kind/is_bool は 0 で
 * 初期化される。 */
bool psx_ctx_find_tag_member_info(token_kind_t kind, char *name, int len,
                                   char *member_name, int member_len,
                                   tag_member_info_t *out);
/* 上記 2 つの「特定スコープ深度に固定」版。タグ shadowing の応用形 (内側スコープでの
 * 外側変数メンバ参照、ネスト 2 段 shadow) で、変数の宣言時 tag_scope_depth を渡して
 * 最も内側ではなく「変数が見ていたタグの scope」のメンバを引くのに使う。scope_depth<0
 * のときは既存挙動 (最も内側を使う) と等価。 */
bool psx_ctx_get_tag_member_info_at_scope(token_kind_t kind, char *name, int len,
                                          int scope_depth, int index,
                                          tag_member_info_t *out);
bool psx_ctx_find_tag_member_info_at_scope(token_kind_t kind, char *name, int len,
                                           int scope_depth,
                                           char *member_name, int member_len,
                                           tag_member_info_t *out);
/* (kind, name, len) のタグが現在見えているスコープ深度を返す。タグが無ければ -1。
 * 変数宣言時に呼んで lvar/global_var の tag_scope_depth に保存するのに使う。 */
int psx_ctx_get_tag_scope_depth(token_kind_t kind, char *name, int len);
int psx_ctx_get_tag_member_count_at_scope(token_kind_t kind, char *name, int len, int scope_depth);
/* 現在見えている tag とそのメンバを file scope に昇格する。関数内 static aggregate を
 * global lowering した後も codegen が匿名タグのレイアウトを参照できるようにする。 */
void psx_ctx_promote_tag_to_file_scope(token_kind_t kind, char *name, int len);
/* (tag_kind, tag_name, tag_len) で識別される tag に、メンバ記述子 *m を追加/上書きする。
 * m の name/len/offset/type_size/deref_size/array_len/tag_*(メンバのネストタグ)/
 * is_tag_pointer/bit_width/bit_offset/bit_is_signed を読む。fp_kind/is_bool/is_unsigned/
 * outer_stride は add 時には使わず、別 setter (psx_ctx_set_tag_member_*) で後付けする。 */
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
  int is_variadic_funcptr;      // `typedef int (*vf)(int, ...)` 等の可変長関数ポインタ typedef
  short funcptr_nargs_fixed;    // 可変長関数ポインタの固定引数数 (`...` の前)
  int funcptr_ret_is_void;      // 指し示す関数の戻り値が void
  int funcptr_ret_is_pointer;   // 指し示す関数の戻り値がポインタ (`struct S * (*)()` → 1)
  int funcptr_ret_is_complex;   // 指し示す関数の戻り値が _Complex
  unsigned char funcptr_ret_int_width; // 指し示す関数の整数戻り幅 (4/8, 0=非整数/未知)
  unsigned short funcptr_param_fp_mask; // 関数ポインタ仮引数の fp 種別 (2bit * 最大8)
  unsigned short funcptr_param_int_mask; // 関数ポインタ仮引数の整数幅/ptr (1=4B, 2=8B, 3=ptr; 2bit * 最大8)
  psx_ret_pointee_array_t funcptr_ret_pointee_array; // 関数ポインタ戻り値が `T (*)[N][M]` のときの pointee 配列
} psx_typedef_info_t;

/* typedef 名を登録する。戻り値 1 = 成功 (新規 or 互換な再宣言)、
 * 0 = 既存と型が異なる衝突。呼び出し元で 0 のとき診断を出す。 */
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
void psx_ctx_set_function_ret_tag(char *name, int len, token_kind_t tag_kind, char *tag_name, int tag_len);
bool psx_ctx_has_function_name(char *name, int len);
int psx_ctx_get_function_ret_struct_size(char *name, int len);
// 関数戻り値の浮動小数点種別 (float/double) を取得/設定する。
// `(int)func()` キャストで FP→int 変換 (fcvtzs) を挿入するために必要。
void psx_ctx_set_function_ret_fp_kind(char *name, int len, tk_float_kind_t fp_kind);
tk_float_kind_t psx_ctx_get_function_ret_fp_kind(char *name, int len);
// 関数戻り値が _Complex かどうかを保持する。呼び出し側 funcall ノードの is_complex
// 伝播 (HFA 戻り値 d0/d1 の受け取り) に使う。
void psx_ctx_set_function_ret_is_complex(char *name, int len, int is_complex);
int psx_ctx_get_function_ret_is_complex(char *name, int len);
// 関数が variadic (`...` を持つ) かどうかと固定引数の個数を保持する。
// Apple ARM64 ABI で variadic 引数を stack に積むため、呼び出し側 codegen が
// `nargs_fixed` を境に register / stack を切り替えるのに使う。
/* 仮引数 i の fp_kind を記録/取得。呼び出し側 IR が int 実引数→double 仮引数
 * の暗黙変換に I2F キャストを挿入するために使う。track は最初の 16 引数まで。 */
void psx_ctx_set_function_param_fp_kind(char *name, int len, int param_idx,
                                         tk_float_kind_t fp_kind);
tk_float_kind_t psx_ctx_get_function_param_fp_kind(char *name, int len, int param_idx);
/* 仮引数 i が整数スカラのときの幅 (4/8、0 = 非整数) を記録/取得。呼び出し側 IR が
 * fp 実引数→整数仮引数の暗黙変換に F2I キャストを挿入するために使う。 */
void psx_ctx_set_function_param_int_size(char *name, int len, int param_idx, int size);
int psx_ctx_get_function_param_int_size(char *name, int len, int param_idx);
void psx_ctx_set_function_param_int_unsigned(char *name, int len, int param_idx, int is_unsigned);
int psx_ctx_get_function_param_int_unsigned(char *name, int len, int param_idx);
void psx_ctx_set_function_variadic(char *name, int len, int is_variadic, int nargs_fixed);
/* 同名関数の再宣言で引数数 / 可変長性が一致するかを track する (C11 6.7p4)。
 * 初回呼び出しは記録、以降は比較。一致なら 1、不一致なら 0。 */
int psx_ctx_track_function_nargs(char *name, int len, int nargs, int is_variadic);

/* 引数 i の型カテゴリ (粗粒度)。同名関数の再宣言での型一致照合に使う。 */
enum {
  PSX_PCAT_UNSET = 0,
  PSX_PCAT_INT4  = 1,  /* char/short/int / _Bool */
  PSX_PCAT_INT8  = 2,  /* long / long long */
  PSX_PCAT_FLOAT = 3,
  PSX_PCAT_DOUBLE = 4,
  PSX_PCAT_PTR   = 5,
  PSX_PCAT_STRUCT = 6, /* struct/union 値 */
  PSX_PCAT_OTHER  = 7,
};

/* 同名関数の再宣言で引数 i のカテゴリが一致するかを track する (C11 6.7p4)。
 * 初回呼び出しは記録、以降は比較。一致なら 1、不一致なら 0。
 * カテゴリは粗粒度 (int width / fp / pointer / struct) で K&R 互換のため厳密型は照合しない。 */
int psx_ctx_track_function_param_category(char *name, int len, int idx, int category);
int psx_ctx_get_function_param_category(char *name, int len, int idx);

/* 同名関数の本体定義が初回かどうかを track する (C11 6.9p3)。
 * 初回なら 1 を返して定義済みフラグを立てる、すでに定義済みなら 0。 */
int psx_ctx_track_function_defined(char *name, int len);
int psx_ctx_is_function_defined(char *name, int len);
bool psx_ctx_get_function_is_variadic(char *name, int len, int *out_nargs_fixed);
int psx_ctx_get_function_nargs_fixed(char *name, int len);
/* 戻り値型が void かどうかを保持/問い合わせる。代入や初期化での
 * void 値使用 (C11 6.5.16 制約違反) の検出に使う。 */
void psx_ctx_set_function_ret_void(char *name, int len, int is_void);
bool psx_ctx_is_function_ret_void(char *name, int len);
/* 関数の戻り値型を track する。既存と異なる型なら 0 を返す。 */
int psx_ctx_track_function_ret_type(char *name, int len,
                                     token_kind_t ret_token_kind, int ret_is_pointer);
/* 関数の戻り値がポインタ型 (`int *f(void)` 等) ならば 1 を返す。 */
int psx_ctx_get_function_ret_is_pointer(char *name, int len);
void psx_ctx_set_function_ret_is_funcptr(char *name, int len, int is_funcptr,
                                         int funcptr_ret_is_pointer);
int psx_ctx_get_function_ret_is_funcptr(char *name, int len);
int psx_ctx_get_function_funcptr_ret_is_pointer(char *name, int len);
void psx_ctx_set_function_funcptr_ret_int_width(char *name, int len, int width);
int psx_ctx_get_function_funcptr_ret_int_width(char *name, int len);
/* 関数の戻り値型トークン (TK_INT / TK_LONG 等)。未登録は TK_EOF。 */
token_kind_t psx_ctx_get_function_ret_token_kind(char *name, int len);
/* 戻り値型の unsigned 性。`unsigned` は TK_INT に潰れるため別管理。 */
void psx_ctx_set_function_ret_unsigned(char *name, int len, int is_unsigned);
int psx_ctx_get_function_ret_is_unsigned(char *name, int len);
/* 戻り値がポインタ型のとき、pointee の const/volatile 修飾を保持する。 */
void psx_ctx_set_function_ret_pointee_qualifiers(char *name, int len,
                                                 int is_const, int is_volatile);
int psx_ctx_get_function_ret_pointee_const(char *name, int len);
int psx_ctx_get_function_ret_pointee_volatile(char *name, int len);
/* 戻り値型が `int (*f())[N]` (配列へのポインタ) のときの先頭次元 N (それ以外 0)。
 * 呼び出し結果 `f()[i]` の行ストライドを N*elem にするのに使う。 */
void psx_ctx_set_function_ret_pointee_array_first_dim(char *name, int len, int first_dim);
int psx_ctx_get_function_ret_pointee_array_first_dim(char *name, int len);
void psx_ctx_set_function_ret_pointee_array_second_dim(char *name, int len, int second_dim);
int psx_ctx_get_function_ret_pointee_array_second_dim(char *name, int len);
/* 戻り値型のポインタ段数 (`int *g()`=1, `int **g()`=2, 非ポインタ=0)。多段ポインタ戻り
 * `int **g(); **g()` の deref を正しい幅で組むのに使う。 */
void psx_ctx_set_function_ret_pointer_levels(char *name, int len, int levels);
int psx_ctx_get_function_ret_pointer_levels(char *name, int len);
void psx_ctx_get_function_ret_tag(char *name, int len, token_kind_t *out_tag_kind,
                                  char **out_tag_name, int *out_tag_len);

bool psx_ctx_is_type_token(token_kind_t kind);
bool psx_ctx_is_tag_keyword(token_kind_t kind);
int psx_ctx_scalar_type_size(token_kind_t kind);
void psx_ctx_get_type_info(token_kind_t kind, bool *is_type_token, int *scalar_size);

#endif
