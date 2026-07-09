#ifndef PARSER_DECL_H
#define PARSER_DECL_H

/* decl.h は AST node 型 (node_t) と シンボルテーブル (global_var_t) の
 * 両方を使う。Phase C1-2: 両ヘッダを明示的に include する。 */
#include "ast.h"
#include "core.h"
#include "lvar_public.h"
#include "symtab.h"

typedef struct {
  char *name;
  int name_len;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int type_size;
  int init_count;
  int has_init;
  long long init_val;
  char *init_symbol;
  int init_symbol_len;
  long long init_symbol_offset;
  double fval;
  tk_float_kind_t fp_kind;
  int is_array;
  int is_extern_decl;
  int is_static;
  int is_thread_local;
  int is_tag_pointer;
  int has_init_fvalues;
} psx_gvar_view_t;

psx_gvar_view_t psx_gvar_view(const global_var_t *gv);

typedef struct psx_lvar_usage_region_t psx_lvar_usage_region_t;
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
  psx_decl_funcptr_sig_t funcptr_sig;
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
  unsigned int pointee_is_unsigned : 1; // 1: data pointer の pointee が unsigned
  unsigned int pointee_is_bool : 1;     // 1: data pointer の pointee が _Bool
  unsigned int is_used : 1;           // 1: 参照された
  unsigned int is_unevaluated_used : 1; // 1: sizeof 等の未評価オペランドで参照された
  unsigned int is_address_taken : 1;  // 1: & 等でアドレスだけ参照された
  unsigned int suppress_unreachable_warnings : 1; // 1: 到達不能文で宣言され W3003/W3004 を抑制
  unsigned int is_param : 1;          // 1: 関数パラメータ
  unsigned int is_initialized : 1;   // 1: 初期化済み（宣言初期化子または代入）
  unsigned int is_complex : 1;       // 1: _Complex型
  unsigned int is_atomic : 1;        // 1: _Atomic型
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
  int used_count; // 評価済みの値参照回数 (&local の再分類で減らす)
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
  psx_type_t *decl_type;
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
  psx_lvar_usage_region_t *decl_region;
};

typedef enum {
  PSX_LVAR_USAGE_EVALUATED,
  PSX_LVAR_USAGE_UNEVALUATED,
  PSX_LVAR_USAGE_ADDRESS_TAKEN,
  PSX_LVAR_USAGE_INITIALIZED,
} psx_lvar_usage_kind_t;

static inline void psx_decl_invalidate_lvar_decl_type(lvar_t *var) {
  if (var) var->decl_type = NULL;
}

static inline void psx_decl_invalidate_gvar_decl_type(global_var_t *gv) {
  if (gv) gv->decl_type = NULL;
}

/* lvar_t / global_var_t の tag 4 フィールド (kind/name/len/is_tag_pointer)
 * を 1 行で設定するヘルパ (Phase A2 リファクタリング)。
 * decl.c / parser.c で 4 行のパターンが 9 箇所重複していたのを集約する。 */
void psx_decl_set_var_tag(lvar_t *var, token_kind_t tag_kind, char *tag_name, int tag_len,
                          int is_tag_pointer);
void psx_decl_set_gvar_tag(global_var_t *gv, token_kind_t tag_kind, char *tag_name, int tag_len,
                           int is_tag_pointer);

void psx_decl_reset_locals(void);
void psx_decl_enter_scope(void);
void psx_decl_leave_scope(void);
lvar_t *psx_decl_get_locals(void);
void psx_decl_reserve_variadic_regs(void);
unsigned char psx_funcptr_ret_int_width_from_kind(token_kind_t kind, int is_pointer,
                                                  tk_float_kind_t fp_kind);
psx_decl_funcptr_sig_t psx_decl_make_funcptr_sig(const psx_funcptr_signature_t *suffix_sig,
                                                 unsigned char ret_int_width,
                                                 tk_float_kind_t ret_fp_kind,
                                                 psx_ret_pointee_array_t ret_pointee_array,
                                                 int ret_is_void,
                                                 int ret_is_data_pointer,
                                                 int ret_is_funcptr,
                                                 int ret_is_complex);
