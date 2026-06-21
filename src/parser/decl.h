#ifndef PARSER_DECL_H
#define PARSER_DECL_H

/* decl.h は AST node 型 (node_t) と シンボルテーブル (global_var_t) の
 * 両方を使う。Phase C1-2: 両ヘッダを明示的に include する。 */
#include "ast.h"
#include "symtab.h"

typedef struct lvar_t lvar_t;
struct lvar_t {
  lvar_t *next;
  lvar_t *next_all;  // 全スコープの変数リスト（未使用チェック・offset検索用）
  lvar_t *next_hash; // 名前ハッシュ表のチェーン（psx_decl_find_lvar の O(1) 化用）
  unsigned scope_seq; // 宣言時スコープの一意連番（同一スコープ重複宣言チェック用）
  char *name;
  int len;
  int offset;
  int size;
  int elem_size;
  tk_float_kind_t fp_kind;
  tk_float_kind_t pointee_fp_kind;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  unsigned int is_array : 1;
  unsigned int is_vla : 1;            // 1: 可変長配列 (VLA) - offsetはベースポインタスロット
  unsigned int is_byref_param : 1;    // 1: >16バイト構造体の値渡し仮引数 - フレームスロットはポインタ(8B)、elemは実際の構造体サイズ
  unsigned int is_tag_pointer : 1;
  unsigned int is_const_qualified : 1;
  unsigned int is_volatile_qualified : 1;
  unsigned int is_pointer_const_qualified : 1;
  unsigned int is_pointer_volatile_qualified : 1;
  unsigned int is_unsigned : 1;       // 1: unsigned type
  unsigned int is_used : 1;           // 1: 参照された
  unsigned int is_param : 1;          // 1: 関数パラメータ
  unsigned int is_initialized : 1;   // 1: 初期化済み（宣言初期化子または代入）
  unsigned int is_complex : 1;       // 1: _Complex型
  unsigned int is_atomic : 1;        // 1: _Atomic型
  // 1: 可変長関数ポインタ (`int (*f)(int, ...)`)。経由呼び出しで variadic ABI を使う。
  unsigned int is_variadic_funcptr : 1;
  short funcptr_nargs_fixed;          // 可変長関数ポインタの固定引数数 (`...` の前)
  // 1: _Bool 型。代入/初期化時に rhs を `(rhs != 0) ? 1 : 0` に正規化する (C11 6.3.1.2)
  unsigned int is_bool : 1;
  // _Generic で long と long long、char と signed/unsigned char を別型として扱うため
  // (サイズだけでは区別できない)。識別子参照ノードへ伝播し infer_generic_control_type が読む。
  unsigned int is_long_long : 1;
  unsigned int is_plain_char : 1;
  // 1: `static` 付きで宣言されたローカル変数。フレーム上には配置されず、
  //    static_global_name のグローバル変数に lowering される。
  //    識別子解決時に ND_LVAR ではなく ND_GVAR を返すフラグ。
  unsigned int is_static_local : 1;
  // 1: ポインタの pointee 型が void (`void *p` 等)。
  //    deref のエラー検出に使う (C11 6.5.3.2)。
  unsigned int pointee_is_void : 1;
  char *static_global_name;
  int static_global_name_len;
  unsigned int pointer_const_qual_mask;
  unsigned int pointer_volatile_qual_mask;
  int pointer_qual_levels;
  short base_deref_size; // 多段ポインタの最内ポインタが指す要素サイズ（int**→4）
  int align_bytes; // 0 = natural alignment
  // 多次元配列サポート用
  int outer_stride;             // 1次サブスクリプトのストライド（直下の次元 1 つ分のバイトサイズ）
  int mid_stride;               // 3D 配列 `a[N1][N2][N3]` の 2 次サブスクリプトのストライド（=N3*elem）。0=2D以下。
  // 4 次元以上の追加ストライド: extra_strides[i] は (3+i+1) 段目サブスクリプトの
  // ストライド。例: 4D `a[N1][N2][N3][N4]` では outer=N2*N3*N4*e, mid=N3*N4*e、
  // 3 段目で使う N4*e は next_deref_size 経由で運ばれ、extra_strides は最後の
  // elem ストライドのみ (count=1) を持つ。5D ならさらに 1 段追加。
  int extra_strides[5];         // 最大 8 次元 (3 + 5) まで対応
  unsigned char extra_strides_count;
  int vla_row_stride_frame_off; // 2D VLA(内側も可変): 行ストライドを格納するフレームオフセット（0=定数stride）
  /* 2D VLA 関数パラメータ (`int g[n][m]`): 関数 entry 時に
   *   *[vla_row_stride_frame_off] = *[vla_row_stride_src_offset] * vla_row_stride_elem_size
   * を計算する。src は同一関数内で先に登録された別パラメータ (内側 dim 識別子)。
   * 0 = この機構を使わない (通常の VLA / 定数 dim)。 */
  int vla_row_stride_src_offset;
  short vla_row_stride_elem_size;
};

