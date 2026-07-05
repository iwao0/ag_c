#ifndef PARSER_DECL_H
#define PARSER_DECL_H

/* decl.h は AST node 型 (node_t) と シンボルテーブル (global_var_t) の
 * 両方を使う。Phase C1-2: 両ヘッダを明示的に include する。 */
#include "ast.h"
#include "symtab.h"
#include "semantic_ctx.h"  /* psx_ctx_get_tag_scope_depth (inline setter で使う) */

typedef struct lvar_t lvar_t;
struct lvar_t {
  lvar_t *next;
  lvar_t *next_all;  // 全スコープの変数リスト（未使用チェック・offset検索用）
  lvar_t *next_hash; // 名前ハッシュ表のチェーン（psx_decl_find_lvar の O(1) 化用）
  lvar_t *next_offhash; // オフセットハッシュ表のチェーン（psx_decl_find_lvar_by_offset 用）
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
  /* タグ宣言時のスコープ深度 + 1 (内側 shadow 対応)。0 = 未設定 (calloc 初期値)、
   * >0 のとき (実際の depth = この値 - 1) を psx_ctx_find_tag_member_info_at_scope に
   * 渡し、変数宣言時に見ていた tag の member を引く。+1 エンコードにすることで
   * calloc/arena_alloc がそのまま未設定扱いになる。 */
  int tag_scope_depth_p1;
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
  unsigned int is_unevaluated_used : 1; // 1: sizeof 等の未評価オペランドで参照された
  unsigned int is_param : 1;          // 1: 関数パラメータ
  unsigned int is_initialized : 1;   // 1: 初期化済み（宣言初期化子または代入）
  unsigned int is_complex : 1;       // 1: _Complex型
  unsigned int is_atomic : 1;        // 1: _Atomic型
  // 1: 可変長関数ポインタ (`int (*f)(int, ...)`)。経由呼び出しで variadic ABI を使う。
  unsigned int is_variadic_funcptr : 1;
  short funcptr_nargs_fixed;          // 可変長関数ポインタの固定引数数 (`...` の前)
  // 関数ポインタの各仮引数 fp 種別 (2bit ずつ, 0=非fp/1=float/2=double, 最大8引数)。
  // 経由呼び出し `fp(3)` で int 実引数を fp 仮引数へ昇格するのに使う。
  unsigned short funcptr_param_fp_mask;
  unsigned short funcptr_param_int_mask;
  unsigned char funcptr_ret_int_width;
  short funcptr_ret_pointee_array_first_dim;
  short funcptr_ret_pointee_array_second_dim;
  short funcptr_ret_pointee_array_elem_size;
  unsigned int funcptr_ret_is_void : 1;
  unsigned int funcptr_ret_is_data_pointer : 1;
  unsigned int funcptr_ret_is_complex : 1;
  // 1: 指す関数の戻り値が関数ポインタ (`int (*(*p)(int))(int,int)` の `p`)。
  //    `(*p)(...)` の結果を呼ぶとき余分な deref をしないために使う。
  unsigned int funcptr_ret_is_pointer : 1;
  // 1: _Bool 型。代入/初期化時に rhs を `(rhs != 0) ? 1 : 0` に正規化する (C11 6.3.1.2)
  unsigned int is_bool : 1;
  // _Generic で long と long long、char と signed/unsigned char を別型として扱うため
  // (サイズだけでは区別できない)。識別子参照ノードへ伝播し infer_generic_control_type が読む。
  unsigned int is_long_long : 1;
  unsigned int is_plain_char : 1;
  unsigned int is_long_double : 1;  // 1: long double (_Generic で double と区別)
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
  /* ローカル `T (*p[M])[N]`: 配列要素は pointer-to-array。
   * struct メンバ側の同名フィールドと同じく、subscript 結果を single pointer-to-array
   * 表現へ組み直すため pointee 配列全体のバイト数を保持する。 */
  int ptr_array_pointee_bytes;
  int align_bytes; // 0 = natural alignment
  // 多次元配列サポート用
  int outer_stride;             // 1次サブスクリプトのストライド（直下の次元 1 つ分のバイトサイズ）
  int mid_stride;               // 3D 配列 `a[N1][N2][N3]` の 2 次サブスクリプトのストライド（=N3*elem）。0=2D以下。
  // 4 次元以上の追加ストライド: extra_strides[i] は (3+i+1) 段目サブスクリプトの
  // ストライド。例: 4D `a[N1][N2][N3][N4]` では outer=N2*N3*N4*e, mid=N3*N4*e、
  // 3 段目で使う N4*e は next_deref_size 経由で運ばれ、extra_strides は最後の
  // elem ストライドのみ (count=1) を持つ。5D ならさらに 1 段追加。
  /* 4 次元以上の追加ストライド (最大 5 = 8 次元まで)。4D+ 配列は稀なので、全 lvar に
   * 20B 抱えさせず、必要なときだけ calloc(5) する (未使用時 NULL)。読みは必ず
   * extra_strides_count>0 で guard 済み (count>0 ⟺ 確保済み)。 */
  int *extra_strides;
  unsigned char extra_strides_count;
  int vla_row_stride_frame_off; // N-D VLA: 次の subscript で消費する runtime stride のフレームオフセット (0=定数 stride)
  /* N-D VLA (N >= 3): vla_row_stride_frame_off の後にさらに何個の runtime stride スロット
   * が続くか。2D は 0 (= row が最後)、3D は 1 (row の後に 1 個 = k*elem)、4D は 2、…
   * subscript で 1 段消費するごとに result の vla_strides_remaining = max(-1, parent-1)、
   * vla_row_stride_frame_off は += 8 シフト (remaining が 0 になったときは 0 にクリア)。
   * これにより任意 N-D VLA を 8 バイト/段で連鎖的に解決できる。 */
  int vla_strides_remaining;
  /* 2D VLA 関数パラメータ (`int g[n][m]`): 関数 entry 時に
   *   *[vla_row_stride_frame_off] = *[vla_row_stride_src_offset] * vla_row_stride_elem_size
   * を計算する。src は同一関数内で先に登録された別パラメータ (内側 dim 識別子)。
   * 0 = この機構を使わない (通常の VLA / 定数 dim)。 */
  int vla_row_stride_src_offset;
  short vla_row_stride_elem_size;
  /* N-D VLA 仮引数 (`int t[n][m][k][l]` 等): pointee 多次元配列の内側 dim 各値の source。
   * idx 0 が外側 (m に相当)、count-1 が最内 (l に相当)。
   * vla_param_inner_dim_consts[i] > 0 のとき定数 dim、== 0 のときは
   * vla_param_inner_dim_src_offsets[i] が param frame offset を示す runtime dim。
   * vla_param_inner_dim_count = 内側 dim 数 (= 配列次元数 - 1)。
   * 関数 entry で emit_vla_row_stride_for_params がこれを使って N-1 個の stride スロットを
   * 計算する。2D 専用の vla_row_stride_src_offset/elem_size は 2D 後方互換で残置。 */
  short vla_param_inner_dim_consts[7];
  int vla_param_inner_dim_src_offsets[7];
  unsigned char vla_param_inner_dim_count;
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
  /* 宣言時に見えているタグの scope_depth を +1 して保存 (0=未設定の規約)。
   * 後段でメンバ参照経路が「変数宣言時に見えていた tag」のメンバを引けるようにする
   * (内側 shadow からの外側変数参照対応)。タグ無しは 0 のまま (未設定)。 */
  if (tag_kind == TK_STRUCT || tag_kind == TK_UNION) {
    int sd = psx_ctx_get_tag_scope_depth(tag_kind, tag_name, tag_len);
    var->tag_scope_depth_p1 = (sd >= 0) ? (sd + 1) : 0;
  } else {
    var->tag_scope_depth_p1 = 0;
  }
}
static inline void psx_decl_set_gvar_tag(global_var_t *gv,
                                          token_kind_t tag_kind, char *tag_name, int tag_len,
                                          int is_tag_pointer) {
  gv->tag_kind = tag_kind;
  gv->tag_name = tag_name;
  gv->tag_len = tag_len;
  gv->is_tag_pointer = is_tag_pointer ? 1 : 0;
  if (tag_kind == TK_STRUCT || tag_kind == TK_UNION) {
    int sd = psx_ctx_get_tag_scope_depth(tag_kind, tag_name, tag_len);
    gv->tag_scope_depth_p1 = (sd >= 0) ? (sd + 1) : 0;
  } else {
    gv->tag_scope_depth_p1 = 0;
  }
}

void psx_decl_reset_locals(void);
void psx_decl_enter_scope(void);
void psx_decl_leave_scope(void);
lvar_t *psx_decl_get_locals(void);
void psx_decl_reserve_variadic_regs(void);
/* 宣言子 trailing `()` の解析前にリセットし、skip_func_param_list で消費する。
 * 直後に psx_last_funcptr_is_variadic / psx_last_funcptr_nargs_fixed で可変長情報を読む。 */
void psx_reset_funcptr_signature_state(void);
void psx_skip_func_param_list(void);
int psx_last_funcptr_is_variadic(void);
int psx_last_funcptr_nargs_fixed(void);
unsigned short psx_last_funcptr_param_fp_mask(void);
unsigned short psx_last_funcptr_param_int_mask(void);
unsigned char psx_funcptr_ret_int_width_from_kind(token_kind_t kind, int is_pointer,
                                                  tk_float_kind_t fp_kind);
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