psx_decl_funcptr_sig_t psx_decl_make_funcptr_sig_from_kind(
    const psx_funcptr_signature_t *suffix_sig, token_kind_t ret_kind,
    tk_float_kind_t fp_kind, int ret_is_data_pointer, int ret_is_funcptr,
    int ret_is_complex, psx_ret_pointee_array_t ret_pointee_array);
lvar_t *psx_decl_find_lvar(char *name, int len);
lvar_t *psx_decl_find_lvar_by_offset(int offset);
void psx_decl_replay_lvar_usage_events(lvar_t *all_locals);
void psx_decl_reset_translation_unit_state(void);
psx_lvar_usage_region_t *psx_decl_begin_lvar_usage_region(void);
void psx_decl_end_lvar_usage_region(psx_lvar_usage_region_t *region);
void psx_decl_suppress_lvar_usage_region(psx_lvar_usage_region_t *region);
void psx_decl_attach_lvar_current_region(lvar_t *var);
lvar_t *psx_decl_register_lvar(char *name, int len);
lvar_t *psx_decl_register_lvar_sized(char *name, int len, int size, int elem_size, int is_array);
lvar_t *psx_decl_register_lvar_sized_align(char *name, int len, int size, int elem_size, int is_array, int align);
void psx_decl_init_lvar_storage_type(lvar_t *var, int size,
                                     int elem_size, int is_array,
                                     tk_float_kind_t fp_kind,
                                     int is_unsigned,
                                     token_kind_t tag_kind,
                                     char *tag_name, int tag_len,
                                     int is_tag_pointer);
void psx_decl_set_lvar_pointer_derived_type(lvar_t *var,
                                            int pointer_qual_levels,
                                            int base_deref_size,
                                            int ptr_array_pointee_bytes);
void psx_decl_set_lvar_pointee_scalar_flags(lvar_t *var,
                                            int is_unsigned, int is_bool);
void psx_decl_set_lvar_pointee_fp_kind(lvar_t *var, tk_float_kind_t fp_kind);
void psx_decl_set_lvar_bool(lvar_t *var, int is_bool);
void psx_decl_set_lvar_complex(lvar_t *var, int is_complex);
void psx_decl_set_lvar_atomic(lvar_t *var, int is_atomic);
void psx_decl_set_lvar_integer_identity(lvar_t *var,
                                        int is_long_long,
                                        int is_plain_char);
void psx_decl_set_lvar_long_double(lvar_t *var, int is_long_double);
void psx_decl_set_lvar_pointee_void(lvar_t *var, int pointee_is_void);
void psx_decl_set_lvar_qualifiers(lvar_t *var,
                                  int is_const_qualified,
                                  int is_volatile_qualified,
                                  int is_pointer_const_qualified,
                                  int is_pointer_volatile_qualified,
                                  unsigned int pointer_const_qual_mask,
                                  unsigned int pointer_volatile_qual_mask);
void psx_decl_set_lvar_array_strides_from_dims(lvar_t *var,
                                               const int *dims, int dim_count,
                                               int elem_size);
void psx_decl_set_lvar_array_strides_from_inner_dims(lvar_t *var,
                                                     const int *inner_dims,
                                                     int inner_dim_count,
                                                     int elem_size);
void psx_decl_set_lvar_vla_descriptor(lvar_t *var,
                                      int outer_stride,
                                      int row_stride_frame_off,
                                      int strides_remaining,
                                      int row_stride_src_offset,
                                      int row_stride_elem_size);
void psx_decl_set_lvar_vla_param_inner_dims(lvar_t *var,
                                            const int *inner_dim_consts,
                                            const int *inner_dim_src_offsets,
                                            int inner_dim_count);
void psx_decl_set_lvar_funcptr_signature(lvar_t *var,
                                         const psx_decl_funcptr_sig_t *sig);