/* lvar_t / global_var_t の tag 4 フィールド (kind/name/len/is_tag_pointer)
 * を 1 行で設定するヘルパ (Phase A2 リファクタリング)。
 * decl.c / parser.c で 4 行のパターンが 9 箇所重複していたのを集約する。 */
static inline void psx_decl_set_var_tag(lvar_t *var,
                                         token_kind_t tag_kind, char *tag_name, int tag_len,
                                         int is_tag_pointer) {
  var->tag_kind = tag_kind;
  var->tag_name = tag_name;
  var->tag_len = tag_len;
  var->is_tag_pointer = is_tag_pointer ? 1 : 0;
}
static inline void psx_decl_set_gvar_tag(global_var_t *gv,
                                          token_kind_t tag_kind, char *tag_name, int tag_len,
                                          int is_tag_pointer) {
  gv->tag_kind = tag_kind;
  gv->tag_name = tag_name;
  gv->tag_len = tag_len;
  gv->is_tag_pointer = is_tag_pointer ? 1 : 0;
}

void psx_decl_reset_locals(void);
void psx_decl_enter_scope(void);
void psx_decl_leave_scope(void);
lvar_t *psx_decl_get_locals(void);
void psx_decl_reserve_variadic_regs(void);
lvar_t *psx_decl_find_lvar(char *name, int len);
lvar_t *psx_decl_find_lvar_by_offset(int offset);
lvar_t *psx_decl_register_lvar(char *name, int len);
lvar_t *psx_decl_register_lvar_sized(char *name, int len, int size, int elem_size, int is_array);
lvar_t *psx_decl_register_lvar_sized_align(char *name, int len, int size, int elem_size, int is_array, int align);

node_t *psx_decl_parse_declaration(void);
node_t *psx_decl_parse_declaration_after_type(int elem_size, tk_float_kind_t decl_fp_kind,
                                              token_kind_t tag_kind, char *tag_name, int tag_len,
                                              int base_is_pointer,
                                              int is_const_qualified, int is_volatile_qualified,
                                              int decl_is_unsigned_hint);
// ex 版: typedef が配列型のとき、その dims を override で渡す
// (`typedef int M[2][3][4]; M m;` で M m を int[2][3][4] と等価扱いする)。
node_t *psx_decl_parse_declaration_after_type_ex(int elem_size, tk_float_kind_t decl_fp_kind,
                                                 token_kind_t tag_kind, char *tag_name, int tag_len,
                                                 int base_is_pointer,
                                                 int is_const_qualified, int is_volatile_qualified,
                                                 int decl_is_unsigned_hint,
                                                 const int *td_array_dims, int td_array_dim_count,
                                                 int decl_base_is_void,
                                                 int decl_base_is_bool);
node_t *psx_decl_parse_initializer_for_var(lvar_t *var, int is_pointer);
/* AST 上の式ノードを定数畳み込みして long long を返す。
 * ok=1 を返した時のみ結果は有効。ND_NUM, ND_ADD/SUB/..., 三項などを扱う。 */
long long psx_decl_eval_const_int(node_t *n, int *ok);

// `{ ... }` のトップレベル要素数を先読みで返す。curtok は変更しない。
// 推定不可なら 0。指定初期化子 `[N]=` で位置がジャンプする場合は最大位置+1 を返す。
long long psx_decl_count_brace_init_elements(token_t *brace_tok);

/* parser.c の brace init flat パーサ。global_var_t の init_values[] /
 * init_value_symbols[] / init_value_symbol_lens[] / init_fvalues[] を埋める。
 * static local 配列の lowering (decl.c) からも再利用する。 */
void psx_parse_global_brace_init_flat(global_var_t *gv, int *cap, int start_idx);

#endif
