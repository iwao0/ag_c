#ifndef PARSER_DECL_H
#define PARSER_DECL_H

/* decl.h は AST node 型 (node_t) と シンボルテーブル (global_var_t) の
 * 両方を使う。Phase C1-2: 両ヘッダを明示的に include する。 */
#include "ast.h"
#include "core.h"
#include "lvar_public.h"
#include "symtab.h"
#include "../semantic/local_type_state.h"

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
  int deref_size;
  int outer_stride;
  int mid_stride;
  int extra_strides[5];
  unsigned char extra_strides_count;
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
  unsigned int is_array : 1;
  unsigned int is_vla : 1;            // 1: 可変長配列 (VLA) - offsetはベースポインタスロット
  unsigned int is_byref_param : 1;    // 1: >16バイト構造体の値渡し仮引数 - フレームスロットはポインタ(8B)、elemは実際の構造体サイズ
  unsigned int is_used : 1;           // 1: 参照された
  unsigned int is_unevaluated_used : 1; // 1: sizeof 等の未評価オペランドで参照された
  unsigned int is_address_taken : 1;  // 1: & 等でアドレスだけ参照された
  unsigned int suppress_unreachable_warnings : 1; // 1: 到達不能文で宣言され W3003/W3004 を抑制
  unsigned int is_param : 1;          // 1: 関数パラメータ
  unsigned int is_initialized : 1;   // 1: 初期化済み（宣言初期化子または代入）
  // 1: `static` 付きで宣言されたローカル変数。フレーム上には配置されず、
  //    static_global_name のグローバル変数に lowering される。
  //    識別子解決時に ND_LVAR ではなく ND_GVAR を返すフラグ。
  unsigned int is_static_local : 1;
  char *static_global_name;
  int static_global_name_len;
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

/* lvar_t / global_var_t の tag 4 フィールド (kind/name/len/is_tag_pointer)
 * を 1 行で設定するヘルパ (Phase A2 リファクタリング)。
 * decl.c / parser.c で 4 行のパターンが 9 箇所重複していたのを集約する。 */

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
void psx_decl_funcptr_sig_promote_return_to_funcptr(
    psx_decl_funcptr_sig_t *sig, const psx_funcptr_signature_t *returned_sig);
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
void psx_decl_set_gvar_type_size(global_var_t *gv, int type_size);
void psx_decl_set_gvar_decl_type(global_var_t *gv,
                                 const psx_type_t *decl_type);
void psx_decl_set_gvar_type_sig(global_var_t *gv, char *type_sig);
void psx_decl_set_current_funcname(char *name, int len);
void psx_decl_get_current_funcname(char **out_name, int *out_len);

node_t *psx_decl_parse_declaration(void);
node_t *psx_decl_parse_initializer_for_var(lvar_t *var, int is_pointer);
node_t *psx_decl_bind_initializer_for_var(
    lvar_t *var, int is_pointer, node_t *initializer,
    psx_decl_init_kind_t initializer_kind, token_t *init_tok);


void psx_decl_record_lvar_usage_in_region(lvar_t *var, psx_lvar_usage_kind_t kind,
                                          psx_lvar_usage_region_t *region);

#endif