void psx_decl_init_gvar_storage_type(global_var_t *gv, int type_size,
                                     int elem_size, int is_array,
                                     tk_float_kind_t fp_kind,
                                     int is_unsigned,
                                     token_kind_t tag_kind,
                                     char *tag_name, int tag_len,
                                     int is_tag_pointer);
void psx_decl_set_gvar_array_strides_from_dims(global_var_t *gv,
                                               const int *dims, int dim_count,
                                               int elem_size);
void psx_decl_set_gvar_array_strides_from_inner_dims(global_var_t *gv,
                                                     const int *inner_dims,
                                                     int inner_dim_count,
                                                     int elem_size);
void psx_decl_set_gvar_type_size(global_var_t *gv, int type_size);
void psx_decl_set_gvar_pointer_derived_type(global_var_t *gv,
                                            int deref_size,
                                            int pointee_elem_size,
                                            int ptr_array_pointee_bytes);
void psx_decl_set_gvar_pointer_qual_levels(global_var_t *gv,
                                           int pointer_qual_levels);
void psx_decl_set_gvar_pointee_elem_size(global_var_t *gv, int pointee_elem_size);
void psx_decl_set_gvar_ptr_array_pointee_bytes(global_var_t *gv,
                                               int ptr_array_pointee_bytes);
void psx_decl_set_gvar_pointee_fp_kind(global_var_t *gv, tk_float_kind_t fp_kind);
void psx_decl_set_gvar_pointee_scalar_flags(global_var_t *gv,
                                            int is_unsigned, int is_bool);
void psx_decl_set_gvar_bool(global_var_t *gv, int is_bool, int elem_is_bool);
void psx_decl_set_gvar_long_double(global_var_t *gv, int is_long_double);
void psx_decl_set_gvar_qualifiers(global_var_t *gv,
                                  int is_const_qualified,
                                  int is_volatile_qualified);
void psx_decl_set_gvar_funcptr_signature(global_var_t *gv,
                                         const psx_decl_funcptr_sig_t *sig);
void psx_decl_set_current_funcname(char *name, int len);
void psx_decl_get_current_funcname(char **out_name, int *out_len);

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
                                                 const psx_type_spec_result_t *type_spec,
                                                 const int *td_array_dims, int td_array_dim_count,
                                                 int td_array_elem_size, int td_is_array,
                                                 int td_is_long_double, int base_pointer_levels,
                                                 psx_decl_funcptr_sig_t base_funcptr_sig,
                                                 token_t *typespec_start,
                                                 int decl_base_is_void,
                                                 int decl_base_is_bool);
node_t *psx_decl_parse_initializer_for_var(lvar_t *var, int is_pointer);
/* AST 上の式ノードを定数畳み込みして long long を返す。
 * ok=1 を返した時のみ結果は有効。ND_NUM, ND_ADD/SUB/..., 三項などを扱う。 */
long long psx_decl_eval_const_int(node_t *n, int *ok);
int psx_resolve_global_addr_init(node_t *e, char **sym, int *sym_len, long long *off);

// `{ ... }` のトップレベル要素数を先読みで返す。curtok は変更しない。
// 推定不可なら 0。指定初期化子 `[N]=` で位置がジャンプする場合は最大位置+1 を返す。
long long psx_decl_count_brace_init_elements(token_t *brace_tok);

/* parser.c の brace init flat パーサ。global_var_t の init_values[] /
 * init_value_symbols[] / init_value_symbol_lens[] / init_fvalues[] を埋める。
 * static local 配列の lowering (decl.c) からも再利用する。 */
void psx_parse_global_brace_init_flat(global_var_t *gv, int *cap, int start_idx);
void psx_decl_finalize_gvar_inferred_array_size(global_var_t *gv, int *cap);

void psx_decl_record_lvar_usage_in_region(lvar_t *var, psx_lvar_usage_kind_t kind,
                                          psx_lvar_usage_region_t *region);

#endif
