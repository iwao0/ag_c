#include "decl.h"
#include "arena.h"
#include "core.h"
#include "diag.h"
#include "expr.h"
#include "node_utils.h"
#include "ret_pointee_array.h"
#include "semantic_ctx.h"
#include "config_runtime.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include "../tokenizer/escape.h"
#include "../tokenizer/literals.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static lvar_t *locals;       // 現在のスコープで見えるローカル変数リスト
static lvar_t *all_locals;   // 全スコープのローカル変数リスト（未使用チェック用）
static int locals_offset;
/* _Generic 用: 局所変数の型を正規化トークン文字列で記録する副テーブル (詳細は下の
 * psx_serialize_decl_type_tokens 付近)。宣言の型開始トークンと、名前→シグネチャ表。 */
static token_t *g_decl_typespec_start = NULL;
#define VAR_TYPESIG_MAX 256
/* 局所変数: 関数ごとにリセットする。 */
static struct {
  char *name;
  int len;
  char *sig;
} g_var_typesigs[VAR_TYPESIG_MAX];
static int g_var_typesig_count = 0;
/* グローバル変数: 翻訳単位を通じて永続 (関数境界でリセットしない)。 */
static struct {
  char *name;
  int len;
  char *sig;
} g_global_typesigs[VAR_TYPESIG_MAX];
static int g_global_typesig_count = 0;
static inline token_t *curtok(void) { return tk_get_current_token(); }
static inline void set_curtok(token_t *tok) { tk_set_current_token(tok); }

// ブロックスコープのローカル変数リスト保存スタック
#define LVAR_SCOPE_STACK_MAX 256
static lvar_t *lvar_scope_stack[LVAR_SCOPE_STACK_MAX];
static int lvar_scope_depth;

/* ローカル変数の名前ハッシュ索引。`locals` 連結リストの線形走査
 * (psx_decl_find_lvar) は識別子参照ごとに呼ばれ O(N^2) になるため、名前を
 * バケットへハッシュして O(1) 化する。各 bucket は MRU 順 (add で先頭挿入)
 * なので最初の名前一致が最も内側のスコープ = 正しいシャドーイング。スコープを
 * 抜けるときに脱落分を bucket から除去するので、bucket には可視なローカルだけが残る。 */
#define LVAR_HASH_BUCKETS 256u
static lvar_t *lvars_by_bucket[LVAR_HASH_BUCKETS];
/* オフセット → lvar 逆引き索引 (psx_decl_find_lvar_by_offset 用)。node_lvar_t は
 * offset だけを持つので lvar 本体を引き戻すのに使う。all_locals (関数内全ローカル) を
 * 線形走査していたため代入のたびに O(N) かかり O(N^2) になっていた。offset は関数内で
 * 一意 (locals_offset は単調増加) かつ all_locals はスコープ離脱で減らないので、
 * add で挿入・関数境界の reset でクリアするだけでよい (名前索引のような除去は不要)。 */
static lvar_t *lvars_by_offset[LVAR_HASH_BUCKETS];

static unsigned lvar_offset_hash(int offset) {
  return (((unsigned)offset) * 2654435761u) >> 24;  /* Knuth 乗算ハッシュの上位 8bit */
}

static void warn_unsupported_gnu_extension_name(const token_t *tok, const char *name);

/* スコープ一意連番。enter_scope ごとに採番し、同一スコープ内の重複宣言検出に使う
 * (同じ scope_seq を持つ変数が既にあれば C11 6.7p3 違反)。 */
static unsigned lvar_scope_seq_stack[LVAR_SCOPE_STACK_MAX];
static unsigned g_lvar_scope_seq;     // enter_scope ごとに ++
static unsigned cur_lvar_scope_seq;   // 現在スコープの連番

static unsigned lvar_name_hash(const char *name, int len) {
  unsigned h = 2166136261u;
  for (int i = 0; i < len; i++) h = (h ^ (unsigned char)name[i]) * 16777619u;
  return h & (LVAR_HASH_BUCKETS - 1u);
}

/* add 後に呼ぶ: スコープ連番を刻み、名前 bucket とオフセット bucket の先頭へ挿入する。 */
static void lvar_index_on_add(lvar_t *var) {
  var->scope_seq = cur_lvar_scope_seq;
  unsigned h = lvar_name_hash(var->name, var->len);
  var->next_hash = lvars_by_bucket[h];
  lvars_by_bucket[h] = var;
  unsigned oh = lvar_offset_hash(var->offset);
  var->next_offhash = lvars_by_offset[oh];
  lvars_by_offset[oh] = var;
}

/* スコープ離脱時に呼ぶ: bucket から var を取り除く (通常は bucket 先頭にいる)。 */
static void lvar_index_on_remove(lvar_t *var) {
  unsigned h = lvar_name_hash(var->name, var->len);
  lvar_t **pp = &lvars_by_bucket[h];
  while (*pp) {
    if (*pp == var) { *pp = var->next_hash; return; }
    pp = &(*pp)->next_hash;
  }
}
/* 集合体メンバ情報は semantic_ctx 側の統合 API (tag_member_info_t) を
 * そのまま再利用する (Phase A1 リファクタリング)。 */

static bool tag_find_member(lvar_t *var, char *name, int len, tag_member_info_t *out);
static bool tag_get_member_at(lvar_t *var, int ordinal, tag_member_info_t *out);
static bool tag_get_next_named_member(lvar_t *var, int *ordinal_inout,
                                      tag_member_info_t *out);
static node_t *parse_scalar_brace_initializer(void);
static node_t *parse_array_initializer(lvar_t *var);
static node_t *parse_struct_initializer(lvar_t *var);
static node_t *parse_union_initializer(lvar_t *var);
static node_t *parse_struct_copy_initializer(lvar_t *var);
static node_t *build_struct_copy_from_value(lvar_t *var, node_t *value);
static node_t *parse_struct_member_no_brace(lvar_t *nested);
static bool elision_consume_separator(void);
static node_t *new_struct_member_lvar(lvar_t *var, int member_offset, int member_type_size,
                                      token_kind_t member_tag_kind, char *member_tag_name,
                                      int member_tag_len, int member_is_tag_pointer);
static int parse_nonneg_const_expr_decl(const char *what);
static int resolve_copy_source_lvar(node_t *expr, node_t **out_prefix, node_lvar_t **out_src);
static int is_supported_scalar_store_size(int size);
static int is_compatible_tag_object_lvar(node_lvar_t *src, lvar_t *var);
static node_t *build_struct_copy_chain_from_source(lvar_t *dst, node_lvar_t *src);
static node_t *try_parse_array_member_copy_initializer(int dst_base_off, int elem_size, int array_len);
static node_t *try_parse_array_member_string_initializer(int dst_base_off, int elem_size, int array_len);
static string_lit_t *find_string_lit_by_label(char *label);
typedef struct {
  token_kind_t type_kind;
  int is_unsigned;
  int is_long_long;   // long long (_Generic で long と区別)
  int is_long_double; // long double (_Generic で double と区別)
  int is_plain_char;  // plain char (_Generic で signed/unsigned char と区別)
  int elem_size;
  tk_float_kind_t fp_kind;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int base_is_pointer;
  int is_const_qualified;
  int is_volatile_qualified;
  int td_pointee_const;
  int td_pointee_volatile;
  int is_extern_decl;
  // typedef が配列型 (`typedef int M[2][3][4]`) のとき、その各次元 (dims[0] が最外側)
  // と次元数。`M m;` 宣言で配列として lvar 登録するために宣言子側で参照する。
  int td_array_dims[8];
  int td_array_dim_count;
  /* typedef が配列型のときの 1 要素のバイト数 (= sizeof_size / dims[0])。
   * `typedef BinOp OpArr3[3]; OpArr3 *pa` のような「typedef 配列へのポインタ + 要素が
   * ポインタ」のケースで、宣言子側が elem_size を 8 (関数ポインタ) に上書きするのに使う。
   * 0 = 配列 typedef でない or 取れない (caller は elem_size を使う)。 */
  int td_array_elem_size;
  unsigned short td_funcptr_param_fp_mask;
  unsigned short td_funcptr_param_int_mask;
  unsigned char td_funcptr_ret_int_width;
  unsigned char td_funcptr_ret_is_void;
  unsigned char td_funcptr_ret_is_pointer;
  unsigned char td_funcptr_ret_is_complex;
  unsigned char td_is_variadic_funcptr;
  short td_funcptr_nargs_fixed;
} local_decl_spec_t;
typedef struct {
  int arr_total;
  int is_array;
  int has_incomplete_array;
  // 最初の `[N]` の N。複数次元 `typedef int M[3][4]` で
  // 仮引数 `M *p` の mid_stride を求める際に使う (= sizeof_size / first_dim)。
  // 不完全配列や 1 次元のみのときは 0。
  int first_dim;
  // 多次元 typedef 用: 解析した各次元のサイズを左から順に保持。
  // dim_count = 解析した `[N]` の個数。上限 8。
  int dims[8];
  int dim_count;
} decl_array_suffix_t;
static int parse_local_decl_spec(local_decl_spec_t *out);
static int parse_local_decl_spec_from_typedef(local_decl_spec_t *out);
static int parse_local_decl_spec_from_builtin(local_decl_spec_t *out);
static node_t *parse_typedef_declaration_local(void);
static void parse_local_extern_declarator_list(local_decl_spec_t *ds);
static void register_local_extern_decl(token_ident_t *name, int is_ptr, decl_array_suffix_t arr,
                                       int elem_size, tk_float_kind_t fp_kind,
                                       token_kind_t tag_kind, char *tag_name, int tag_len,
                                       int is_unsigned);
static void resolve_local_typedef_decl_spec(token_kind_t *base_kind, int *elem_size,
                                            tk_float_kind_t *fp_kind,
                                            token_kind_t *tag_kind, char **tag_name, int *tag_len,
                                            int *is_pointer_base, int *is_long_double_base);
static void define_local_typedef_from_declarator(token_ident_t *name, int is_ptr, int paren_array_mul,
                                                 token_kind_t base_kind, int elem_size,
                                                 tk_float_kind_t fp_kind,
                                                 token_kind_t tag_kind, char *tag_name, int tag_len,
                                                 int td_pointee_const, int td_pointee_volatile,
                                                 int td_is_unsigned, int td_is_long_double,
                                                 int td_base_is_void,
                                                 int decl_added_pointer);
static void parse_local_typedef_declarator_list(token_kind_t base_kind, int elem_size,
                                                tk_float_kind_t fp_kind,
                                                token_kind_t tag_kind, char *tag_name, int tag_len,
                                                int is_pointer_base,
                                                int td_pointee_const, int td_pointee_volatile,
                                                int td_is_unsigned, int td_is_long_double,
                                                int td_base_is_void);
static global_var_t *find_global_var_decl(char *name, int len);
static tk_float_kind_t fp_kind_for_type_kind(token_kind_t type_kind);
static void resolve_builtin_type_local(token_kind_t type_kind, int *out_elem_size,
                                       tk_float_kind_t *out_fp_kind);
static void init_local_decl_spec(local_decl_spec_t *out);
static void take_local_decl_prefix_flags(local_decl_spec_t *out);
static void adjust_local_decl_spec_from_typedef(local_decl_spec_t *out, token_kind_t base_kind);
static void resolve_typedef_name_ref_local(token_kind_t *out_base_kind, int *out_elem_size,
                                           tk_float_kind_t *out_fp_kind,
                                           token_kind_t *out_tag_kind, char **out_tag_name,
                                           int *out_tag_len, int *out_base_is_pointer,
                                           int *out_pointee_const, int *out_pointee_volatile,
                                           int *out_is_unsigned, int *out_is_long_double);

static tk_float_kind_t fp_kind_for_type_kind(token_kind_t type_kind) {
  if (type_kind == TK_FLOAT) return TK_FLOAT_KIND_FLOAT;
  if (type_kind == TK_DOUBLE) return TK_FLOAT_KIND_DOUBLE;
  return TK_FLOAT_KIND_NONE;
}

static void resolve_builtin_type_local(token_kind_t type_kind, int *out_elem_size,
                                       tk_float_kind_t *out_fp_kind) {
  psx_ctx_get_type_info(type_kind, NULL, out_elem_size);
  if (out_fp_kind) *out_fp_kind = fp_kind_for_type_kind(type_kind);
}

static void init_local_decl_spec(local_decl_spec_t *out) {
  memset(out, 0, sizeof(*out));
  out->elem_size = 8;
  out->fp_kind = TK_FLOAT_KIND_NONE;
  out->tag_kind = TK_EOF;
}

static void take_local_decl_prefix_flags(local_decl_spec_t *out) {
  psx_take_type_qualifiers(&out->is_const_qualified, &out->is_volatile_qualified);
  psx_take_extern_flag(&out->is_extern_decl);
}

static void adjust_local_decl_spec_from_typedef(local_decl_spec_t *out, token_kind_t base_kind) {
  if ((out->tag_kind == TK_STRUCT || out->tag_kind == TK_UNION) &&
      out->tag_name && out->tag_len > 0 &&
      psx_ctx_has_tag_type(out->tag_kind, out->tag_name, out->tag_len)) {
    int tag_sz = psx_ctx_get_tag_size(out->tag_kind, out->tag_name, out->tag_len);
    if (tag_sz > 0) out->elem_size = tag_sz;
  }
  out->type_kind = base_kind;
  /* typedef 由来の unsigned 性 (例: `typedef unsigned char uint8_t` は
   * base_kind=TK_CHAR だが unsigned) を保持する。base_kind が TK_UNSIGNED の
   * ときも unsigned。上書きで捨てると 1byte ロードが ldrsb (符号付き) になり
   * uint8_t の 200 が -56 に化ける。 */
  out->is_unsigned = out->is_unsigned || (base_kind == TK_UNSIGNED);
}

static void resolve_typedef_name_ref_local(token_kind_t *out_base_kind, int *out_elem_size,
                                           tk_float_kind_t *out_fp_kind,
                                           token_kind_t *out_tag_kind, char **out_tag_name,
                                           int *out_tag_len, int *out_base_is_pointer,
                                           int *out_pointee_const, int *out_pointee_volatile,
                                           int *out_is_unsigned, int *out_is_long_double) {
  token_ident_t *id = (token_ident_t *)curtok();
  psx_typedef_info_t _ti;
  if (psx_ctx_find_typedef_name(id->str, id->len, &_ti)) {
    if (out_base_kind) *out_base_kind = _ti.base_kind;
    if (out_elem_size) *out_elem_size = _ti.elem_size;
    if (out_fp_kind) *out_fp_kind = _ti.fp_kind;
    if (out_tag_kind) *out_tag_kind = _ti.tag_kind;
    if (out_tag_name) *out_tag_name = _ti.tag_name;
    if (out_tag_len) *out_tag_len = _ti.tag_len;
    if (out_base_is_pointer) *out_base_is_pointer = _ti.is_pointer;
    if (out_pointee_const) *out_pointee_const = _ti.pointee_const_qualified;
    if (out_pointee_volatile) *out_pointee_volatile = _ti.pointee_volatile_qualified;
    if (out_is_unsigned) *out_is_unsigned = _ti.is_unsigned;
    if (out_is_long_double) *out_is_long_double = _ti.is_long_double;
  }
  set_curtok(curtok()->next);
}

// typedef 配列の dims[] と次元数を取得する補助。dims が無い場合は dim_count=0。
static void resolve_typedef_array_dims(token_ident_t *id, int *out_dims, int *out_dim_count) {
  int dim_count = 0;
  psx_typedef_info_t _ti;
  if (psx_ctx_find_typedef_name(id->str, id->len, &_ti)) {
    dim_count = _ti.array_dim_count;
    if (out_dims) for (int i = 0; i < dim_count && i < 8; i++) out_dims[i] = _ti.array_dims[i];
  }
  /* dim_count>0 は配列 typedef (is_array=1) と pointer-to-array typedef
   * (`typedef int (*PA)[3]`、is_array=0 でポインティ extent を dims に格納) の両方で
   * 立つ。後者も dims に extent を持つので is_array では絞らず dim_count で判定する。 */
  if (out_dim_count) *out_dim_count = (dim_count > 0) ? dim_count : 0;
}

/* `typedef T A[N]; A *pa` (配列 typedef へのポインタ) で要素サイズを正しく取るための
 * 補助。typedef が配列型 (is_array=1) のとき、要素 1 個のバイト数 = sizeof_size / dims[0]
 * を返す。配列でなければ 0 (= caller は elem_size を使う)。
 * pointer-to-array typedef (`typedef int (*PA)[3]`、is_array=0) は dims を持つが要素サイズ
 * 計算には使わない (PA p の要素は int で elem_size=4 のまま)。 */
static int resolve_typedef_array_element_size(token_ident_t *id) {
  psx_typedef_info_t _ti;
  if (!psx_ctx_find_typedef_name(id->str, id->len, &_ti)) return 0;
  if (!_ti.is_array) return 0;
  if (_ti.array_dim_count < 1 || _ti.array_dims[0] <= 0) return 0;
  if (_ti.sizeof_size <= 0) return 0;
  return _ti.sizeof_size / _ti.array_dims[0];
}

static long long eval_const_expr_decl(node_t *n, int *ok);

/* parser.c で定義された forward 宣言。グローバル初期化のシンボル+offset 解決を再利用する。 */
extern int psx_resolve_global_addr_init(node_t *e, char **sym, int *sym_len, long long *off);

long long psx_decl_eval_const_int(node_t *n, int *ok) {
  return eval_const_expr_decl(n, ok);
}

static long long eval_const_expr_decl(node_t *n, int *ok) {
  if (!n) { *ok = 0; return 0; }
  switch (n->kind) {
  case ND_NUM:
    return ((node_num_t *)n)->val;
  case ND_GVAR: {
    /* グローバル変数参照: 整数定数で初期化された const gvar は ICE として
     * 折り畳む (例: `const int A = 5; const int C = A * 7;`)。
     * 厳密な C 標準では ICE に該当しないが ag_c は寛容に扱う。
     * シンボルアドレス初期 / 浮動小数 / 配列は除外。 */
    node_gvar_t *gv = (node_gvar_t *)n;
    for (global_var_t *g = psx_find_global_var(gv->name, gv->name_len); g; g = NULL) {
      if (g->name_len == gv->name_len &&
          memcmp(g->name, gv->name, (size_t)g->name_len) == 0) {
        if (g->has_init && !g->init_symbol && !g->init_values && !g->init_fvalues &&
            g->fp_kind == TK_FLOAT_KIND_NONE && !g->is_array) {
          return g->init_val;
        }
        break;
      }
    }
    *ok = 0;
    return 0;
  }
  case ND_COMMA:
    (void)eval_const_expr_decl(n->lhs, ok);
    if (!*ok) return 0;
    return eval_const_expr_decl(n->rhs, ok);
  case ND_TERNARY: {
    long long c = eval_const_expr_decl(n->lhs, ok);
    if (!*ok) return 0;
    node_t *then_expr = n->rhs;
    node_t *else_expr = ((node_ctrl_t *)n)->els;
    return c ? eval_const_expr_decl(then_expr, ok) : eval_const_expr_decl(else_expr, ok);
  }
  case ND_ADD: case ND_SUB: case ND_MUL: case ND_DIV: case ND_MOD:
  case ND_SHL: case ND_SHR:
  case ND_BITAND: case ND_BITXOR: case ND_BITOR:
  case ND_EQ: case ND_NE: case ND_LT: case ND_LE:
  case ND_LOGAND: case ND_LOGOR:
    break;
  default:
    *ok = 0; return 0;
  }
  /* `&g_arr[3] - &g_arr[1]` のような「同一シンボル上のポインタ減算」は ICE。
   * 両辺をそれぞれ (sym, off) に解決し、同 sym なら byte 差分を返す。
   * 上位レイヤ (`add()` で ND_DIV(ND_SUB, sizeof) にラップ) が要素単位に変換する。
   * 修正前は ND_SUB の左右 (= ND_ADD/ND_ADDR) が switch にないため eval が失敗し、
   * `long g_diff = &g_arr[3] - &g_arr[1];` がグローバル init を `.comm` (0) に落としていた。 */
  if (n->kind == ND_SUB) {
    char *lsym = NULL, *rsym = NULL;
    int lsym_len = 0, rsym_len = 0;
    long long loff = 0, roff = 0;
    if (psx_resolve_global_addr_init(n->lhs, &lsym, &lsym_len, &loff) &&
        psx_resolve_global_addr_init(n->rhs, &rsym, &rsym_len, &roff) &&
        lsym && rsym && lsym_len == rsym_len &&
        (lsym_len == -1 ? lsym == rsym
                        : (lsym_len > 0 && memcmp(lsym, rsym, (size_t)lsym_len) == 0))) {
      return loff - roff;
    }
  }
  // 二項演算共通: 左→右の順で評価し、op を適用。
  long long l = eval_const_expr_decl(n->lhs, ok);
  if (!*ok) return 0;
  long long r = eval_const_expr_decl(n->rhs, ok);
  switch (n->kind) {
  case ND_ADD:    return l + r;
  case ND_SUB:    return l - r;
  case ND_MUL:    return l * r;
  case ND_DIV:    return l / r;
  case ND_MOD:    return l % r;
  case ND_SHL:    return l << r;
  case ND_SHR:    return l >> r;
  case ND_BITAND: return l & r;
  case ND_BITXOR: return l ^ r;
  case ND_BITOR:  return l | r;
  case ND_EQ:     return l == r;
  case ND_NE:     return l != r;
  case ND_LT:     return l < r;
  case ND_LE:     return l <= r;
  case ND_LOGAND: return (l && r) ? 1 : 0;
  case ND_LOGOR:  return (l || r) ? 1 : 0;
  default:        *ok = 0; return 0;
  }
}

static void skip_ptr_qualifiers_decl(int *is_const_qualified, int *is_volatile_qualified) {
  while (curtok()->kind == TK_CONST || curtok()->kind == TK_VOLATILE || curtok()->kind == TK_RESTRICT) {
    if (curtok()->kind == TK_CONST && is_const_qualified) *is_const_qualified = 1;
    if (curtok()->kind == TK_VOLATILE && is_volatile_qualified) *is_volatile_qualified = 1;
    set_curtok(curtok()->next);
  }
}

static void consume_pointer_chain_decl(int *is_pointer,
                                       unsigned int *const_mask, unsigned int *volatile_mask,
                                       int *levels) {
  while (tk_consume('*')) {
    *is_pointer = 1;
    int cur_const = 0;
    int cur_volatile = 0;
    skip_ptr_qualifiers_decl(&cur_const, &cur_volatile);
    if (levels && const_mask && volatile_mask) {
      int lv = *levels;
      if (lv < 32) {
        if (cur_const) *const_mask |= (1u << lv);
        if (cur_volatile) *volatile_mask |= (1u << lv);
      }
      *levels = lv + 1;
    }
  }
}

// 配列サイズ式をパースし定数評価する。ok=0 なら VLA (可変長配列) を示す。
static long long parse_array_size_expr_decl(node_t **out_node, int *out_ok) {
  node_t *n = psx_expr_assign();
  if (out_node) *out_node = n;
  int ok = 1;
  long long v = eval_const_expr_decl(n, &ok);
  if (out_ok) *out_ok = ok;
  if (ok && v < 0) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
  } else if (ok && v == 0) {
    warn_unsupported_gnu_extension_name(curtok(), "zero-length array");
  }
  return v;
}

static int parse_array_size_constexpr_decl(void) {
  int ok = 1;
  long long v = parse_array_size_expr_decl(NULL, &ok);
  if (!ok) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_ARRAY_SIZE_CONSTEXPR_REQUIRED));
  }
  return (int)v;
}

static int parse_array_size_optional_constexpr_decl(int *out_has_size) {
  if (curtok()->kind == TK_RBRACKET) {
    if (out_has_size) *out_has_size = 0;
    return 0;
  }
  if (out_has_size) *out_has_size = 1;
  return parse_array_size_constexpr_decl();
}

static void parse_decl_skip_constexpr_array_suffixes(void) {
  while (tk_consume('[')) {
    (void)parse_array_size_constexpr_decl();
    tk_expect(']');
  }
}

// 多次元配列の trailing dim 列 `[N2][N3][N4]...` を読み、各 dim を out_dims に
// 格納し、全 trailing dim の積を返す（最大 max_dims 個まで、それ以降は積に
// だけ寄与）。dim が無いときは out_dims=0 のまま 1 を返す。
static int parse_decl_constexpr_array_suffix_product_n(int *out_dims, int max_dims, int *out_count) {
  int mul = 1;
  int count = 0;
  while (tk_consume('[')) {
    int dim = parse_array_size_constexpr_decl();
    tk_expect(']');
    if (count < max_dims) out_dims[count] = dim;
    count++;
    if (dim > 0) mul *= dim;
  }
  if (out_count) *out_count = count;
  return mul;
}

/* `T (*p)[N][M]...` (配列へのポインタ) 局所宣言子で、paren の後ろの `[N][M]` の
 * 先頭次元 (N) と次元数を捕捉する。outer_stride に加え mid_stride を計算するのに使う。
 * consume_decl_name_recursive が paren-array を消費するときにセットする。 */
static int g_paren_array_first_dim = 0;
static int g_paren_array_second_dim = 0;
static int g_paren_array_dim_count = 0;
/* 関数ポインタ配列 `int (*t[2][2])(void)` の括弧内 `[N][M]` 個別次元。inner_array_mul は
 * 積しか持たないため、2 次元以上の funcptr 配列でストライドを立てられるよう dims を保持する。
 * consume_decl_name_recursive が括弧内 `[N]` 列を消費するときにセット。宣言子ごとにリセット。 */
static int g_inner_array_dims[8] = {0};
static int g_inner_array_dim_count = 0;
/* `int (*p)[m]` (配列へのポインタの VLA 形) の先頭次元のランタイム式。非 NULL なら
 * registration が pointer-to-VLA として登録し、行ストライドスロットを確保する。 */
static node_t *g_paren_array_vla_dim = NULL;
/* 宣言子の trailing 部に関数シグネチャ `(args...)` があれば 1。
 * 「関数ポインタ配列へのポインタ」(`int (*(*pa)[N])(args)` / `BinOp (*pa)[N]`) の登録で
 * 要素サイズを「関数ポインタ = 8」と判定するのに使う。consume_decl_name_recursive が
 * skip_func_params の呼び出し時に立てる。宣言子ごとにリセット。 */
static int g_decl_trailing_func_suffix = 0;
/* 宣言子に paren グループ `(*...)` があれば 1 (`**pp` のように括弧無しは 0)。
 * pointer-to-function (`(* (*p)(args))(ret-args)`) と pointer-to-funcptr (`**pp`) を
 * 区別して pointer_qual_levels を補正するのに使う。 */
static int g_decl_had_paren_group = 0;
/* trailing `()` の個数 (pointer-to-function が戻り funcptr を持つとき 2 以上)。 */
static int g_decl_func_suffix_count = 0;
/* 基底 typedef が配列型 (`typedef BinOp OpArr3[3]`) のとき、要素 1 個のバイト数。
 * `OpArr3 *pa` (typedef 配列型へのポインタ + 要素がポインタ) で要素サイズを 8 と判定する
 * のに使う。pointer-to-array typedef (`typedef int (*PA)[3]`) では 0 のまま (PA p の要素は
 * int=4 で elem_size を使うため、上書きしない)。parse_local_decl_spec_from_typedef がセット。 */
static int g_decl_td_array_elem_size = 0;
static int g_decl_td_is_array = 0;
static int g_decl_td_is_long_double = 0;
/* 基底型がポインタ typedef のときの段数 (`typedef int **PP; PP p;` で 2)。
 * parse_local_decl_spec_from_typedef が設定し、psx_decl_parse_declaration_after_type_ex が
 * 読んで pql / total_pointer_levels に反映する。非ポインタ基底や直書きでは 0。 */
static int g_decl_base_pointer_levels = 0;
/* 基底 typedef が関数ポインタ型のときの仮引数 fp マスク。
 * `typedef double (*Op)(double); Op op; op(3)` で int 実引数を double へ昇格するため。 */
static unsigned short g_decl_base_funcptr_param_fp_mask = 0;
static unsigned short g_decl_base_funcptr_param_int_mask = 0;
static unsigned char g_decl_base_funcptr_ret_int_width = 0;
static psx_ret_pointee_array_t g_decl_base_funcptr_ret_pointee_array = {0};
static int g_decl_base_funcptr_ret_is_void = 0;
static int g_decl_base_funcptr_ret_is_pointer = 0;
static int g_decl_base_funcptr_ret_is_complex = 0;
static int g_decl_base_is_variadic_funcptr = 0;
static short g_decl_base_funcptr_nargs_fixed = 0;

unsigned char psx_funcptr_ret_int_width_from_kind(token_kind_t kind, int is_pointer,
                                                  tk_float_kind_t fp_kind) {
  if (is_pointer || fp_kind != TK_FLOAT_KIND_NONE || kind == TK_VOID ||
      kind == TK_STRUCT || kind == TK_UNION || kind == TK_EOF) {
    return 0;
  }
  return psx_ctx_scalar_type_size(kind) >= 8 ? 8 : 4;
}

/* curtok から後続の `[...]` 列を peek し、いずれかの次元式が非定数 (= VLA 候補) なら 1 を返す。
 * 「定数」とは [...] 内が TK_NUM のみで構成されることを指す。TK_IDENT がある場合は変数または
 * enum 定数だが、enum 定数のときは psx_ctx_find_enum_const で識別して定数扱いする。それ以外
 * (識別子参照、関数呼び出し、その他複雑な式) は VLA と判定する。
 * 主な用途: 第 1 dim が const、後の dim が VLA の混在配列 `int t[2][n][4]` を VLA 経路へ
 * redirect する判定に使う。curtok は変更しない。 */
static int decl_peek_trailing_array_dims_have_vla(void) {
  token_t *t = curtok();
  while (t && t->kind == TK_LBRACKET) {
    t = t->next;
    int depth = 1;
    int has_non_const = 0;
    while (t && depth > 0) {
      if (t->kind == TK_LBRACKET) { depth++; }
      else if (t->kind == TK_RBRACKET) {
        depth--;
        if (depth == 0) break;
      } else if (t->kind == TK_IDENT) {
        /* enum 定数なら const、それ以外は VLA とみなす。 */
        token_ident_t *id = (token_ident_t *)t;
        long long ev;
        if (!psx_ctx_find_enum_const(id->str, id->len, &ev)) {
          has_non_const = 1;
        }
      }
      t = t->next;
    }
    if (has_non_const) return 1;
    if (t && t->kind == TK_RBRACKET) t = t->next;
  }
  return 0;
}

/* out_vla_dim 非 NULL のとき、非定数次元 (VLA, `(*p)[m]`) を即エラーにせず先頭の 1 つを
 * *out_vla_dim に式として捕捉する (arr_total には寄与させない)。NULL なら従来どおり定数必須。 */
static decl_array_suffix_t parse_decl_array_suffixes_ex(int base_mul, node_t **out_vla_dim) {
  decl_array_suffix_t out = {0};
  out.arr_total = (base_mul > 0) ? base_mul : 1;
  out.is_array = (base_mul > 0);
  out.has_incomplete_array = 0;
  out.first_dim = 0;
  out.dim_count = 0;
  int dim_count = 0;
  while (tk_consume('[')) {
    int has_size = 0;
    int n = 0;
    int is_vla_dim = 0;
    if (out_vla_dim && curtok()->kind != TK_RBRACKET) {
      /* VLA 許容: 式を評価し、非定数なら捕捉。 */
      node_t *dim_node = NULL;
      int ok = 1;
      long long v = parse_array_size_expr_decl(&dim_node, &ok);
      if (!ok) {
        if (*out_vla_dim == NULL) *out_vla_dim = dim_node;  /* 先頭 VLA 次元のみ */
        is_vla_dim = 1;
        has_size = 1;
      } else {
        has_size = 1;
        n = (int)v;
      }
    } else {
      n = parse_array_size_optional_constexpr_decl(&has_size);
    }
    if (!has_size) {
      out.has_incomplete_array = 1;
    } else if (!is_vla_dim) {
      out.arr_total *= n;
      if (dim_count == 0) out.first_dim = n;
    }
    if (dim_count < 8) {
      out.dims[dim_count] = (has_size && !is_vla_dim) ? n : 0;
    }
    dim_count++;
    out.is_array = 1;
    tk_expect(']');
  }
  if (dim_count > 8) dim_count = 8;
  out.dim_count = dim_count;
  return out;
}

static decl_array_suffix_t parse_decl_array_suffixes(int base_mul) {
  return parse_decl_array_suffixes_ex(base_mul, NULL);
}


// 波括弧初期化子 `{ ... }` のトップレベル要素数を数えるトークン先読みヘルパ。
// `brace_tok` は `{` を指している必要がある。curtok は変更しない。
// 指定初期化子 `[N]=` で位置がジャンプする場合は、最大位置+1 を返す。
// 推定不可（空、複雑な指定子、トークン不整合）なら 0。
long long psx_decl_count_brace_init_elements(token_t *brace_tok) {
  if (!brace_tok || brace_tok->kind != TK_LBRACE) return 0;
  token_t *t = brace_tok->next; // skip '{'
  if (t && t->kind == TK_RBRACE) return 0; // 空 `{}` は推定不可

  long long idx = 0;
  long long max_seen = -1;
  int depth = 0;
  bool seen_content = false;
  while (t) {
    if (depth == 0) {
      if (t->kind == TK_RBRACE) {
        if (seen_content && idx > max_seen) max_seen = idx;
        break;
      }
      if (t->kind == TK_COMMA) {
        if (seen_content) {
          if (idx > max_seen) max_seen = idx;
          idx++;
          seen_content = false;
        }
        t = t->next;
        continue;
      }
      if (!seen_content && t->kind == TK_LBRACKET) {
        // 指定初期化子 `[N]=...`
        t = t->next;
        if (t && t->kind == TK_NUM && tk_as_num(t)->num_kind == TK_NUM_KIND_INT) {
          idx = (long long)tk_as_num_int(t)->uval;
          t = t->next;
        } else {
          return 0; // 複雑な式は未対応
        }
        if (!t || t->kind != TK_RBRACKET) return 0;
        t = t->next;
        if (t && t->kind == TK_ASSIGN) t = t->next;
        continue;
      }
    }
    if (t->kind == TK_LBRACE || t->kind == TK_LPAREN || t->kind == TK_LBRACKET) {
      depth++;
    } else if (t->kind == TK_RBRACE || t->kind == TK_RPAREN || t->kind == TK_RBRACKET) {
      depth--;
      if (depth < 0) return 0; // 異常: 開きより閉じが多い
    }
    seen_content = true;
    t = t->next;
  }
  if (max_seen < 0) return 0;
  return max_seen + 1;
}

// `=` 直後 `{` の中身がもう一段の `{` で始まるか（ネスト初期化子）を判定する。
// 2D 推定 `int a[][N]={{...},{...}}` をフラット形式 `{1,2,3,...}` と区別するために使う。
// curtok は変更しない。
static bool init_first_element_is_brace(void) {
  token_t *t = curtok();
  if (!t || t->kind != TK_ASSIGN) return false;
  t = t->next;
  if (!t || t->kind != TK_LBRACE) return false;
  t = t->next;
  // 簡略化のため、先頭の指定初期化子 (`[N]=` `.name=`) はスキップせず、直接判定する。
  return t && t->kind == TK_LBRACE;
}

// 文字列リテラル列の合計内容長 + 1 (NUL) を返す。t は文字列開始トークン。
// 文字列の終端後のトークンが終端記号 (`}` または NULL ライク) でなければ
// 単純な文字列初期化と見なせないので 0 を返す。
/* C11 5.1.1.2: 文字列中のエスケープシーケンス (`\t` `\xNN` `\NNN` 等) は
 * 1 文字にデコードされるので、配列要素数は decode 後の文字数で数える。
 * 生バイト長 (raw len) を返してしまうと `char s[] = "\t"` で要素数が 3 (raw '\','t',NUL)
 * になり過剰確保 + 値もズレる。 */
static long long count_decoded_chars_in_string(const char *s, int len, int char_width) {
  long long n = 0;
  int i = 0;
  int cw = char_width > 0 ? char_width : 1;
  while (i < len) {
    uint32_t units[2];
    n += tk_next_string_code_units(s, len, &i, cw, units);  /* 幅に応じたコードユニット数 */
  }
  return n;
}

static long long count_char_init_from_string_seq(token_t *t, bool require_rbrace_terminator) {
  long long total = 0;
  while (t && t->kind == TK_STRING) {
    token_string_t *st = (token_string_t *)t;
    total += count_decoded_chars_in_string(st->str, st->len, (int)st->char_width);
    t = t->next;
  }
  if (require_rbrace_terminator) {
    if (!t || t->kind != TK_RBRACE) return 0;
  }
  return total + 1; // 終端 NUL
}

// `int a[] = ...` の `[]` で要素数を初期化子から推定するためのトークン先読みヘルパ。
// curtok は変更しない。`=` で始まる初期化子を想定し、推定できなければ 0 を返す。
// elem_size==1 の場合は文字列リテラル初期化子（NUL を含む長さ）にも対応する。
// 波括弧で囲まれた `{"..."}` 形式も同じく文字列初期化として扱う。
static long long infer_array_count_from_initializer_at(token_t *assign_tok, int elem_size) {
  token_t *t = assign_tok;
  if (!t || t->kind != TK_ASSIGN) return 0;
  t = t->next;
  if (!t) return 0;
  /* `T a[] = "..."` / `u"..."` (U/L 含む): 要素幅 (elem_size) が文字列のコード単位幅
   * (char/u8=1, u=2, U/L=4) と一致するとき、コード単位数 + NUL で要素数を推論する。 */
  if (t->kind == TK_STRING) {
    int cw = (int)((token_string_t *)t)->char_width;
    if (cw <= 0) cw = 1;
    if (elem_size == cw) return count_char_init_from_string_seq(t, false);
  }
  if (t->kind == TK_LBRACE) {
    token_t *inside = t->next;
    if (inside && inside->kind == TK_STRING) {
      int cw = (int)((token_string_t *)inside)->char_width;
      if (cw <= 0) cw = 1;
      if (elem_size == cw) {
        long long n = count_char_init_from_string_seq(inside, true);
        if (n > 0) return n;
      }
    }
  }
  return psx_decl_count_brace_init_elements(t);
}

static long long infer_array_count_from_initializer(int elem_size) {
  return infer_array_count_from_initializer_at(curtok(), elem_size);
}

static int parse_nonneg_const_expr_decl(const char *what) {
  node_t *n = psx_expr_assign();
  int ok = 1;
  long long v = eval_const_expr_decl(n, &ok);
  if (!ok) {
    psx_diag_ctx(curtok(), "decl", diag_message_for(DIAG_ERR_PARSER_NONNEG_CONSTEXPR_REQUIRED),
                 what);
  }
  if (v < 0) {
    psx_diag_ctx(curtok(), "decl", diag_message_for(DIAG_ERR_PARSER_NONNEG_VALUE_REQUIRED),
                 what);
  }
  return (int)v;
}

static node_t *parse_scalar_brace_initializer(void) {
  if (!tk_consume('{')) {
    return psx_expr_assign();
  }
  node_t *rhs = psx_expr_assign();
  if (tk_consume(',')) {
      if (!tk_consume('}')) {
      psx_diag_ctx(curtok(), "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_SCALAR_BRACE_SINGLE_ELEMENT_ONLY));
      }
    return rhs;
  }
  tk_expect('}');
  return rhs;
}

static node_t *new_array_elem_lvar(lvar_t *var, int idx) {
  int elem_off = var->offset + idx * var->elem_size;
  node_t *lvar = psx_node_new_lvar_typed(elem_off, var->elem_size);
  lvar->fp_kind = var->fp_kind;
  ((node_lvar_t *)lvar)->mem.tag_kind = var->tag_kind;
  ((node_lvar_t *)lvar)->mem.tag_name = var->tag_name;
  ((node_lvar_t *)lvar)->mem.tag_len = var->tag_len;
  ((node_lvar_t *)lvar)->mem.is_tag_pointer = var->is_tag_pointer;
  return lvar;
}

static node_t *new_array_elem_lvar_at(int base_offset, int elem_size, int idx) {
  node_t *lvar = psx_node_new_lvar_typed(base_offset + idx * elem_size, elem_size);
  return lvar;
}

static node_t *new_byte_lvar_at(int offset) {
  return psx_node_new_lvar_typed(offset, 1);
}

static node_t *build_byte_copy_chain(int dst_base_off, int src_base_off, int size, node_t *init_chain) {
  for (int i = 0; i < size; i++) {
    node_t *lhs = new_byte_lvar_at(dst_base_off + i);
    node_t *rhs = new_byte_lvar_at(src_base_off + i);
    node_mem_t *assign_node = psx_node_new_assign(lhs, rhs);
    assign_node->type_size = 1;
    node_t *init_node = (node_t *)assign_node;
    if (!init_chain) init_chain = init_node;
    else init_chain = psx_node_new_binary(ND_COMMA, init_chain, init_node);
  }
  return init_chain;
}

static int is_supported_scalar_store_size(int size) {
  return size == 1 || size == 2 || size == 4 || size == 8;
}

static int is_compatible_tag_object_lvar(node_lvar_t *src, lvar_t *var) {
  if (!src || !var) return 0;
  if (src->mem.is_tag_pointer || var->is_tag_pointer) return 0;
  if (src->mem.tag_kind != var->tag_kind) return 0;
  return src->mem.type_size > 0 && var->size > 0 && src->mem.type_size == var->size;
}

static int is_compatible_tag_object_mem(node_mem_t *src, lvar_t *var) {
  if (!src || !var) return 0;
  if (src->is_tag_pointer || var->is_tag_pointer) return 0;
  if (src->tag_kind != var->tag_kind) return 0;
  return src->type_size > 0 && var->size > 0 && src->type_size == var->size;
}

static node_t *build_struct_copy_chain_from_source(lvar_t *dst, node_lvar_t *src) {
  lvar_t src_var = {0};
  src_var.offset = src->offset;
  src_var.tag_kind = src->mem.tag_kind;
  src_var.tag_name = src->mem.tag_name;
  src_var.tag_len = src->mem.tag_len;
  src_var.is_tag_pointer = src->mem.is_tag_pointer;

  int member_count = psx_ctx_get_tag_member_count(dst->tag_kind, dst->tag_name, dst->tag_len);
  node_t *init_chain = NULL;
  for (int ordinal = 0; ordinal < member_count; ordinal++) {
    tag_member_info_t info = {0};
    bool found = psx_ctx_get_tag_member_info(dst->tag_kind, dst->tag_name, dst->tag_len, ordinal, &info);
    if (!found || info.len <= 0) continue;
    /* 配列メンバは type_size が要素サイズなので全体サイズへ換算する。
     * スカラ(非配列)で 1/2/4/8B のときだけ 1 ワード assign、それ以外
     * (配列メンバ・ネスト struct 等) はバイトコピーで全体を複製する。 */
    int full_size = (info.array_len > 0) ? info.type_size * info.array_len : info.type_size;
    if (info.array_len <= 0 && is_supported_scalar_store_size(info.type_size)) {
      node_t *lhs = new_struct_member_lvar(dst, info.offset, info.type_size,
                                           info.tag_kind, info.tag_name, info.tag_len, info.is_tag_pointer);
      node_t *rhs_member = new_struct_member_lvar(&src_var, info.offset, info.type_size,
                                                  info.tag_kind, info.tag_name, info.tag_len, info.is_tag_pointer);
      node_mem_t *assign_node = psx_node_new_assign(lhs, rhs_member);
      assign_node->type_size = info.type_size;
      node_t *init_node = (node_t *)assign_node;
      if (!init_chain) init_chain = init_node;
      else init_chain = psx_node_new_binary(ND_COMMA, init_chain, init_node);
      continue;
    }
    init_chain = build_byte_copy_chain(dst->offset + info.offset, src_var.offset + info.offset,
                                       full_size, init_chain);
  }
  return init_chain ? init_chain : psx_node_new_num(0);
}

static node_t *try_parse_array_member_copy_initializer(int dst_base_off, int elem_size, int array_len) {
  if (!curtok() || curtok()->kind != TK_IDENT) return NULL;
  token_ident_t *id = (token_ident_t *)curtok();
  lvar_t *src = psx_decl_find_lvar(id->str, id->len);
  if (!src || !src->is_array) return NULL;
  src->is_used = 1;
  if (src->elem_size != elem_size || src->size != elem_size * array_len) return NULL;
  if (!curtok()->next || (curtok()->next->kind != TK_COMMA && curtok()->next->kind != TK_RBRACE)) return NULL;

  (void)psx_expr_assign();
  node_t *init_chain = NULL;
  for (int idx = 0; idx < array_len; idx++) {
    node_t *lhs = new_array_elem_lvar_at(dst_base_off, elem_size, idx);
    int src_elem_off = src->offset + idx * src->elem_size;
    node_t *rhs = psx_node_new_lvar_typed(src_elem_off, elem_size);
    node_mem_t *assign_node = psx_node_new_assign(lhs, rhs);
    assign_node->type_size = elem_size;
    node_t *init_node = (node_t *)assign_node;
    if (!init_chain) init_chain = init_node;
    else init_chain = psx_node_new_binary(ND_COMMA, init_chain, init_node);
  }
  return init_chain ? init_chain : psx_node_new_num(0);
}

static node_t *try_parse_array_member_string_initializer(int dst_base_off, int elem_size, int array_len) {
  if (elem_size != 1) return NULL;
  if (!curtok() || curtok()->kind != TK_STRING) return NULL;

  node_t *rhs = psx_expr_assign();
  if (!rhs || rhs->kind != ND_STRING) return NULL;

  node_string_t *s = (node_string_t *)rhs;
  string_lit_t *lit = find_string_lit_by_label(s->string_label);
  if (!lit) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_STRING_INIT_RESOLVE_FAILED));
  }

  node_t *init_chain = NULL;
  int idx = 0;       /* 配列に書き込んだバイト数 */
  int src_pos = 0;   /* lit->str を走査するインデックス */
  while (src_pos < lit->len && idx < array_len) {
    /* C11 5.1.1.2: 文字列リテラル中のエスケープシーケンスは
     * 1 文字にデコードしてから配列に格納する。 */
    uint32_t cp = 0;
    if (lit->str[src_pos] == '\\') {
      if (!tk_parse_escape_value(lit->str, lit->len, &src_pos, &cp)) {
        cp = (unsigned char)lit->str[src_pos];
        src_pos++;
      }
    } else {
      cp = (unsigned char)lit->str[src_pos];
      src_pos++;
    }
    node_t *lhs = new_array_elem_lvar_at(dst_base_off, elem_size, idx);
    node_mem_t *assign_node = psx_node_new_assign(lhs, psx_node_new_num((unsigned char)cp));
    assign_node->type_size = elem_size;
    node_t *init_node = (node_t *)assign_node;
    if (!init_chain) init_chain = init_node;
    else init_chain = psx_node_new_binary(ND_COMMA, init_chain, init_node);
    idx++;
  }
  if (idx < array_len) {
    node_t *lhs = new_array_elem_lvar_at(dst_base_off, elem_size, idx);
    node_mem_t *assign_node = psx_node_new_assign(lhs, psx_node_new_num(0));
    assign_node->type_size = elem_size;
    node_t *init_node = (node_t *)assign_node;
    if (!init_chain) init_chain = init_node;
    else init_chain = psx_node_new_binary(ND_COMMA, init_chain, init_node);
  }
  return init_chain ? init_chain : psx_node_new_num(0);
}

static int resolve_copy_source_lvar(node_t *expr, node_t **out_prefix, node_lvar_t **out_src) {
  node_t *prefix = NULL;
  node_t *value = expr;
  while (value && value->kind == ND_COMMA) {
    if (!prefix) prefix = value->lhs;
    else prefix = psx_node_new_binary(ND_COMMA, prefix, value->lhs);
    value = value->rhs;
  }
  if (!value || value->kind != ND_LVAR) return 0;
  if (out_prefix) *out_prefix = prefix;
  if (out_src) *out_src = (node_lvar_t *)value;
  return 1;
}

static string_lit_t *find_string_lit_by_label(char *label) {
  for (string_lit_t *lit = string_literals; lit; lit = lit->next) {
    if (strcmp(lit->label, label) == 0) return lit;
  }
  return NULL;
}

static node_t *append_to_init_chain(node_t *init_chain, node_t *init_node);
static node_t *parse_array_init_chunk(lvar_t *var, int *init_elem_count, bool *assigned, int array_len,
                                      int start_idx, const int *chunk_sizes, int depth);

// `var->arr[idx] = value` を表す ASSIGN ノードを構築する。
// type_size と fp_kind は var の要素型から複製する。
static node_t *build_array_elem_assign(lvar_t *var, int idx, node_t *value) {
  node_t *lhs = new_array_elem_lvar(var, idx);
  /* C11 6.3.1.2: `_Bool` 配列の要素初期化子は (value != 0) に正規化して 0/1 を
   * 格納する (スカラ `_Bool b = 42;` と同じ。配列初期化子では生値が入っていた
   * ため `_Bool f[]={5}` が 5 のままだった)。 */
  if (var->is_bool && value) {
    value = psx_node_new_binary(ND_NE, value, psx_node_new_num(0));
  }
  node_mem_t *assign_node = psx_node_new_assign(lhs, value);
  assign_node->type_size = var->elem_size;
  assign_node->base.fp_kind = var->fp_kind;
  return (node_t *)assign_node;
}

/* `struct P a[3] = {{1, 2}, {3, 4}, {5, 6}};` 中の 1 要素 `{1, 2}` を、
 * 配列要素 idx のメンバ単位代入チェーンに展開する。呼出時に '{' は未消費。
 *
 *   chain = (a_idx.m0 = v0, a_idx.m1 = v1, ...)
 *
 * 書かれなかったメンバはここでは 0 埋めしない (C 仕様上は 0 だが、現状
 * struct 要素の初期化漏れは未対応とする)。 */
static node_t *parse_array_elem_struct_brace_init(lvar_t *var, int idx) {
  /* `struct V arr[N] = { ..., [k] = {.a=1, .b=2}, ... }` の `{...}` 部分を、
   * 配列要素 (idx 番目) を target とした struct 初期化として処理する。
   * 自前で member 順次パースしていた旧実装は struct designator (`.a=`) を
   * 受け付けず E3064 を出していた (p280)。parse_struct_initializer に委譲
   * することで designator も positional も両形に対応する。 */
  lvar_t nested = *var;
  nested.offset = var->offset + idx * var->elem_size;
  nested.size = var->elem_size; /* parse_struct_initializer の zero-fill 範囲を要素 1 つに制限 */
  /* 要素が union のときは union 初期化子へ委譲する。struct 用に投げると `.n=5` の
   * メンバ designator を struct レイアウトで誤解決し、値が格納されず 0 になる
   * (`union U a[2]={[1]={.n=5}}` で a[1].n が 0 に化けていた)。配列全体は呼び出し側で
   * zero_prefill 済みなので、ここはメンバ代入を出すだけでよい。 */
  if (var->tag_kind == TK_UNION) return parse_union_initializer(&nested);
  return parse_struct_initializer(&nested);
}

// 初期化子の要素数を 1 増やし、上限を超えていれば診断を出す。
static void bump_initializer_count(int *count) {
  (*count)++;
  if (*count > PS_MAX_INITIALIZER_ELEMENTS) {
    psx_diag_ctx(curtok(), "decl",
                 diag_message_for(DIAG_ERR_PARSER_INITIALIZER_ELEMENT_LIMIT_EXCEEDED),
                 PS_MAX_INITIALIZER_ELEMENTS);
  }
}

/* `char a[] = {"hello"};` 形式 (C11 6.7.9p14) のチェック&パース。
 * 該当する場合のみトークンを消費して init chain を返す。該当しなければ
 * NULL を返し、呼び出し側は通常の brace 初期化に進む。 */
static node_t *try_parse_array_braced_string_initializer(lvar_t *var, int array_len) {
  if (var->elem_size != 1 || !curtok() || curtok()->kind != TK_LBRACE) return NULL;
  token_t *peek = curtok()->next;
  if (!peek || peek->kind != TK_STRING) return NULL;
  token_t *p = peek;
  while (p && p->kind == TK_STRING) p = p->next;
  if (!p || p->kind != TK_RBRACE) return NULL;
  tk_consume('{');
  node_t *str_node = psx_expr_assign(); // 連結を含めて1つの ND_STRING になる
  tk_expect('}');
  if (!str_node || str_node->kind != ND_STRING) return NULL;
  node_string_t *s = (node_string_t *)str_node;
  string_lit_t *lit = find_string_lit_by_label(s->string_label);
  if (!lit) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_STRING_INIT_RESOLVE_FAILED));
  }
  node_t *init_chain = NULL;
  int i = 0;
  int src_pos = 0;
  while (src_pos < lit->len && i < array_len) {
    uint32_t cp = 0;
    if (lit->str[src_pos] == '\\') {
      if (!tk_parse_escape_value(lit->str, lit->len, &src_pos, &cp)) {
        cp = (unsigned char)lit->str[src_pos];
        src_pos++;
      }
    } else {
      cp = (unsigned char)lit->str[src_pos];
      src_pos++;
    }
    init_chain = append_to_init_chain(init_chain,
        build_array_elem_assign(var, i, psx_node_new_num((unsigned char)cp)));
    i++;
  }
  /* 残り全てを 0 で埋める (C11 6.7.9p21)。修正前は 1 個だけだった。 */
  while (i < array_len) {
    init_chain = append_to_init_chain(init_chain,
        build_array_elem_assign(var, i, psx_node_new_num(0)));
    i++;
  }
  return init_chain ? init_chain : psx_node_new_num(0);
}

/* 多次元配列の指定初期化子 [i0][i1]... の第 d 次サブスクリプトの要素単位
 * ストライド。d=0 は outer_stride、d=1 は mid_stride、d>=2 は extra_strides から
 * 求め、該当ストライドが無い最内次元は 1 を返す (chunk_sizes 構築と同じ規則)。
 * 1D 配列は outer_stride=0 なので 1 を返し、従来の単一指定子と一致する。
 * 注意: extra_strides の意味付け上 2D/3D を正しく扱える。4D 以上の深い指定子は
 * best-effort (従来は構文エラーだったため退行ではない)。 */
static int array_desig_elem_stride(const lvar_t *var, int d) {
  if (var->elem_size <= 0) return 1;
  if (d == 0) return var->outer_stride > 0 ? var->outer_stride / var->elem_size : 1;
  if (d == 1) return var->mid_stride > 0 ? var->mid_stride / var->elem_size : 1;
  int i = d - 2;
  if (i < (int)var->extra_strides_count) return var->extra_strides[i] / var->elem_size;
  return 1;
}

static void warn_unsupported_gnu_extension_name(const token_t *tok, const char *name) {
  diag_warn_tokf(DIAG_WARN_PARSER_UNSUPPORTED_GNU_EXTENSION, tok,
                 "%s: %s",
                 diag_warn_message_for(DIAG_WARN_PARSER_UNSUPPORTED_GNU_EXTENSION),
                 name);
}

static void consume_gnu_range_designator_tail_if_any(void) {
  if (curtok()->kind != TK_ELLIPSIS) return;
  warn_unsupported_gnu_extension_name(curtok(), "array range designator");
  set_curtok(curtok()->next);
  (void)psx_expr_assign();
}

static node_t *append_struct_zero_fill_chain(lvar_t *var, node_t *init_chain);
static node_t *consume_nested_designator_and_build_assign(lvar_t *var, tag_member_info_t info);

/* `{ elem, elem, ... }` 形の配列 brace 初期化。
 * 呼出側は冒頭 `{` をまだ消費していない前提 (本ヘルパが consume する)。
 * designator `[idx] = val` / 多次元ネスト指定子 `[i][j] = val` /
 * 多次元ネスト brace `{{...},{...}}` / 要素 struct 初期化子 /
 * 未指定要素の 0 補完 (C11 6.7.9p21) を全て担う。 */
static node_t *parse_array_braced_init(lvar_t *var, int array_len) {
  tk_consume('{');
  node_t *init_chain = NULL;
  int init_elem_count = 0;
  int idx = 0;
  int row_len = (var->outer_stride > 0 && var->elem_size > 0) ? var->outer_stride / var->elem_size : 0;
  bool *assigned = calloc((size_t)(array_len > 0 ? array_len : 1), sizeof(bool));
  /* struct/union 要素の配列は、部分初期化 (no-brace スカラが要素途中で尽きる /
   * 一部要素のみ指定 / 一部メンバのみ designator 指定) で残部が 0 にならない
   * (要素単位の assigned[] や scalar 0-fill ではメンバ単位の補完ができない)。
   * C11 6.7.9p21 に従い、先に配列全体を 0 埋めしてから明示初期化子で上書きする。
   * scalar 要素配列は末尾の per-index 0-fill で足りるので対象外。 */
  node_t *zero_prefill = NULL;
  int elem_is_aggregate = (!var->is_tag_pointer &&
                           (var->tag_kind == TK_STRUCT || var->tag_kind == TK_UNION));
  if (elem_is_aggregate) {
    zero_prefill = append_struct_zero_fill_chain(var, NULL);
  }
  if (!tk_consume('}')) {
    for (;;) {
      int target_idx = idx;
      if (tk_consume('[')) {
        /* 多次元ネスト指定子 [i0][i1]... を平坦化インデックスに畳む。各次元の
         * 要素ストライドを掛けて加算する。単一 [i] のときは stride0 = row_len
         * なので従来の `idx * row_len` と一致する。 */
        int d = 0;
        int flat = 0;
        for (;;) {
          int di = parse_nonneg_const_expr_decl(diag_text_for(DIAG_TEXT_ARRAY_DESIGNATOR_INDEX));
          consume_gnu_range_designator_tail_if_any();
          tk_expect(']');
          flat += di * array_desig_elem_stride(var, d);
          d++;
          if (!tk_consume('[')) break;
        }
        /* `[i].member = val` 連鎖 designator (C11 6.7.9 designator-list)。`[i]` の後に
         * `.` が続く形。要素 (struct/union) のメンバ designator へ降りて代入を生成する。
         * 未指定メンバは冒頭の zero_prefill で 0 になる。`[i] = ...` は従来どおり下で処理。 */
        if (curtok()->kind == TK_DOT && elem_is_aggregate) {
          if (flat < 0 || flat >= array_len) {
            psx_diag_ctx(curtok(), "decl", "%s",
                         diag_message_for(DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
          }
          lvar_t elem = {0};
          elem.offset = var->offset + flat * var->elem_size;
          elem.size = var->elem_size;
          elem.elem_size = var->elem_size;
          elem.tag_kind = var->tag_kind;
          elem.tag_name = var->tag_name;
          elem.tag_len = var->tag_len;
          tk_consume('.');
          token_ident_t *mid = tk_consume_ident();
          if (!mid) psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_MEMBER_NAME));
          tag_member_info_t minfo = {0};
          minfo.tag_kind = TK_EOF;
          if (!tag_find_member(&elem, mid->str, mid->len, &minfo)) {
            psx_diag_ctx(curtok(), "decl", "%s",
                         diag_message_for(DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
          }
          init_chain = append_to_init_chain(init_chain,
              consume_nested_designator_and_build_assign(&elem, minfo));
          bump_initializer_count(&init_elem_count);
          assigned[flat] = true;
          idx = flat + 1;
          if (tk_consume('}')) break;
          tk_expect(',');
          if (tk_consume('}')) break;
          continue;
        }
        tk_expect('=');
        target_idx = flat;
      }
      /* 多次元 char 配列の行を文字列リテラルで初期化:
       * `char a[2][6] = {"hello", "world"}`。各文字列が row_len バイトの行を埋め、
       * 残りは 0。文字列をスカラ要素として処理すると行が壊れていた。 */
      if (row_len > 0 && var->elem_size == 1 && curtok() && curtok()->kind == TK_STRING) {
        node_t *str_node = psx_expr_assign();  /* 隣接文字列の連結も 1 ノードに */
        if (str_node && str_node->kind == ND_STRING) {
          node_string_t *s = (node_string_t *)str_node;
          string_lit_t *lit = find_string_lit_by_label(s->string_label);
          int base = target_idx;  /* 行先頭の平坦要素インデックス */
          int j = 0, sp = 0;
          if (lit) {
            while (sp < lit->len && j < row_len) {
              uint32_t cp = 0;
              if (lit->str[sp] == '\\') {
                if (!tk_parse_escape_value(lit->str, lit->len, &sp, &cp)) {
                  cp = (unsigned char)lit->str[sp]; sp++;
                }
              } else {
                cp = (unsigned char)lit->str[sp]; sp++;
              }
              if (base + j < array_len) {
                init_chain = append_to_init_chain(init_chain,
                    build_array_elem_assign(var, base + j, psx_node_new_num((unsigned char)cp)));
                assigned[base + j] = true;
              }
              j++;
            }
          }
          /* 行の残りを 0 埋め */
          while (j < row_len) {
            if (base + j < array_len) {
              init_chain = append_to_init_chain(init_chain,
                  build_array_elem_assign(var, base + j, psx_node_new_num(0)));
              assigned[base + j] = true;
            }
            j++;
          }
          bump_initializer_count(&init_elem_count);
          idx = target_idx + row_len;
          if (tk_consume('}')) break;
          tk_expect(',');
          if (tk_consume('}')) break;
          continue;
        }
      }
      /* 多次元配列のネスト brace: {{1,2,3},{4,5,6}} など。
       * 3D/4D/5D... のチャンクサイズを組み立てて parse_array_init_chunk へ委譲。 */
      if (row_len > 0 && tk_consume('{')) {
        int chunk_sizes[8] = {0};
        int depth = 0;
        chunk_sizes[depth++] = row_len;
        if (var->mid_stride > 0 && var->elem_size > 0) {
          chunk_sizes[depth++] = var->mid_stride / var->elem_size;
        }
        for (int i = 0; i < var->extra_strides_count && depth < 8; i++) {
          if (var->elem_size > 0) {
            chunk_sizes[depth++] = var->extra_strides[i] / var->elem_size;
          }
        }
        node_t *sub = parse_array_init_chunk(var, &init_elem_count, assigned, array_len,
                                             target_idx, chunk_sizes, depth);
        if (sub) {
          init_chain = init_chain ? psx_node_new_binary(ND_COMMA, init_chain, sub) : sub;
        }
        idx = target_idx + row_len;
      } else {
        bump_initializer_count(&init_elem_count);
        if (target_idx >= array_len) {
          psx_diag_ctx(curtok(), "decl", "%s",
                       diag_message_for(DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
        }
        /* C11 6.7.9p19: 同一 subobject への複数の指定初期化子は「後勝ち」で
         * 上書きされる (エラーではない)。逐次代入を順に発行すれば最後の代入が
         * 残るため重複を拒否しない。 */
        /* 配列要素が struct/union で初期化子が `{...}` (`struct P a[3] = {{1, 2}, ...}`):
         * 要素単位の代入式チェーンに展開する。 */
        if (curtok() && curtok()->kind == TK_LBRACE &&
            !var->is_tag_pointer &&
            (var->tag_kind == TK_STRUCT || var->tag_kind == TK_UNION)) {
          init_chain = append_to_init_chain(init_chain,
              parse_array_elem_struct_brace_init(var, target_idx));
        } else if (!var->is_tag_pointer && var->tag_kind == TK_STRUCT) {
          /* struct 配列要素の brace 省略 (`struct P a[2] = {1, 2, 3, 4}` で
           * a[0]={1,2}, a[1]={3,4})。要素を nested struct とみなし、scalar 始まりなら
           * 内側メンバを取り込み、互換 struct 式ならコピー初期化する。 */
          lvar_t nested = *var;
          nested.offset = var->offset + target_idx * var->elem_size;
          nested.size = var->elem_size;
          init_chain = append_to_init_chain(init_chain,
              parse_struct_member_no_brace(&nested));
        } else {
          init_chain = append_to_init_chain(init_chain,
              build_array_elem_assign(var, target_idx, parse_scalar_brace_initializer()));
        }
        assigned[target_idx] = true;
        idx = target_idx + 1;
      }
      if (tk_consume('}')) break;
      tk_expect(',');
      if (tk_consume('}')) break;
    }
  }
  /* C11 6.7.9p21: 部分初期化や指定初期化子で書かれなかった要素は 0 で初期化。
   * elem_size が 1 を超えるスカラ要素のみ対応。struct/union 要素配列は冒頭の
   * zero_prefill で配列全体を 0 埋め済みなのでここはスキップする。 */
  if (!elem_is_aggregate && array_len > 0 && var->elem_size > 0) {
    for (int i = 0; i < array_len; i++) {
      if (assigned[i]) continue;
      init_chain = append_to_init_chain(init_chain,
          build_array_elem_assign(var, i, psx_node_new_num(0)));
    }
  }
  free(assigned);
  node_t *body = init_chain ? init_chain : psx_node_new_num(0);
  /* 事前 0 埋めを明示初期化子より前に発行する (上書き順序のため COMMA で先頭に置く)。 */
  if (zero_prefill) return psx_node_new_binary(ND_COMMA, zero_prefill, body);
  return body;
}

/* `char a[N] = "hello"` 形の raw 文字列リテラル初期化 (C11 6.7.9p14)。
 * 各文字を psx_node_new_num で elem ごとに assign し、配列長より短ければ
 * 残りを 0 で埋める (p21)。 */
static node_t *build_array_string_initializer(lvar_t *var, node_string_t *s, int array_len) {
  node_t *init_chain = NULL;
  string_lit_t *lit = find_string_lit_by_label(s->string_label);
  if (!lit) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_STRING_INIT_RESOLVE_FAILED));
  }
  int idx = 0;
  int src_pos = 0;
  /* 要素幅 (= var->elem_size) のコードユニットに変換して格納する。char/u8 配列は
   * 1 バイト = 1 ユニット、u/U/L 配列は UTF-8 をデコードしたコードユニット (u は補助面で
   * サロゲート対)。dispatch 側で elem_size == 文字列 char_width を保証済み。 */
  int cw = var->elem_size > 0 ? var->elem_size : 1;
  while (src_pos < lit->len && idx < array_len) {
    uint32_t units[2];
    int nu = tk_next_string_code_units(lit->str, lit->len, &src_pos, cw, units);
    for (int k = 0; k < nu && idx < array_len; k++) {
      init_chain = append_to_init_chain(init_chain,
          build_array_elem_assign(var, idx, psx_node_new_num((long long)units[k])));
      idx++;
    }
  }
  /* 文字列が配列長より短い場合 (`char a[10] = "hi"`) は残り全てを 0 で埋める。 */
  while (idx < array_len) {
    init_chain = append_to_init_chain(init_chain,
        build_array_elem_assign(var, idx, psx_node_new_num(0)));
    idx++;
  }
  return init_chain ? init_chain : psx_node_new_num(0);
}

static node_t *parse_array_initializer(lvar_t *var) {
  int array_len = var->elem_size > 0 ? (var->size / var->elem_size) : 0;
  // 特例: `char a[] = {"hello"};` 形の波括弧で囲まれた文字列リテラル
  // (隣接連結も含む) は C11 6.7.9p14 により素の文字列初期化と同じに扱う。
  {
    node_t *str_init = try_parse_array_braced_string_initializer(var, array_len);
    if (str_init) return str_init;
  }
  if (curtok() && curtok()->kind == TK_LBRACE) {
    return parse_array_braced_init(var, array_len);
  }

  node_t *rhs = psx_expr_assign();
  /* `char a[N] = "hello"` / `unsigned short a[N] = u"hi"` / `T a[N] = U".."`/`L".."` 形
   * (波括弧なし raw 文字列): 各コード単位を要素 assign。要素幅 (elem_size) が文字列の
   * char_width (char/u8=1, u=2, U/L=4) と一致するときに限る (ASCII 内容のみ対応。
   * 非 ASCII の UTF-8→UTF-16/32 デコードは未対応)。 */
  if (rhs->kind == ND_STRING) {
    int cw = (int)((node_string_t *)rhs)->char_width;
    if (cw <= 0) cw = 1;
    if (var->elem_size == cw) {
      return build_array_string_initializer(var, (node_string_t *)rhs, array_len);
    }
  }
  node_t *init_chain = NULL;
  /* Extension: `int arr[N] = (T[N]){...}` 形式 (compound literal で配列初期化)。
   * parse_compound_literal_from_type は ND_COMMA(init_chain, ND_ADDR(lvar)) を
   * 返すので、その lvar から要素ごとに arr へ copy する init_chain を生成する。
   * Clang/GCC が拡張で受け付ける形式で、p304 (関数ポインタ配列の compound
   * literal init) の最終段でこの経路が要る。 */
  if (array_len > 0 && rhs && rhs->kind == ND_COMMA &&
      rhs->rhs && rhs->rhs->kind == ND_ADDR &&
      rhs->rhs->lhs && rhs->rhs->lhs->kind == ND_LVAR) {
    node_lvar_t *src = (node_lvar_t *)rhs->rhs->lhs;
    int elem_size = var->elem_size;
    init_chain = rhs->lhs; /* compound literal の init 部分 */
    for (int i = 0; i < array_len; i++) {
      node_t *dst = new_array_elem_lvar_at(var->offset, elem_size, i);
      node_t *src_node = psx_node_new_lvar_typed(src->offset + i * elem_size, elem_size);
      node_mem_t *assign = psx_node_new_assign(dst, src_node);
      assign->type_size = elem_size;
      init_chain = append_to_init_chain(init_chain, (node_t *)assign);
    }
    return init_chain;
  }
  // Extension: scalar expression for array init
  //   int a[3] = 1;  => a[0]=1, a[1]=0, a[2]=0
  if (array_len > 0) {
    init_chain = build_array_elem_assign(var, 0, rhs);
    for (int idx = 1; idx < array_len; idx++) {
      init_chain = append_to_init_chain(init_chain,
          build_array_elem_assign(var, idx, psx_node_new_num(0)));
    }
    return init_chain;
  }
  return psx_node_new_num(0);
}

static node_t *new_struct_member_lvar(lvar_t *var, int member_offset, int member_type_size,
                                      token_kind_t member_tag_kind, char *member_tag_name,
                                      int member_tag_len, int member_is_tag_pointer) {
  node_t *lvar = psx_node_new_lvar_typed(var->offset + member_offset, member_type_size);
  ((node_lvar_t *)lvar)->mem.tag_kind = member_tag_kind;
  ((node_lvar_t *)lvar)->mem.tag_name = member_tag_name;
  ((node_lvar_t *)lvar)->mem.tag_len = member_tag_len;
  ((node_lvar_t *)lvar)->mem.is_tag_pointer = member_is_tag_pointer;
  return lvar;
}

/* _Bool 要素/メンバの初期化子を (v != 0) に正規化 (C11 6.3.1.2)。is_bool=0 なら素通し。 */
static node_t *bool_normalize_if(node_t *v, int is_bool) {
  if (is_bool && v) return psx_node_new_binary(ND_NE, v, psx_node_new_num(0));
  return v;
}

/* 配列メンバの 1 要素代入を構築する。_Bool 正規化と float/double の fp_kind 伝播
 * (float 配列メンバ `float v[4]` の要素 store を fp store にする) をまとめて行う。 */
static node_mem_t *build_member_array_elem_assign_node(node_t *lhs, node_t *value, int elem_size,
                                                       tk_float_kind_t fp_kind, int is_bool) {
  value = bool_normalize_if(value, is_bool);
  node_mem_t *an = psx_node_new_assign(lhs, value);
  an->type_size = elem_size;
  if (fp_kind != TK_FLOAT_KIND_NONE) {
    lhs->fp_kind = fp_kind;
    an->base.fp_kind = fp_kind;
  }
  return an;
}

/* 多次元 char 配列メンバ (`char c[2][2][3]`) の brace init を再帰的に展開する。
 * 呼出時点で外側 `{` は **消費済み** (caller が tk_consume 済み、内部の再帰も同様)。
 *
 *   ndim == 1: 行 (dims[0] バイト)。内側 brace の中で文字列 1 つ (`{"ab"}`) を展開する
 *              or 数値スカラを並べる (`{'a','b'}`)。
 *   ndim >= 2: dims[0] 個の要素を順に処理し、各要素は内側 (ndim-1) 次元配列。
 *              内側 `{` で再帰、文字列だけなら ndim==2 のとき行 brace elision として展開。
 *
 * `*flat` はメンバ先頭からの累積バイト位置。要素ごとに進め、関数末で level 末まで進める。 */
static node_t *parse_multidim_char_member_brace(lvar_t *owner, int member_offset,
                                                int array_len, const int *dims, int ndim,
                                                int *flat, node_t *init_chain,
                                                tk_float_kind_t member_fp_kind, int member_is_bool);
static node_t *parse_multidim_tag_member_brace(lvar_t *owner, int member_offset, int elem_size,
                                               int array_len, const int *dims, int ndim,
                                               int *flat, node_t *init_chain,
                                               token_kind_t tag_kind, char *tag_name, int tag_len);
static node_t *emit_string_row_assigns(lvar_t *owner, int member_offset, int row_w,
                                       int array_len, int *flat, node_string_t *s,
                                       node_t *init_chain,
                                       tk_float_kind_t fp_kind, int is_bool);

static int consume_terminal_zero_initializer(void) {
  token_t *t = curtok();
  if (!t || t->kind != TK_NUM || tk_as_num(t)->num_kind != TK_NUM_KIND_INT ||
      tk_as_num_int(t)->val != 0) {
    return 0;
  }
  token_t *next = t->next;
  if (!next || next->kind != TK_RBRACE) return 0;
  set_curtok(next);
  return 1;
}

static bool brace_starts_whole_array_initializer(token_t *t) {
  if (!t || t->kind != TK_LBRACE) return false;
  token_t *p = t->next;
  if (p && p->kind == TK_LBRACKET) return true;
  int brace_depth = 1;
  int paren_depth = 0;
  int bracket_depth = 0;
  for (; p; p = p->next) {
    if (p->kind == TK_LBRACE) {
      brace_depth++;
    } else if (p->kind == TK_RBRACE) {
      brace_depth--;
      if (brace_depth == 0) return false;
    } else if (p->kind == TK_LPAREN) {
      paren_depth++;
    } else if (p->kind == TK_RPAREN) {
      if (paren_depth > 0) paren_depth--;
    } else if (p->kind == TK_LBRACKET) {
      bracket_depth++;
    } else if (p->kind == TK_RBRACKET) {
      if (bracket_depth > 0) bracket_depth--;
    } else if (p->kind == TK_COMMA &&
               (brace_depth == 1 || (brace_depth == 2 && t->next && t->next->kind == TK_LBRACE)) &&
               paren_depth == 0 && bracket_depth == 0) {
      return true;
    }
  }
  return false;
}

static node_t *parse_scalar_array_member_brace_body(lvar_t *owner, int member_offset,
                                                    int elem_size, int array_len,
                                                    tk_float_kind_t member_fp_kind,
                                                    int member_is_bool) {
  if (!tk_consume('{')) return psx_node_new_num(0);
  node_t *init_chain = NULL;
  int idx = 0;
  if (!tk_consume('}')) {
    if (brace_starts_whole_array_initializer(curtok())) {
      node_t *chain = parse_scalar_array_member_brace_body(owner, member_offset, elem_size,
                                                          array_len, member_fp_kind,
                                                          member_is_bool);
      tk_expect('}');
      return chain;
    }
    for (;;) {
      int target_idx = idx;
      if (tk_consume('[')) {
        target_idx = parse_nonneg_const_expr_decl(diag_text_for(DIAG_TEXT_ARRAY_DESIGNATOR_INDEX));
        consume_gnu_range_designator_tail_if_any();
        tk_expect(']');
        tk_expect('=');
      }
      if (target_idx >= array_len) {
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
      }
      node_t *lhs = new_array_elem_lvar_at(owner->offset + member_offset, elem_size, target_idx);
      node_mem_t *assign_node = build_member_array_elem_assign_node(
          lhs, parse_scalar_brace_initializer(), elem_size, member_fp_kind, member_is_bool);
      init_chain = append_to_init_chain(init_chain, (node_t *)assign_node);
      idx = target_idx + 1;
      if (tk_consume('}')) break;
      tk_expect(',');
      if (tk_consume('}')) break;
    }
  }
  return init_chain ? init_chain : psx_node_new_num(0);
}

static node_t *parse_member_initializer(lvar_t *owner, int member_offset, int member_type_size,
                                        token_kind_t member_tag_kind, char *member_tag_name,
                                        int member_tag_len, int member_is_tag_pointer,
                                        int member_array_len, int member_outer_stride,
                                        int member_is_bool, tk_float_kind_t member_fp_kind,
                                        const int *member_arr_dims, int member_arr_ndim) {
  if (member_is_tag_pointer) {
    member_fp_kind = TK_FLOAT_KIND_NONE;
  }
  if (member_array_len > 0) {
    int array_len = member_array_len;
    int elem_size = member_type_size;
    node_t *init_chain = NULL;
    if (tk_consume('{')) {
      if (member_outer_stride <= 0 && member_arr_ndim < 2 &&
          !(member_tag_kind == TK_STRUCT || member_tag_kind == TK_UNION) &&
          brace_starts_whole_array_initializer(curtok())) {
        node_t *chain = parse_scalar_array_member_brace_body(owner, member_offset, elem_size,
                                                            array_len, member_fp_kind,
                                                            member_is_bool);
        tk_expect('}');
        return chain;
      }
      /* 3 次元以上の char 配列メンバ (`char c[2][2][3]`) は、各次元 dims を持って
       * 再帰展開する。2D の outer_stride 経路では「行 = 内側 1 次元 char 配列」しか
       * 表現できず、行自体がさらに 2D 配列となる 3D 以上で内側構造を見れないため。
       * グローバルの gbrace_ctx_t.sub_dims チェーンと同じ機構をローカルに移植。 */
      if (member_arr_ndim >= 3 && member_arr_dims && elem_size == 1) {
        int flat = 0;
        node_t *chain = parse_multidim_char_member_brace(
            owner, member_offset, array_len,
            member_arr_dims, member_arr_ndim, &flat, NULL,
            member_fp_kind, member_is_bool);
        return chain ? chain : psx_node_new_num(0);
      }
      if (member_arr_ndim >= 3 && member_arr_dims &&
          (member_tag_kind == TK_STRUCT || member_tag_kind == TK_UNION) &&
          !member_is_tag_pointer) {
        int flat = 0;
        node_t *chain = parse_multidim_tag_member_brace(
            owner, member_offset, elem_size, array_len,
            member_arr_dims, member_arr_ndim, &flat, NULL,
            member_tag_kind, member_tag_name, member_tag_len);
        return chain ? chain : psx_node_new_num(0);
      }
      /* 多次元配列メンバ (`int a[2][2]`): ネスト brace `{{1,2},{3,4}}` を行優先で
       * フラット展開する。member_outer_stride は 1 行のバイトサイズなので
       * 行要素数 inner_len = outer_stride / elem_size。各行 brace の不足要素は
       * struct 全体の zero-fill に委ねる (行頭スナップで桁をずらす)。 */
      if (member_outer_stride > 0 && elem_size > 0) {
        int inner_len = member_outer_stride / elem_size;
        if (inner_len < 1) inner_len = 1;
        int flat = 0;
        /* 多次元 char 配列メンバ (`char rows[2][4]`) の行を文字列リテラルで初期化
         * する場合 (`{"ab","cd"}` / `{{"ab"},{"cd"}}`)、文字列を inner_len バイトへ
         * バイト展開して flat に書き込み、行ぶん flat を進めるヘルパ。これがないと
         * 文字列リテラルが「.LC アドレスの 1 バイト」として 1 slot に書き込まれ
         * (`strb w20, [x19]`)、行データがポインタ値の下位 1 バイトに化けていた。
         * グローバル経路 (psx_gbrace_flat) と対称な処理。 */
#define EMIT_ROW_FROM_STRING(VAL)                                                                  \
        do {                                                                                       \
          node_string_t *_s = (node_string_t *)(VAL);                                              \
          string_lit_t *_lit = find_string_lit_by_label(_s->string_label);                         \
          int _row = flat / inner_len; flat = _row * inner_len; /* 行頭スナップ */                  \
          int _j = 0, _sp = 0;                                                                     \
          if (_lit) {                                                                              \
            while (_sp < _lit->len && _j < inner_len && flat + _j < array_len) {                   \
              uint32_t _cp = 0;                                                                    \
              if (_lit->str[_sp] == '\\') {                                                        \
                if (!tk_parse_escape_value(_lit->str, _lit->len, &_sp, &_cp)) {                    \
                  _cp = (unsigned char)_lit->str[_sp]; _sp++;                                      \
                }                                                                                  \
              } else { _cp = (unsigned char)_lit->str[_sp]; _sp++; }                               \
              node_t *_lhs = new_array_elem_lvar_at(owner->offset + member_offset, elem_size,      \
                                                    flat + _j);                                    \
              node_mem_t *_an = build_member_array_elem_assign_node(                               \
                  _lhs, psx_node_new_num((int)_cp), elem_size, member_fp_kind, member_is_bool);    \
              if (!init_chain) init_chain = (node_t *)_an;                                         \
              else init_chain = psx_node_new_binary(ND_COMMA, init_chain, (node_t *)_an);          \
              _j++;                                                                                \
            }                                                                                      \
          }                                                                                        \
          flat = (_row + 1) * inner_len; /* 行末まで進める (残りは struct zero-fill) */            \
        } while (0)
        /* 多次元 struct タグ配列メンバ (`struct C rows[3][2]`): 1 要素は struct 値で
         * parse_struct_initializer 経由で初期化する必要がある。スカラ要素 (int 等) は
         * 従来どおり parse_scalar_brace_initializer + flat slot に書き込む。 */
        int is_struct_tag_elem = ((member_tag_kind == TK_STRUCT || member_tag_kind == TK_UNION)
                                   && !member_is_tag_pointer);
        if (!tk_consume('}')) {
          for (;;) {
            /* 外側 `[N]=` designator (C11 6.7.9p6): 行 N へジャンプ。 */
            if (curtok()->kind == TK_LBRACKET) {
              set_curtok(curtok()->next);
              long long ridx = psx_decl_eval_const_int(psx_expr_assign(), NULL);
              consume_gnu_range_designator_tail_if_any();
              tk_expect(']');
              tk_expect('=');
              if (ridx < 0) ridx = 0;
              flat = (int)ridx * inner_len;
            }
            if (tk_consume('{')) {
              int row = flat / inner_len;
              flat = row * inner_len;            /* 行頭へスナップ */
              int k = 0;
              if (!tk_consume('}')) {
                for (;;) {
                  /* 内側 `[M]=` designator: 行内の列 M へジャンプ。 */
                  if (curtok()->kind == TK_LBRACKET) {
                    set_curtok(curtok()->next);
                    long long cidx = psx_decl_eval_const_int(psx_expr_assign(), NULL);
                    consume_gnu_range_designator_tail_if_any();
                    tk_expect(']');
                    tk_expect('=');
                    if (cidx < 0) cidx = 0;
                    k = (int)cidx;
                    flat = row * inner_len + k;
                  }
                  if (is_struct_tag_elem && curtok()->kind == TK_LBRACE) {
                    /* struct/union 要素 `{...}`: 1 slot ぶん解釈する。
                     * nested lvar の offset / size / tag を 1 要素に絞ることで designator
                     * (`.val=99`) も positional もそのまま解決できる。 */
                    lvar_t nested = {0};
                    nested.offset = owner->offset + member_offset + flat * elem_size;
                    nested.size = elem_size;
                    nested.elem_size = elem_size;
                    nested.tag_kind = member_tag_kind;
                    nested.tag_name = member_tag_name;
                    nested.tag_len = member_tag_len;
                    node_t *snode = (member_tag_kind == TK_UNION)
                                       ? parse_union_initializer(&nested)
                                       : parse_struct_initializer(&nested);
                    if (snode) {
                      if (!init_chain) init_chain = snode;
                      else init_chain = psx_node_new_binary(ND_COMMA, init_chain, snode);
                    }
                    flat++; k++;
                    if (tk_consume('}')) break;
                    tk_expect(',');
                    if (tk_consume('}')) break;
                    continue;
                  }
                  node_t *val = parse_scalar_brace_initializer();
                  /* `{{"ab"},{"cd"}}` 形式: 内側 brace 内の文字列を行幅へ展開して
                   * 内側 brace を抜ける (k 進度より flat を優先)。 */
                  if (elem_size == 1 && val && val->kind == ND_STRING) {
                    EMIT_ROW_FROM_STRING(val);
                    /* 内側 brace の残り `,...}` は捨てるのではなく、文字列 1 行で
                     * 完結する想定。後続要素は標準の break / `,` 処理に任せる。 */
                    if (tk_consume('}')) break;
                    tk_expect(',');
                    if (tk_consume('}')) break;
                    continue;
                  }
                  if (k < inner_len && flat < array_len) {
                    node_t *lhs = new_array_elem_lvar_at(owner->offset + member_offset, elem_size, flat);
                    node_mem_t *an = build_member_array_elem_assign_node(lhs, val, elem_size,
                        member_fp_kind, member_is_bool);
                    if (!init_chain) init_chain = (node_t *)an;
                    else init_chain = psx_node_new_binary(ND_COMMA, init_chain, (node_t *)an);
                    flat++; k++;
                  }
                  if (tk_consume('}')) break;
                  tk_expect(',');
                  if (tk_consume('}')) break;
                }
              }
              flat = (row + 1) * inner_len;       /* 次の行頭へ (残りは 0) */
            } else {
              node_t *val = parse_scalar_brace_initializer();
              /* `{"ab","cd"}` 形式 (外側 brace 内に文字列が直接並ぶ; brace elision):
               * 各文字列を 1 行 (inner_len バイト) として展開する。 */
              if (elem_size == 1 && val && val->kind == ND_STRING) {
                EMIT_ROW_FROM_STRING(val);
              } else if (flat < array_len) {
                node_t *lhs = new_array_elem_lvar_at(owner->offset + member_offset, elem_size, flat);
                node_mem_t *an = build_member_array_elem_assign_node(lhs, val, elem_size,
                    member_fp_kind, member_is_bool);
                if (!init_chain) init_chain = (node_t *)an;
                else init_chain = psx_node_new_binary(ND_COMMA, init_chain, (node_t *)an);
                flat++;
              }
            }
            if (tk_consume('}')) break;
            tk_expect(',');
            if (tk_consume('}')) break;
          }
        }
#undef EMIT_ROW_FROM_STRING
        return init_chain ? init_chain : psx_node_new_num(0);
      }
      int idx = 0;
      bool *assigned = calloc((size_t)(array_len > 0 ? array_len : 1), sizeof(bool));
      if (!tk_consume('}')) {
        for (;;) {
          int target_idx = idx;
          if (tk_consume('[')) {
            target_idx = parse_nonneg_const_expr_decl(diag_text_for(DIAG_TEXT_ARRAY_DESIGNATOR_INDEX));
            consume_gnu_range_designator_tail_if_any();
            tk_expect(']');
            tk_expect('=');
          }
          if (target_idx >= array_len) {
            psx_diag_ctx(curtok(), "decl", "%s",
                         diag_message_for(DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
          }
          /* C11 6.7.9p19: 同一要素への複数指定初期化子は後勝ち (エラーではない)。 */
          node_t *init_node;
          if ((member_tag_kind == TK_STRUCT || member_tag_kind == TK_UNION) &&
              !member_is_tag_pointer) {
            if (consume_terminal_zero_initializer()) {
              init_node = psx_node_new_num(0);
            } else {
              /* struct/union 配列メンバの 1 要素 `{...}` を、要素 target_idx を target と
               * して処理する (`.pts={{1,2},{3,4}}` / `[k]={.x=1}`)。 */
              lvar_t nested = {0};
              nested.offset = owner->offset + member_offset + target_idx * elem_size;
              nested.size = elem_size;
              nested.elem_size = elem_size;
              nested.tag_kind = member_tag_kind;
              nested.tag_name = member_tag_name;
              nested.tag_len = member_tag_len;
              init_node = (member_tag_kind == TK_UNION)
                            ? parse_union_initializer(&nested)
                            : parse_struct_initializer(&nested);
            }
          } else {
            node_t *lhs = new_array_elem_lvar_at(owner->offset + member_offset, elem_size, target_idx);
            node_mem_t *assign_node = build_member_array_elem_assign_node(lhs,
                parse_scalar_brace_initializer(), elem_size, member_fp_kind, member_is_bool);
            init_node = (node_t *)assign_node;
          }
          if (!init_chain) init_chain = init_node;
          else init_chain = psx_node_new_binary(ND_COMMA, init_chain, init_node);
          assigned[target_idx] = true;
          idx = target_idx + 1;
          if (tk_consume('}')) break;
          tk_expect(',');
          if (tk_consume('}')) break;
        }
      }
      free(assigned);
      return init_chain ? init_chain : psx_node_new_num(0);
    }
    if (owner->tag_kind == TK_STRUCT ||
        (owner->tag_kind == TK_UNION && ps_get_enable_union_array_member_nonbrace_init())) {
      /* 多次元 char 配列メンバへの brace elision (C11 6.7.9p20):
       *   struct B{char rows[2][4];}; struct B b = {"ab","cd"};
       * 外側 brace 内に文字列が直接並ぶ形は、try_parse_array_member_string_initializer
       * が「最初の文字列で配列全体を埋め」て return してしまい (2 つ目の "cd" を読まない)、
       * 親 parse_struct_initializer のループが "cd" を「次メンバ」として扱い E3064 と
       * 診断していた。多次元 char メンバ (arr_ndim>=2, elem_size==1) の場合は文字列を
       * 行ごとに消費するヘルパに委譲する。 */
      if (member_arr_ndim >= 2 && elem_size == 1 && member_arr_dims &&
          curtok() && curtok()->kind == TK_STRING) {
        int row_w = member_arr_dims[member_arr_ndim - 1];
        if (row_w > 0) {
          int rows_total = array_len / row_w;
          int flat = 0;
          node_t *chain = NULL;
          int r = 0;
          while (r < rows_total && curtok() && curtok()->kind == TK_STRING) {
            node_t *val = psx_expr_assign();
            if (val && val->kind == ND_STRING) {
              flat = r * row_w;
              chain = emit_string_row_assigns(owner, member_offset, row_w, array_len,
                                              &flat, (node_string_t *)val, chain,
                                              member_fp_kind, member_is_bool);
            }
            r++;
            /* 次が `,文字列` なら次行へ。`,}` / `,.m=` / 親 `}` は親に任せる。 */
            if (!curtok() || curtok()->kind != TK_COMMA) break;
            token_t *nx = curtok()->next;
            if (!nx || nx->kind != TK_STRING) break;
            tk_consume(',');
          }
          return chain ? chain : psx_node_new_num(0);
        }
      }
      // Brace elision for aggregate array members: allow flat scalar list.
      node_t *array_str = try_parse_array_member_string_initializer(owner->offset + member_offset, elem_size, array_len);
      if (array_str) return array_str;
      node_t *array_copy = try_parse_array_member_copy_initializer(owner->offset + member_offset, elem_size, array_len);
      if (array_copy) return array_copy;
      if (consume_terminal_zero_initializer()) return psx_node_new_num(0);
      node_t *lhs0 = new_array_elem_lvar_at(owner->offset + member_offset, elem_size, 0);
      node_mem_t *assign0 = build_member_array_elem_assign_node(lhs0,
          parse_scalar_brace_initializer(), elem_size, member_fp_kind, member_is_bool);
      init_chain = (node_t *)assign0;
      for (int idx = 1; idx < array_len; idx++) {
        /* comma の次が designator (`.m`) / 終端 (`}`) ならこの配列メンバの省略充填は
         * 終了し、comma は親の初期化子ループが消費する (`{1,2,.a={3,4}}` で a を
         * 途中まで埋めてから .a で上書きするケース)。 */
        if (!elision_consume_separator()) break;
        node_t *lhs = new_array_elem_lvar_at(owner->offset + member_offset, elem_size, idx);
        node_mem_t *assign_node = build_member_array_elem_assign_node(lhs,
            parse_scalar_brace_initializer(), elem_size, member_fp_kind, member_is_bool);
        init_chain = psx_node_new_binary(ND_COMMA, init_chain, (node_t *)assign_node);
      }
      return init_chain;
    }
    if (owner->tag_kind == TK_UNION) {
      psx_diag_ctx(curtok(), "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_UNION_ARRAY_MEMBER_NONBRACE_UNSUPPORTED));
    } else {
      psx_diag_ctx(curtok(), "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_ARRAY_INIT_UNSUPPORTED_FORM));
    }
  }
  if (!member_is_tag_pointer && member_tag_kind == TK_STRUCT) {
    lvar_t nested = {0};
    nested.offset = owner->offset + member_offset;
    nested.elem_size = member_type_size;
    nested.size = member_type_size;
    nested.tag_kind = TK_STRUCT;
    nested.tag_name = member_tag_name;
    nested.tag_len = member_tag_len;
    /* C11 6.7.9: struct メンバの初期化は `{...}`、同型の式 (compound literal や
     * struct lvar) によるコピー、または brace 省略 (`{1,2,3}` の内側展開) で行える。
     * `{` 以外で始まるときは copy か brace 省略かを式の型で判断する。 */
    if (curtok() && curtok()->kind != TK_LBRACE) {
      return parse_struct_member_no_brace(&nested);
    }
    return parse_struct_initializer(&nested);
  }
  if (!member_is_tag_pointer && member_tag_kind == TK_UNION) {
    lvar_t nested = {0};
    nested.offset = owner->offset + member_offset;
    nested.elem_size = member_type_size;
    nested.size = member_type_size;
    nested.tag_kind = TK_UNION;
    nested.tag_name = member_tag_name;
    nested.tag_len = member_tag_len;
    return parse_union_initializer(&nested);
  }
  if (!is_supported_scalar_store_size(member_type_size)) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_AGGREGATE_INIT_SCALAR_SIZE_UNSUPPORTED));
  }
  return parse_scalar_brace_initializer();
}

/* 文字列リテラル val を `flat` から `row_w` バイト (上限 array_len) に展開し、
 * 1 バイトずつ assign ノードを init_chain に追加する。残りは struct 全体の zero-fill
 * に委ねる (caller が flat を行末まで進める)。grobal 経路 (psx_gbrace_flat) と対称。 */
static node_t *emit_string_row_assigns(lvar_t *owner, int member_offset, int row_w,
                                       int array_len, int *flat, node_string_t *s,
                                       node_t *init_chain,
                                       tk_float_kind_t fp_kind, int is_bool) {
  string_lit_t *lit = find_string_lit_by_label(s->string_label);
  if (!lit) return init_chain;
  int j = 0, sp = 0;
  while (sp < lit->len && j < row_w && *flat + j < array_len) {
    uint32_t cp = 0;
    if (lit->str[sp] == '\\') {
      if (!tk_parse_escape_value(lit->str, lit->len, &sp, &cp)) {
        cp = (unsigned char)lit->str[sp]; sp++;
      }
    } else { cp = (unsigned char)lit->str[sp]; sp++; }
    node_t *lhs = new_array_elem_lvar_at(owner->offset + member_offset, 1, *flat + j);
    node_mem_t *an = build_member_array_elem_assign_node(
        lhs, psx_node_new_num((int)cp), 1, fp_kind, is_bool);
    init_chain = init_chain
        ? psx_node_new_binary(ND_COMMA, init_chain, (node_t *)an)
        : (node_t *)an;
    j++;
  }
  return init_chain;
}

static node_t *parse_multidim_tag_member_brace(lvar_t *owner, int member_offset, int elem_size,
                                               int array_len, const int *dims, int ndim,
                                               int *flat, node_t *init_chain,
                                               token_kind_t tag_kind, char *tag_name, int tag_len) {
  if (tk_consume('}')) return init_chain;
  int level_total = 1;
  for (int i = 0; i < ndim; i++) level_total *= dims[i];
  int elems = dims[0];
  int per_elem = level_total / (elems > 0 ? elems : 1);
  int level_start = *flat;
  int idx = 0;
  for (;;) {
    if (curtok()->kind == TK_LBRACKET) {
      set_curtok(curtok()->next);
      long long didx = psx_decl_eval_const_int(psx_expr_assign(), NULL);
      consume_gnu_range_designator_tail_if_any();
      tk_expect(']');
      tk_expect('=');
      if (didx < 0) didx = 0;
      idx = (int)didx;
    }
    if (idx < elems) *flat = level_start + idx * per_elem;
    if (ndim > 1) {
      if (tk_consume('{')) {
        init_chain = parse_multidim_tag_member_brace(owner, member_offset, elem_size,
                                                     array_len, dims + 1, ndim - 1,
                                                     flat, init_chain,
                                                     tag_kind, tag_name, tag_len);
      } else {
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_ARRAY_INIT_UNSUPPORTED_FORM));
      }
    } else {
      if (*flat >= array_len) {
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
      }
      lvar_t nested = {0};
      nested.offset = owner->offset + member_offset + (*flat) * elem_size;
      nested.size = elem_size;
      nested.elem_size = elem_size;
      nested.tag_kind = tag_kind;
      nested.tag_name = tag_name;
      nested.tag_len = tag_len;
      node_t *snode = (tag_kind == TK_UNION)
                        ? parse_union_initializer(&nested)
                        : parse_struct_initializer(&nested);
      init_chain = init_chain
          ? psx_node_new_binary(ND_COMMA, init_chain, snode)
          : snode;
    }
    idx++;
    *flat = level_start + idx * per_elem;
    if (tk_consume('}')) break;
    tk_expect(',');
    if (tk_consume('}')) break;
  }
  *flat = level_start + level_total;
  return init_chain;
}

static node_t *parse_multidim_char_member_brace(lvar_t *owner, int member_offset,
                                                int array_len, const int *dims, int ndim,
                                                int *flat, node_t *init_chain,
                                                tk_float_kind_t member_fp_kind, int member_is_bool) {
  if (tk_consume('}')) return init_chain;
  /* この level が消費する総バイト数 (dims[0..ndim) の積)。要素境界を決めるのに使う。 */
  int level_total = 1;
  for (int i = 0; i < ndim; i++) level_total *= dims[i];
  int level_start = *flat;
  int elems = dims[0];
  int per_elem = level_total / (elems > 0 ? elems : 1);
  int idx = 0;
  for (;;) {
    /* この要素の書き出し開始位置を要素境界へスナップ (前要素が短かった場合の埋め)。 */
    if (idx < elems) *flat = level_start + idx * per_elem;
    if (curtok()->kind == TK_LBRACE) {
      if (ndim == 2) {
        /* 内側 brace は「行」: dims[1] バイトを 1 つの文字列で埋めるか、char スカラを並べる。 */
        tk_consume('{');
        int row_w = dims[1];
        if (!tk_consume('}')) {
          int row_start_flat = *flat;
          int k = 0;
          for (;;) {
            node_t *val = parse_scalar_brace_initializer();
            if (val && val->kind == ND_STRING) {
              init_chain = emit_string_row_assigns(owner, member_offset, row_w, array_len,
                                                   flat, (node_string_t *)val, init_chain,
                                                   member_fp_kind, member_is_bool);
              /* 文字列 1 つで行を消費。残り要素は捨てる (グローバル経路と同じ振る舞い)。 */
              if (tk_consume('}')) break;
              tk_expect(',');
              if (tk_consume('}')) break;
              continue;
            }
            if (k < row_w && *flat + k < array_len) {
              node_t *lhs = new_array_elem_lvar_at(owner->offset + member_offset, 1, *flat + k);
              node_mem_t *an = build_member_array_elem_assign_node(lhs, val, 1,
                  member_fp_kind, member_is_bool);
              init_chain = init_chain
                  ? psx_node_new_binary(ND_COMMA, init_chain, (node_t *)an)
                  : (node_t *)an;
              k++;
            }
            if (tk_consume('}')) break;
            tk_expect(',');
            if (tk_consume('}')) break;
          }
          (void)row_start_flat;
          *flat = row_start_flat + row_w;     /* 行末まで進める (残りは zero-fill) */
        }
      } else {
        /* ndim >= 3: 内側 `{` を消費してから次元を再帰展開。 */
        tk_consume('{');
        init_chain = parse_multidim_char_member_brace(owner, member_offset, array_len,
                                                       dims + 1, ndim - 1, flat, init_chain,
                                                       member_fp_kind, member_is_bool);
      }
    } else if (curtok()->kind == TK_STRING && ndim == 2) {
      /* 行 brace なしの brace elision: `{"ab","cd"}` の各文字列を 1 行として展開。 */
      node_t *val = parse_scalar_brace_initializer();
      init_chain = emit_string_row_assigns(owner, member_offset, dims[1], array_len,
                                           flat, (node_string_t *)val, init_chain,
                                           member_fp_kind, member_is_bool);
      *flat = level_start + (idx + 1) * per_elem;  /* 要素境界へ */
    } else {
      /* 全 brace 省略の scalar 要素 (`{1,2,3,4,...}`)。1 要素 1 バイトずつ書く。 */
      node_t *val = parse_scalar_brace_initializer();
      if (*flat < array_len) {
        node_t *lhs = new_array_elem_lvar_at(owner->offset + member_offset, 1, *flat);
        node_mem_t *an = build_member_array_elem_assign_node(lhs, val, 1,
            member_fp_kind, member_is_bool);
        init_chain = init_chain
            ? psx_node_new_binary(ND_COMMA, init_chain, (node_t *)an)
            : (node_t *)an;
        (*flat)++;
      }
    }
    idx++;
    if (tk_consume('}')) break;
    tk_expect(',');
    if (tk_consume('}')) break;
  }
  /* level 末まで進めて caller の境界計算を正しく保つ。 */
  *flat = level_start + level_total;
  return init_chain;
}

static bool tag_find_member(lvar_t *var, char *name, int len, tag_member_info_t *out) {
  return psx_ctx_find_tag_member_info(var->tag_kind, var->tag_name, var->tag_len,
                                       name, len, out);
}

static bool tag_get_member_at(lvar_t *var, int ordinal, tag_member_info_t *out) {
  return psx_ctx_get_tag_member_info(var->tag_kind, var->tag_name, var->tag_len, ordinal, out);
}

// 次の名前付きメンバまで ordinal を前進。見つかれば true。
// 見つからなかった場合の最終 ordinal は member_count（または途中で「found=false」になった値）。
static bool tag_get_next_named_member(lvar_t *var, int *ordinal_inout,
                                      tag_member_info_t *out) {
  int ordinal = *ordinal_inout;
  int member_count = psx_ctx_get_tag_member_count(var->tag_kind, var->tag_name, var->tag_len);
  while (ordinal < member_count) {
    bool found = tag_get_member_at(var, ordinal, out);
    ordinal++;
    if (!found) { *ordinal_inout = ordinal; return false; }
    if (out->len > 0) { *ordinal_inout = ordinal; return true; }
  }
  *ordinal_inout = ordinal;
  return false;
}

// チェーン末尾に init_node を追加する。先頭時は init_node 自身が新しい先頭。
static node_t *append_to_init_chain(node_t *init_chain, node_t *init_node) {
  if (!init_chain) return init_node;
  return psx_node_new_binary(ND_COMMA, init_chain, init_node);
}

// 多次元配列のネスト初期化子 `{...}` を 1 段分処理する。呼び出し時点で
// 該当階層の `{` は既に消費済み。
// `chunk_sizes[0]` がこの階層のチャンク長、`chunk_sizes[1]` がさらに 1 段
// 内側のチャンク長、... `depth` は呼出側が用意した chunk_sizes の有効長。
// `{` を再度見たら 1 段深く再帰する。
static node_t *parse_array_init_chunk(lvar_t *var, int *init_elem_count, bool *assigned, int array_len,
                                      int start_idx, const int *chunk_sizes, int depth) {
  node_t *init_chain = NULL;
  int chunk_len = chunk_sizes[0];
  int ci = 0;
  while (ci < chunk_len) {
    if (depth > 1 && curtok() && curtok()->kind == TK_LBRACE) {
      // さらに内側のネスト `{...}`
      tk_consume('{');
      node_t *sub = parse_array_init_chunk(var, init_elem_count, assigned, array_len,
                                           start_idx + ci, chunk_sizes + 1, depth - 1);
      if (sub) init_chain = init_chain ? psx_node_new_binary(ND_COMMA, init_chain, sub) : sub;
      ci += chunk_sizes[1];
    } else {
      bump_initializer_count(init_elem_count);
      int flat_idx = start_idx + ci;
      if (flat_idx < array_len) {
        /* 多次元 struct/union 配列の最内側要素が `{...}` で始まるとき:
         * `struct P g[2][2] = {{{1,2},{3,4}}, ...}` の `{1,2}` をパースする。
         * 通常の psx_expr_assign では `{` を数値として読もうとして失敗する。 */
        if (curtok() && curtok()->kind == TK_LBRACE &&
            !var->is_tag_pointer &&
            (var->tag_kind == TK_STRUCT || var->tag_kind == TK_UNION)) {
          init_chain = append_to_init_chain(init_chain,
              parse_array_elem_struct_brace_init(var, flat_idx));
        } else {
          init_chain = append_to_init_chain(init_chain,
              build_array_elem_assign(var, flat_idx, psx_expr_assign()));
        }
        assigned[flat_idx] = true;
      }
      ci++;
    }
    if (tk_consume('}')) return init_chain;
    tk_expect(',');
    if (tk_consume('}')) return init_chain;
  }
  return init_chain;
}

// .name[idx] = val の組み立て。lhs は member の offset+idx 要素を指す lvar。
static node_t *build_nested_array_designator_assign(lvar_t *var,
                                                    const tag_member_info_t *info,
                                                    int nested_idx) {
  node_t *lhs = new_array_elem_lvar_at(var->offset + info->offset, info->type_size, nested_idx);
  node_t *val = parse_scalar_brace_initializer();
  node_mem_t *assign_node = psx_node_new_assign(lhs, val);
  assign_node->type_size = info->type_size;
  return (node_t *)assign_node;
}

// member_init の意味を見て、必要なら ASSIGN ノードで包む。
// 配列メンバや struct/union メンバの場合、parse_member_initializer が
// すでに代入チェーンを返しているのでそのまま使う。
static node_t *wrap_member_init_as_assign(lvar_t *var,
                                          const tag_member_info_t *info,
                                          node_t *member_init) {
  /* 配列メンバ (array_len>0) は member_init が既に要素単位の代入チェーンなので
   * そのまま返す。is_tag_pointer は要素がポインタかどうかであって配列であることとは
   * 独立 (`int (*ops[2])(int,int)` / `struct N *arr[2]` のポインタ配列メンバも配列)。
   * 以前は `!is_tag_pointer` を要求しており、ポインタ配列メンバで init_chain の
   * 最終値を member スロットへ余分に代入し先頭要素を破壊していた。 */
  if (info->array_len > 0 ||
      (!info->is_tag_pointer && (info->tag_kind == TK_STRUCT || info->tag_kind == TK_UNION))) {
    return member_init;
  }
  /* _Bool スカラメンバ初期化子: rhs を (x != 0) に正規化 (C11 6.3.1.2)。
   * これがないと `struct{_Bool b;} s = {5};` で b が 5 のままだった。 */
  if (info->is_bool) {
    member_init = psx_node_new_binary(ND_NE, member_init, psx_node_new_num(0));
  }
  node_t *lhs = new_struct_member_lvar(var, info->offset, info->type_size,
                                       info->tag_kind, info->tag_name, info->tag_len,
                                       info->is_tag_pointer);
  node_mem_t *assign_node = psx_node_new_assign(lhs, member_init);
  assign_node->type_size = info->type_size;
  /* float/double メンバなら lhs と assign に fp_kind を伝播し、
   * IR が FP store を出すようにする。 */
  if (!info->is_tag_pointer && info->fp_kind != TK_FLOAT_KIND_NONE) {
    lhs->fp_kind = info->fp_kind;
    assign_node->base.fp_kind = info->fp_kind;
  }
  /* bitfield メンバ: lhs lvar に bit_width / bit_offset / bit_is_signed を載せ、
   * IR builder の ND_ASSIGN(ND_LVAR) で bitfield 書き込み経路 (mask preserve) を
   * 通す。これがないと storage 全体を上書きして他フィールドを破壊する。 */
  if (info->bit_width > 0) {
    ((node_lvar_t *)lhs)->mem.bit_width = info->bit_width;
    ((node_lvar_t *)lhs)->mem.bit_offset = info->bit_offset;
    ((node_lvar_t *)lhs)->mem.bit_is_signed = info->bit_is_signed;
  }
  return (node_t *)assign_node;
}

/* parse_struct_initializer の `.a.b.c = val` (ネスト designator) 経路。
 * 呼出時に最初の `.a` は消費済みで info に格納されている前提。さらに `.b.c...`
 * を辿って累積 offset を計算し、最深メンバの型で assign node を作って返す。
 * `=` と rhs もここで消費する (parse_scalar_brace_initializer)。 */
static node_t *consume_nested_designator_and_build_assign(lvar_t *var, tag_member_info_t info) {
  int cumulative_offset = info.offset;
  tag_member_info_t cur_info = info;
  /* C11 6.7.9p6+p17: designator は `.member` と `[idx]` を任意に連ねた subobject
   * パスを表せる (`.m.x[1].b`)。`.` なら struct/union メンバへ、`[` なら配列要素へ
   * 降りて累積 offset を更新し、`=` に達するまで辿る。 */
  while (curtok()->kind == TK_DOT || curtok()->kind == TK_LBRACKET) {
    if (curtok()->kind == TK_DOT) {
      if (!((cur_info.tag_kind == TK_STRUCT || cur_info.tag_kind == TK_UNION) &&
            !cur_info.is_tag_pointer)) {
        /* `.member` の左辺が struct/union でない (スカラ/ポインタ/未降下の配列)。 */
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
        break;
      }
      tk_consume('.');
      token_ident_t *id2 = tk_consume_ident();
      if (!id2) psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_MEMBER_NAME));
      tag_member_info_t sub_info = {0};
      sub_info.tag_kind = TK_EOF;
      lvar_t nested_owner = {0};
      nested_owner.tag_kind = cur_info.tag_kind;
      nested_owner.tag_name = cur_info.tag_name;
      nested_owner.tag_len = cur_info.tag_len;
      if (!tag_find_member(&nested_owner, id2->str, id2->len, &sub_info)) {
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
        break;
      }
      cumulative_offset += sub_info.offset;
      cur_info = sub_info;
    } else {
      /* `[idx]`: cur_info が配列メンバ (array_len>0) のときだけ
       * 要素へ降りる。stride は要素サイズ (type_size)。降下後は単一要素なので
       * array_len を 0 にし、後続の `.member` がそのまま tag を引けるようにする。 */
      if (cur_info.array_len <= 0) {
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_NESTED_DESIG_NOT_ARRAY));
        break;
      }
      tk_consume('[');
      int nested_idx = parse_nonneg_const_expr_decl(diag_text_for(DIAG_TEXT_ARRAY_DESIGNATOR_INDEX));
      consume_gnu_range_designator_tail_if_any();
      tk_expect(']');
      if (nested_idx < 0 || nested_idx >= cur_info.array_len) {
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
      }
      cumulative_offset += nested_idx * cur_info.type_size;
      cur_info.array_len = 0;
    }
  }
  tk_expect('=');
  /* leaf が struct/union 集約で `{...}` が続くとき (`.arr[1] = {7, 9}` /
   * `.u[1] = {.n = 7}`) は、その subobject を target とした集約初期化へ委譲する。
   * これがないと parse_scalar_brace_initializer がスカラ扱いし、内側 designator を
   * E3064 で拒否していた。 */
  if (curtok()->kind == TK_LBRACE &&
      (cur_info.tag_kind == TK_STRUCT || cur_info.tag_kind == TK_UNION) &&
      !cur_info.is_tag_pointer) {
    lvar_t nested = {0};
    nested.offset = var->offset + cumulative_offset;
    nested.size = cur_info.type_size;
    nested.elem_size = cur_info.type_size;
    nested.tag_kind = cur_info.tag_kind;
    nested.tag_name = cur_info.tag_name;
    nested.tag_len = cur_info.tag_len;
    return (cur_info.tag_kind == TK_UNION)
               ? parse_union_initializer(&nested)
               : parse_struct_initializer(&nested);
  }
  node_t *lhs = psx_node_new_lvar_typed(var->offset + cumulative_offset, cur_info.type_size);
  ((node_lvar_t *)lhs)->mem.tag_kind = cur_info.tag_kind;
  ((node_lvar_t *)lhs)->mem.tag_name = cur_info.tag_name;
  ((node_lvar_t *)lhs)->mem.tag_len = cur_info.tag_len;
  ((node_lvar_t *)lhs)->mem.is_tag_pointer = cur_info.is_tag_pointer;
  if (cur_info.bit_width > 0) {
    ((node_lvar_t *)lhs)->mem.bit_width = cur_info.bit_width;
    ((node_lvar_t *)lhs)->mem.bit_offset = cur_info.bit_offset;
    ((node_lvar_t *)lhs)->mem.bit_is_signed = cur_info.bit_is_signed;
  }
  node_t *rhs_val = parse_scalar_brace_initializer();
  node_mem_t *assign_node = psx_node_new_assign(lhs, rhs_val);
  assign_node->type_size = cur_info.type_size;
  if (cur_info.fp_kind != TK_FLOAT_KIND_NONE) {
    lhs->fp_kind = cur_info.fp_kind;
    assign_node->base.fp_kind = cur_info.fp_kind;
  }
  return (node_t *)assign_node;
}

static bool member_is_covered_by_unnamed_union(lvar_t *var, const tag_member_info_t *info);
static void skip_remaining_unnamed_union_members(lvar_t *var, const tag_member_info_t *info,
                                                 int *ordinal_inout);
static bool offset_is_covered_by_unnamed_union_rec(token_kind_t tag_kind, char *tag_name,
                                                   int tag_len, int base_offset,
                                                   int target_offset);

/* parse_struct_initializer 末尾の未割当スカラメンバ補完。
 * assigned_names/assigned_lens に登録済みでなく、is_supported_scalar_store_size を
 * 満たすスカラ (= 配列/集約でない) メンバを ordinal 順に探し、明示 0 代入を append する
 * (C11 6.7.9p21)。集約 (struct/union/array) メンバはこの実装では別経路でゼロ化済み
 * (append_struct_zero_fill_chain) なのでここではスキップする。 */
static node_t *append_unassigned_scalar_zero_fills(lvar_t *var, int member_count,
                                                    char **assigned_names, int *assigned_lens,
                                                    int assigned_n, node_t *init_chain) {
  for (int o = 0; o < member_count; o++) {
    tag_member_info_t info = {0};
    info.tag_kind = TK_EOF;
    int probe_ordinal = o;
    if (!tag_get_next_named_member(var, &probe_ordinal, &info)) continue;
    if (info.len <= 0) continue;
    if (member_is_covered_by_unnamed_union(var, &info)) continue;
    int already = 0;
    for (int i = 0; i < assigned_n; i++) {
      if (assigned_lens[i] == info.len &&
          strncmp(assigned_names[i], info.name, (size_t)info.len) == 0) {
        already = 1; break;
      }
    }
    if (already) continue;
    if (!is_supported_scalar_store_size(info.type_size)) continue;
    if (info.array_len > 0 || info.tag_kind == TK_STRUCT || info.tag_kind == TK_UNION) continue;
    node_t *zero = psx_node_new_num(0);
    init_chain = append_to_init_chain(init_chain,
        wrap_member_init_as_assign(var, &info, zero));
  }
  return init_chain;
}

/* var の全 bytes を 8/4/2/1 単位の 0 store チェーンで埋める。
 * struct brace init 冒頭で呼び、部分指定の場合に未代入メンバが
 * garbage 残りしないようにする (C11 6.7.9p21)。 */
static node_t *append_struct_zero_fill_chain(lvar_t *var, node_t *init_chain) {
  int total = var->size > 0 ? var->size : var->elem_size;
  int off = 0;
  while (off + 8 <= total) {
    node_t *lhs = psx_node_new_lvar_typed(var->offset + off, 8);
    node_mem_t *assign = psx_node_new_assign(lhs, psx_node_new_num(0));
    assign->type_size = 8;
    init_chain = append_to_init_chain(init_chain, (node_t *)assign);
    off += 8;
  }
  while (off + 4 <= total) {
    node_t *lhs = psx_node_new_lvar_typed(var->offset + off, 4);
    node_mem_t *assign = psx_node_new_assign(lhs, psx_node_new_num(0));
    assign->type_size = 4;
    init_chain = append_to_init_chain(init_chain, (node_t *)assign);
    off += 4;
  }
  while (off + 2 <= total) {
    node_t *lhs = psx_node_new_lvar_typed(var->offset + off, 2);
    node_mem_t *assign = psx_node_new_assign(lhs, psx_node_new_num(0));
    assign->type_size = 2;
    init_chain = append_to_init_chain(init_chain, (node_t *)assign);
    off += 2;
  }
  while (off + 1 <= total) {
    node_t *lhs = psx_node_new_lvar_typed(var->offset + off, 1);
    node_mem_t *assign = psx_node_new_assign(lhs, psx_node_new_num(0));
    assign->type_size = 1;
    init_chain = append_to_init_chain(init_chain, (node_t *)assign);
    off += 1;
  }
  return init_chain;
}

/* zero-fill 対象判定用に member を assigned 集合へ記録する。既に記録済み (C11
 * 6.7.9p19 の後勝ち重複) なら何もしない。これをしないと重複 designator で
 * assigned_n が member_count (= 配列容量) を超えてバッファ溢れする。 */
static void record_assigned_member(char **names, int *lens, int *kinds, int *n,
                                   int cap, char *name, int len, int kind) {
  if (!name || len <= 0) return;
  for (int i = 0; i < *n; i++) {
    if (lens[i] == len && strncmp(names[i], name, (size_t)len) == 0) return;
  }
  if (*n < cap) {
    names[*n] = name; lens[*n] = len; kinds[*n] = kind; (*n)++;
  }
}

static bool member_is_covered_by_unnamed_union(lvar_t *var, const tag_member_info_t *info) {
  if (!info || info->len <= 0) return false;
  return offset_is_covered_by_unnamed_union_rec(var->tag_kind, var->tag_name, var->tag_len,
                                                0, info->offset);
}

static bool offset_is_covered_by_unnamed_union_rec(token_kind_t tag_kind, char *tag_name,
                                                   int tag_len, int base_offset,
                                                   int target_offset) {
  int member_count = psx_ctx_get_tag_member_count(tag_kind, tag_name, tag_len);
  for (int o = 0; o < member_count; o++) {
    tag_member_info_t mi = {0};
    if (!psx_ctx_get_tag_member_info(tag_kind, tag_name, tag_len, o, &mi)) break;
    if (mi.len != 0 || mi.is_tag_pointer) continue;
    int start = base_offset + mi.offset;
    int end = start + mi.type_size;
    if (mi.len == 0 && mi.tag_kind == TK_UNION && !mi.is_tag_pointer &&
        target_offset >= start && target_offset < end) {
      return true;
    }
    if (mi.tag_kind == TK_STRUCT &&
        target_offset >= start && target_offset < end &&
        offset_is_covered_by_unnamed_union_rec(mi.tag_kind, mi.tag_name, mi.tag_len,
                                               start, target_offset)) {
      return true;
    }
  }
  return false;
}

static void skip_remaining_unnamed_union_members(lvar_t *var, const tag_member_info_t *info,
                                                 int *ordinal_inout) {
  if (!info || !ordinal_inout || !member_is_covered_by_unnamed_union(var, info)) return;
  int member_count = psx_ctx_get_tag_member_count(var->tag_kind, var->tag_name, var->tag_len);
  int ordinal = *ordinal_inout;
  while (ordinal < member_count) {
    tag_member_info_t next = {0};
    if (!tag_get_member_at(var, ordinal, &next)) break;
    if (next.len <= 0 || !member_is_covered_by_unnamed_union(var, &next)) break;
    ordinal++;
  }
  *ordinal_inout = ordinal;
}

static node_t *parse_struct_initializer(lvar_t *var) {
  if (!tk_consume('{')) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_AGGREGATE_INIT_BRACE_REQUIRED));
  }
  int member_count = psx_ctx_get_tag_member_count(var->tag_kind, var->tag_name, var->tag_len);
  node_t *init_chain = NULL;
  int ordinal = 0;
  /* C11 6.7.9p21: brace 指定がない要素は 0 として初期化される。
   * 既存の「末尾でスカラ未指定メンバを 0 で埋める」処理は struct/union/array
   * メンバを skip するため、部分指定だと struct メンバが garbage のままだった。
   * 確実にゼロ化するため struct 全体を 0 ストアで埋めてから、明示代入で上書きする。 */
  init_chain = append_struct_zero_fill_chain(var, init_chain);
  // assigned_kind[i]: 0=full assignment ('.m = v' or ordinal), 1=indexed-only ('.m[i]=v')。
  // 完全代入とインデックス指定が同じメンバ名で混在したら重複と扱う。
  char **assigned_names = calloc((size_t)(member_count > 0 ? member_count : 1), sizeof(char *));
  int *assigned_lens = calloc((size_t)(member_count > 0 ? member_count : 1), sizeof(int));
  int *assigned_kind = calloc((size_t)(member_count > 0 ? member_count : 1), sizeof(int));
  int assigned_n = 0;
  if (!tk_consume('}')) {
    for (;;) {
      tag_member_info_t info = {0};
      info.tag_kind = TK_EOF;
      bool found = false;
      if (tk_consume('.')) {
        token_ident_t *id = tk_consume_ident();
        if (!id) psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_MEMBER_NAME));
        found = tag_find_member(var, id->str, id->len, &info);
        /* C11 6.7.9p17: designator の後に続く位置指定初期化子は、その designated
         * member の「次」のメンバから継続する。positional 用 ordinal を designated
         * member の index+1 に同期する (`{.b=2, 3, 4}` の 3 は c、4 は d)。 */
        if (found) {
          tag_member_info_t probe = {0};
          for (int o = 0; o < member_count; o++) {
            if (tag_get_member_at(var, o, &probe) && probe.len == info.len &&
                probe.len > 0 && info.name &&
                strncmp(probe.name, info.name, (size_t)info.len) == 0) {
              ordinal = o + 1;
              break;
            }
          }
        }
        /* designator のサブパス (C11 6.7.9p6 + 6.7.9p17): `.member` の後に `.x` や
         * `[i]` が続けば subobject へ降りるパス指定 (`.a.b`, `.a[i]`, `.m.x[1].b`,
         * `.arr[i].f` など)。consume_nested_designator_and_build_assign が `.`/`[` の
         * 連鎖を辿り累積 offset の lhs と代入を作る。`.member = val` (続きなし) は
         * 下の tk_expect('=') へ。C11 6.7.9p19 により同一 subobject への重複指定は
         * 後勝ち (エラーではない)。 */
        if (found && (curtok()->kind == TK_DOT || curtok()->kind == TK_LBRACKET)) {
          init_chain = append_to_init_chain(init_chain,
              consume_nested_designator_and_build_assign(var, info));
          /* トップレベル `member` への部分代入として記録 (`.a.x=1, .a.y=2` 混在を許す)。 */
          record_assigned_member(assigned_names, assigned_lens, assigned_kind,
                                 &assigned_n, member_count, info.name, info.len, 1);
          if (tk_consume('}')) break;
          tk_expect(',');
          if (tk_consume('}')) break;
          continue;
        }
        tk_expect('=');
      } else {
        found = tag_get_next_named_member(var, &ordinal, &info);
      }
      if (!found || info.len <= 0) {
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
      }
      /* C11 6.7.9p19: 同名メンバへの複数指定初期化子は後勝ち (エラーではない)。
       * 逐次代入を順に発行すれば最後の代入が残る。 */
      node_t *member_init = parse_member_initializer(var, info.offset, info.type_size,
                                                     info.tag_kind, info.tag_name, info.tag_len,
                                                     info.is_tag_pointer, info.array_len, info.outer_stride, info.is_bool, info.fp_kind,
                                                     info.arr_dims, info.arr_ndim);
      init_chain = append_to_init_chain(init_chain,
          wrap_member_init_as_assign(var, &info, member_init));
      skip_remaining_unnamed_union_members(var, &info, &ordinal);
      record_assigned_member(assigned_names, assigned_lens, assigned_kind,
                             &assigned_n, member_count, info.name, info.len, 0);
      if (tk_consume('}')) break;
      tk_expect(',');
      if (tk_consume('}')) break;
    }
  }
  /* C11 6.7.9p21: 部分初期化や指定初期化子で書かれなかったメンバは 0 で
   * 初期化される (struct S s = {10, 20}; なら c, d = 0)。スカラのみ対応。 */
  init_chain = append_unassigned_scalar_zero_fills(var, member_count,
                                                    assigned_names, assigned_lens,
                                                    assigned_n, init_chain);
  free(assigned_names);
  free(assigned_lens);
  free(assigned_kind);
  return init_chain ? init_chain : psx_node_new_num(0);
}

/* 解析済みの value から struct コピー初期化チェーンを構築する。互換型でない
 * (= scalar 等) なら NULL を返す (呼び出し側が brace 省略やエラーを判断する)。 */
static node_t *build_struct_copy_from_value(lvar_t *var, node_t *value) {
  node_t *init_chain = NULL;
  if (value && value->kind == ND_LVAR && is_compatible_tag_object_lvar((node_lvar_t *)value, var)) {
    init_chain = build_struct_copy_chain_from_source(var, (node_lvar_t *)value);
  } else if (value && value->kind == ND_GVAR &&
             is_compatible_tag_object_lvar((node_lvar_t *)value, var)) {
    /* グローバル構造体からのコピー初期化 `struct S t = g;`。構造体全体を 1 つの
     * ND_ASSIGN でコピーする (代入文 `t = g` と同じ memcpy 経路)。node_gvar_t は
     * node_lvar_t と同じく先頭が node_mem_t なので互換判定を共用できる。 */
    node_t *lhs_var = psx_node_new_lvar_typed(var->offset, var->size);
    node_mem_t *assign_node = psx_node_new_assign(lhs_var, value);
    assign_node->type_size = var->size;
    init_chain = (node_t *)assign_node;
  } else if (value && value->kind == ND_DEREF &&
             is_compatible_tag_object_mem((node_mem_t *)value, var)) {
    /* `va_arg(ap, struct S)` expands to `*(struct S *)...`: a same-type
     * struct lvalue that can be copied as a whole. */
    node_t *lhs_var = psx_node_new_lvar_typed(var->offset, var->size);
    node_mem_t *assign_node = psx_node_new_assign(lhs_var, value);
    assign_node->type_size = var->size;
    init_chain = (node_t *)assign_node;
  } else if (value && value->kind == ND_TERNARY) {
    node_ctrl_t *ternary = (node_ctrl_t *)value;
    node_t *then_prefix = NULL;
    node_t *else_prefix = NULL;
    node_lvar_t *then_src = NULL;
    node_lvar_t *else_src = NULL;
    resolve_copy_source_lvar(ternary->base.rhs, &then_prefix, &then_src);
    resolve_copy_source_lvar(ternary->els, &else_prefix, &else_src);
    if (!is_compatible_tag_object_lvar(then_src, var) || !is_compatible_tag_object_lvar(else_src, var)) {
      /* 分岐が lvar でない (`struct S s = c ? ok() : err()` の funccall 分岐など) ときは
       * struct 全体の ND_ASSIGN(var, ternary) にする。<=8B はスカラ選択、>8B は IR の
       * materialize_aggregate_expr_to が各分岐を dst へ materialize する。 */
      if (var->size <= 8 || ps_node_type_size(value) == var->size) {
        node_t *lhs_var = psx_node_new_lvar_typed(var->offset, var->size);
        node_mem_t *assign_node = psx_node_new_assign(lhs_var, value);
        assign_node->type_size = var->size;
        return (node_t *)assign_node;
      }
      psx_diag_ctx(curtok(), "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_STRUCT_COPY_COMPAT_REQUIRED));
    }
    node_ctrl_t *copy_select = arena_alloc(sizeof(node_ctrl_t));
    copy_select->base.kind = ND_TERNARY;
    copy_select->base.lhs = ternary->base.lhs;
    node_t *then_copy = build_struct_copy_chain_from_source(var, then_src);
    node_t *else_copy = build_struct_copy_chain_from_source(var, else_src);
    copy_select->base.rhs = then_prefix ? psx_node_new_binary(ND_COMMA, then_prefix, then_copy) : then_copy;
    copy_select->els = else_prefix ? psx_node_new_binary(ND_COMMA, else_prefix, else_copy) : else_copy;
    init_chain = (node_t *)copy_select;
  } else if (var->size <= 8 && value &&
             (value->kind == ND_FUNCALL || value->kind == ND_DEREF)) {
    /* ≤8B struct: 関数呼び出し結果や `*ptr` deref の非 lvar 値を
     * 1 ワード assign でコピー初期化する。 */
    node_t *lhs_var = psx_node_new_lvar_typed(var->offset, var->size);
    if (value->kind == ND_DEREF) {
      /* ND_DEREF の type_size を struct サイズに揃えて、codegen が
       * struct 全体を 1 ワードで load するようにする。 */
      ((node_mem_t *)value)->type_size = var->size;
    }
    node_mem_t *assign_node = psx_node_new_assign(lhs_var, value);
    assign_node->type_size = var->size;
    init_chain = (node_t *)assign_node;
  } else if (var->size > 8 && var->size <= 16 && value && value->kind == ND_FUNCALL) {
    // 9-16B struct: 関数呼び出し結果を x0/x1 ペアで受け取り、2ワード代入で初期化
    node_t *lhs_var = psx_node_new_lvar_typed(var->offset, var->size);
    node_mem_t *assign_node = psx_node_new_assign(lhs_var, value);
    assign_node->type_size = var->size;
    init_chain = (node_t *)assign_node;
  } else if (var->size > 16 && value && value->kind == ND_FUNCALL) {
    // >16B struct: indirect return (x8) 経由で呼び出し先が直接代入先に書き込む
    node_t *lhs_var = psx_node_new_lvar_typed(var->offset, var->size);
    node_mem_t *assign_node = psx_node_new_assign(lhs_var, value);
    assign_node->type_size = var->size;
    init_chain = (node_t *)assign_node;
  }
  return init_chain;  // 互換型でなければ NULL
}

/* value から先頭の ND_COMMA prefix を剥がす (`(a, b, structval)` の副作用部)。 */
static node_t *strip_comma_prefix(node_t *rhs, node_t **out_prefix) {
  node_t *prefix = NULL;
  node_t *value = rhs;
  while (value && value->kind == ND_COMMA) {
    prefix = prefix ? psx_node_new_binary(ND_COMMA, prefix, value->lhs) : value->lhs;
    value = value->rhs;
  }
  *out_prefix = prefix;
  return value;
}

static node_t *parse_struct_copy_initializer(lvar_t *var) {
  node_t *prefix = NULL;
  node_t *value = strip_comma_prefix(psx_expr_assign(), &prefix);
  node_t *init_chain = build_struct_copy_from_value(var, value);
  if (!init_chain) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_STRUCT_COPY_COMPAT_REQUIRED));
  }
  if (prefix) return psx_node_new_binary(ND_COMMA, prefix, init_chain);
  return init_chain;
}

/* 次の内側メンバ取り込みを継続して良いか (comma があり、その先が designator や
 * 終端でない)。継続するなら comma を消費して true。 */
static bool elision_consume_separator(void) {
  if (!curtok() || curtok()->kind != TK_COMMA) return false;
  token_t *nx = curtok()->next;
  /* 次が designator (`.m`) / 終端 (`}`) なら取り込み終了。comma は親が消費する。 */
  if (nx && (nx->kind == TK_DOT || nx->kind == TK_RBRACE)) return false;
  tk_consume(',');
  return true;
}

/* brace 省略 (純 scalar 始まり): 親のフラットリストから内側 struct の名前付き
 * メンバを宣言順に取り込む。各メンバを parse_member_initializer で処理するため
 * 入れ子集約 (`struct C{struct B{struct A a; int z;} b; int w;}` の `{1,2,3,4}`) も
 * 再帰的に展開される。 */
static node_t *struct_member_elision(lvar_t *nested) {
  int member_count = psx_ctx_get_tag_member_count(nested->tag_kind, nested->tag_name, nested->tag_len);
  node_t *chain = NULL;
  int ordinal = 0;
  while (ordinal < member_count) {
    if (chain && !elision_consume_separator()) break;
    tag_member_info_t mi = {0};
    if (!tag_get_next_named_member(nested, &ordinal, &mi) || mi.len <= 0) break;
    node_t *minit = parse_member_initializer(nested, mi.offset, mi.type_size,
                                             mi.tag_kind, mi.tag_name, mi.tag_len,
                                             mi.is_tag_pointer, mi.array_len, mi.outer_stride, mi.is_bool, mi.fp_kind,
                                             mi.arr_dims, mi.arr_ndim);
    node_t *as = wrap_member_init_as_assign(nested, &mi, minit);
    chain = chain ? psx_node_new_binary(ND_COMMA, chain, as) : as;
    if (curtok() && curtok()->kind == TK_RBRACE) break;
  }
  return chain;
}

/* `{` 無しの struct メンバ初期化。C11 6.7.9:
 *  - 互換 struct 式 (`m.i = innerVar` 等) ならメンバ単位 copy 初期化。
 *  - scalar から始まるなら brace 省略 (struct_member_elision)。
 * 識別子/`(` 始まりは copy の可能性があるので 1 式だけ先読みして判定する。
 * その他 (数値/文字/文字列/演算子) は純 scalar 省略とみなし先読みせず再帰展開する
 * (先頭メンバが集約でも parse_member_initializer が正しく消費するため)。 */
static node_t *parse_struct_member_no_brace(lvar_t *nested) {
  token_t *t = curtok();
  if (consume_terminal_zero_initializer()) return psx_node_new_num(0);
  if (!(t && (t->kind == TK_IDENT || t->kind == TK_LPAREN))) {
    return struct_member_elision(nested);
  }
  node_t *prefix = NULL;
  node_t *first = strip_comma_prefix(psx_expr_assign(), &prefix);
  node_t *copy = build_struct_copy_from_value(nested, first);
  if (copy) {
    return prefix ? psx_node_new_binary(ND_COMMA, prefix, copy) : copy;
  }
  /* 識別子だが互換 struct でない (scalar 変数等): 先読みした first を内側メンバ 0
   * (scalar 前提) に入れ、残りメンバを継続。 */
  int member_count = psx_ctx_get_tag_member_count(nested->tag_kind, nested->tag_name, nested->tag_len);
  tag_member_info_t info = {0};
  int ordinal = 0;
  if (!tag_get_next_named_member(nested, &ordinal, &info) || info.len <= 0) return first;
  node_t *lhs0 = new_struct_member_lvar(nested, info.offset, info.type_size,
                                        info.tag_kind, info.tag_name, info.tag_len,
                                        info.is_tag_pointer);
  node_mem_t *a0 = psx_node_new_assign(lhs0, first);
  a0->type_size = info.type_size;
  node_t *chain = prefix ? psx_node_new_binary(ND_COMMA, prefix, (node_t *)a0) : (node_t *)a0;
  while (ordinal < member_count) {
    if (!elision_consume_separator()) break;
    tag_member_info_t mi = {0};
    if (!tag_get_next_named_member(nested, &ordinal, &mi) || mi.len <= 0) break;
    node_t *minit = parse_member_initializer(nested, mi.offset, mi.type_size,
                                             mi.tag_kind, mi.tag_name, mi.tag_len,
                                             mi.is_tag_pointer, mi.array_len, mi.outer_stride, mi.is_bool, mi.fp_kind,
                                             mi.arr_dims, mi.arr_ndim);
    chain = psx_node_new_binary(ND_COMMA, chain, wrap_member_init_as_assign(nested, &mi, minit));
  }
  return chain;
}

// 波カッコなしの `union U u = expr;` 経路。
//  - 互換型からの copy 初期化を試みる
//  - そうでなければ scalar 値を最初の名前付きメンバへ代入
static node_t *parse_union_initializer_no_brace(lvar_t *var) {
  node_t *rhs = psx_expr_assign();
  node_t *prefix = NULL;
  node_lvar_t *src = NULL;
  if (resolve_copy_source_lvar(rhs, &prefix, &src)) {
    if (is_compatible_tag_object_lvar(src, var)) {
      node_t *copy = build_byte_copy_chain(var->offset, src->offset, var->size, NULL);
      if (prefix) return psx_node_new_binary(ND_COMMA, prefix, copy);
      return copy;
    }
  }
  // Fallback: scalar が先頭の名前付きメンバを初期化する。
  tag_member_info_t info = {0};
  info.tag_kind = TK_EOF;
  int ordinal = 0;
  bool found = tag_get_next_named_member(var, &ordinal, &info);
  if (!found || info.len <= 0) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND));
  }
  node_t *lhs = new_struct_member_lvar(var, info.offset, info.type_size,
                                       info.tag_kind, info.tag_name, info.tag_len, info.is_tag_pointer);
  node_mem_t *assign_node = psx_node_new_assign(lhs, rhs);
  assign_node->type_size = info.type_size;
  return (node_t *)assign_node;
}

static node_t *parse_union_initializer(lvar_t *var) {
  bool has_brace = tk_consume('{');
  if (has_brace && tk_consume('}')) return psx_node_new_num(0);
  if (!has_brace) return parse_union_initializer_no_brace(var);

  tag_member_info_t info = {0};
  info.tag_kind = TK_EOF;
  bool found = false;
  if (tk_consume('.')) {
    token_ident_t *id = tk_consume_ident();
    if (!id) psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_MEMBER_NAME));
    found = tag_find_member(var, id->str, id->len, &info);
    if (tk_consume('[')) {
      // Nested designator: .member[idx] = val
      if (!found || info.len <= 0) {
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND));
      }
      if (info.array_len <= 0 || info.is_tag_pointer) {
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_NESTED_DESIG_NOT_ARRAY));
      }
      int nested_idx = parse_nonneg_const_expr_decl(diag_text_for(DIAG_TEXT_ARRAY_DESIGNATOR_INDEX));
      tk_expect(']');
      tk_expect('=');
      if (nested_idx < 0 || nested_idx >= info.array_len) {
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
      }
      node_t *result = build_nested_array_designator_assign(var, &info, nested_idx);
      tk_consume(',');
      tk_expect('}');
      return result;
    }
    tk_expect('=');
  } else {
    int ordinal = 0;
    found = tag_get_next_named_member(var, &ordinal, &info);
  }
  if (!found || info.len <= 0) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND));
  }
  node_t *member_init = parse_member_initializer(var, info.offset, info.type_size,
                                                 info.tag_kind, info.tag_name, info.tag_len,
                                                 info.is_tag_pointer, info.array_len, info.outer_stride, info.is_bool, info.fp_kind,
                                                 info.arr_dims, info.arr_ndim);
  node_t *init_chain = wrap_member_init_as_assign(var, &info, member_init);
  if (!tk_consume(',')) {
    tk_expect('}');
    return init_chain;
  }
  if (tk_consume('}')) return init_chain;
  // 仕様外: `{ .a = 1, .b = 2 }` のように union に複数初期化子。
  // 各エントリは診断を出しつつパースを継続する。
  if (!tk_consume('.')) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_UNION_INIT_SINGLE_ELEMENT_ONLY));
  }
  for (;;) {
    token_ident_t *id = tk_consume_ident();
    if (!id) psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_MEMBER_NAME));
    tk_expect('=');
    found = tag_find_member(var, id->str, id->len, &info);
    if (!found || info.len <= 0) {
      psx_diag_ctx(curtok(), "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND));
    }
    node_t *extra_init = parse_member_initializer(var, info.offset, info.type_size,
                                                  info.tag_kind, info.tag_name, info.tag_len,
                                                  info.is_tag_pointer, info.array_len, info.outer_stride, info.is_bool, info.fp_kind,
                                                  info.arr_dims, info.arr_ndim);
    node_t *extra_assign = wrap_member_init_as_assign(var, &info, extra_init);
    init_chain = append_to_init_chain(init_chain, extra_assign);
    if (tk_consume('}')) return init_chain;
    tk_expect(',');
    if (!tk_consume('.')) {
      psx_diag_ctx(curtok(), "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_UNION_INIT_SINGLE_ELEMENT_ONLY));
    }
  }
}

/* 直近に skip_func_params が解析した関数(ポインタ)シグネチャが可変長か。
 * 宣言子の trailing `()` 解析ループ直前にリセットされ、解析後に登録側が読む
 * (`int (*f)(int, ...)` の経由呼び出しで variadic ABI を選ぶため)。 */
static int g_last_funcptr_is_variadic = 0;
static int g_last_funcptr_nfixed = 0;
/* 関数ポインタの各仮引数が float/double スカラかを 2bit ずつ (0=非fp / 1=float /
 * 2=double) で記録。`double (*fp)(double)` のような funcptr に int 実引数を渡すとき、
 * 経由呼び出しでも昇格できるようにする (最大 8 引数)。 */
static unsigned short g_last_funcptr_param_fp_mask = 0;
static unsigned short g_last_funcptr_param_int_mask = 0;

static void skip_func_params(void) {
  if (!tk_consume('(')) return;
  int depth = 1;
  int ncommas = 0;       /* depth==1 のカンマ数 */
  int saw_ellipsis = 0;
  int fixed_before_ellipsis = 0;
  unsigned short fp_mask = 0;
  unsigned short int_mask = 0;
  int param_idx = 0;     /* 現在の仮引数 index */
  int cur_fp = 0;        /* 0=未 / 1=float / 2=double */
  int cur_int = 0;       /* 0=未 / 1=4B / 2=8B / 3=pointer */
  int cur_disq = 0;      /* * / [ / ( を含む = スカラ fp でない */
  while (depth > 0) {
    token_kind_t k = curtok()->kind;
    if (k == TK_EOF) {
      psx_diag_ctx(curtok(), "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_MISSING_FUNC_DECL_RPAREN));
    }
    if (k == TK_LPAREN) {
      if (depth == 1) {
        cur_disq = 1;
        cur_int = 3;
      }
      depth++;
    }
    else if (k == TK_RPAREN) {
      depth--;
      if (depth == 0) {  /* 最後の仮引数を確定 */
        if (cur_fp && !cur_disq && param_idx < 8)
          fp_mask |= (unsigned short)(cur_fp << (2 * param_idx));
        if (cur_int && (!cur_disq || cur_int == 3) && param_idx < 8)
          int_mask |= (unsigned short)(cur_int << (2 * param_idx));
      }
    }
    else if (k == TK_COMMA && depth == 1) {
      ncommas++;
      if (cur_fp && !cur_disq && param_idx < 8)
        fp_mask |= (unsigned short)(cur_fp << (2 * param_idx));
      if (cur_int && (!cur_disq || cur_int == 3) && param_idx < 8)
        int_mask |= (unsigned short)(cur_int << (2 * param_idx));
      param_idx++; cur_fp = 0; cur_int = 0; cur_disq = 0;
    }
    else if (k == TK_ELLIPSIS && depth == 1) {
      saw_ellipsis = 1;
      fixed_before_ellipsis = ncommas;  /* `...` 前のカンマ数 = 固定引数数 */
    }
    else if (depth == 1) {
      if (k == TK_DOUBLE) cur_fp = 2;        /* double / long double */
      else if (k == TK_FLOAT) cur_fp = 1;
      else if (k == TK_LONG) cur_int = 2;
      else if (k == TK_INT || k == TK_CHAR || k == TK_SHORT ||
               k == TK_SIGNED || k == TK_UNSIGNED || k == TK_BOOL ||
               k == TK_ENUM) cur_int = 1;
      else if (k == TK_MUL || k == TK_LBRACKET) {
        cur_disq = 1;
        cur_int = 3;  /* pointer/array adjusted parameter */
      }
    }
    set_curtok(curtok()->next);
  }
  if (saw_ellipsis) {
    g_last_funcptr_is_variadic = 1;
    g_last_funcptr_nfixed = fixed_before_ellipsis;
  }
  g_last_funcptr_param_fp_mask = fp_mask;
  g_last_funcptr_param_int_mask = int_mask;
}

void psx_reset_funcptr_signature_state(void) {
  g_last_funcptr_is_variadic = 0;
  g_last_funcptr_nfixed = 0;
  g_last_funcptr_param_fp_mask = 0;
  g_last_funcptr_param_int_mask = 0;
}

void psx_skip_func_param_list(void) {
  skip_func_params();
}

int psx_last_funcptr_is_variadic(void) {
  return g_last_funcptr_is_variadic;
}

int psx_last_funcptr_nargs_fixed(void) {
  return g_last_funcptr_nfixed;
}

unsigned short psx_last_funcptr_param_fp_mask(void) {
  return g_last_funcptr_param_fp_mask;
}

unsigned short psx_last_funcptr_param_int_mask(void) {
  return g_last_funcptr_param_int_mask;
}

static void skip_bracket_group(void) {
  if (!tk_consume('[')) return;
  int depth = 1;
  while (depth > 0) {
    if (curtok()->kind == TK_EOF) {
      psx_diag_missing(curtok(), "]");
    }
    if (curtok()->kind == TK_LBRACKET) depth++;
    else if (curtok()->kind == TK_RBRACKET) depth--;
    set_curtok(curtok()->next);
  }
}

static global_var_t *find_global_var_decl(char *name, int len) {
  return psx_find_global_var(name, len);
}

static token_ident_t *consume_decl_name_recursive(int *is_pointer,
                                                  unsigned int *const_mask, unsigned int *volatile_mask,
                                                  int *levels, int *out_paren_array_mul,
                                                  int *had_parens,
                                                  int *out_inner_array_mul) {
  consume_pointer_chain_decl(is_pointer, const_mask, volatile_mask, levels);
  psx_skip_gnu_attributes();
  token_ident_t *tok = NULL;
  int local_had_parens = 0;
  if (tk_consume('(')) {
    local_had_parens = 1;
    psx_skip_gnu_attributes();
    tok = consume_decl_name_recursive(is_pointer, const_mask, volatile_mask, levels,
                                      out_paren_array_mul, NULL, out_inner_array_mul);
    // パレン内の `[N]` を捕捉する: `int (*ops[N])(...)` の N は関数ポインタ配列の要素数。
    // 空 `[]` の場合は -1 を伝え、呼び出し側で初期化子から推定させる。
    while (curtok()->kind == TK_LBRACKET) {
      if (out_inner_array_mul && tk_consume('[')) {
        bool empty_bracket = (curtok() && curtok()->kind == TK_RBRACKET);
        int n = 0;
        if (!empty_bracket) {
          n = parse_array_size_constexpr_decl();
        }
        tk_expect(']');
        if (empty_bracket) {
          *out_inner_array_mul = -1; // size unspecified
        } else if (n > 0) {
          if (*out_inner_array_mul == 0) *out_inner_array_mul = 1;
          if (*out_inner_array_mul > 0) *out_inner_array_mul *= n;
          /* 個別次元を記録 (2 次元以上の funcptr 配列 `int(*t[2][2])(void)` のストライド用)。 */
          if (g_inner_array_dim_count < 8) g_inner_array_dims[g_inner_array_dim_count] = n;
          g_inner_array_dim_count++;
        }
      } else {
        skip_bracket_group();
      }
    }
    tk_expect(')');
  } else {
    tok = tk_consume_ident();
  }
  /* この宣言子の trailing `()` を解析する前にリセット。最外フレームの本ループが
   * シグネチャ `(int, ...)` を最後に解析するので、その結果が登録側に届く。 */
  g_last_funcptr_is_variadic = 0;
  g_last_funcptr_nfixed = 0;
  g_last_funcptr_param_fp_mask = 0;
  g_last_funcptr_param_int_mask = 0;
  while (curtok()->kind == TK_LPAREN) {
    skip_func_params();
    /* 関数シグネチャを 1 つでも消費したら trailing-func-suffix を立てる。
     * `int (*(*pa)[N])(args)` で要素が関数ポインタの場合、後段の `(*p)[N]` 登録経路で
     * elem_size を 8 に上書きするのに使う。 */
    g_decl_trailing_func_suffix = 1;
    g_decl_func_suffix_count++;
  }
  if (local_had_parens) g_decl_had_paren_group = 1;
  /* paren-grouped 宣言子 `(*p)...` の後ろに `[N]` が続くのは `int (*p)[N]`
   * (配列へのポインタ) 専用形式。`[` が来ていないなら paren_array_mul を立てない
   * (parse_decl_array_suffixes_constexpr_required は初期値 1 をそのまま返すため、
   * 立ててしまうと `int (**pp)(int)` 等が `(*p)[N]` 分岐で誤登録される)。 */
  if (local_had_parens && out_paren_array_mul && curtok()->kind == TK_LBRACKET) {
    /* 多次元 inner (`(*p)[N][M]`) の mid_stride 計算のため先頭次元と次元数を捕捉する。
     * arr_total は全次元の積で従来の paren_array_mul と一致。
     * 非定数次元 (`int (*p)[m]`, VLA) は g_paren_array_vla_dim に式として捕捉し、
     * registration が pointer-to-VLA として行ストライドを実行時計算する。 */
    g_paren_array_vla_dim = NULL;
    decl_array_suffix_t pa = parse_decl_array_suffixes_ex(1, &g_paren_array_vla_dim);
    *out_paren_array_mul = pa.arr_total > 0 ? pa.arr_total : 1;
    g_paren_array_first_dim = pa.first_dim;
    g_paren_array_second_dim = (pa.dim_count >= 2) ? pa.dims[1] : 0;
    g_paren_array_dim_count = pa.dim_count;
  }
  if (had_parens) *had_parens = local_had_parens;
  return tok;
}

static token_ident_t *consume_decl_name_ex(int *is_pointer,
                                           unsigned int *const_mask, unsigned int *volatile_mask,
                                           int *levels, int *out_paren_array_mul,
                                           int *out_inner_array_mul) {
  token_ident_t *tok = consume_decl_name_recursive(is_pointer, const_mask, volatile_mask,
                                                   levels, out_paren_array_mul, NULL,
                                                   out_inner_array_mul);
  if (!tok) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED));
  }
  return tok;
}

static token_ident_t *consume_decl_name(int *is_pointer,
                                        unsigned int *const_mask, unsigned int *volatile_mask,
                                        int *levels, int *out_paren_array_mul) {
  token_ident_t *tok = consume_decl_name_recursive(is_pointer, const_mask, volatile_mask,
                                                   levels, out_paren_array_mul, NULL, NULL);
  if (!tok) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED));
  }
  return tok;
}

void psx_decl_reset_locals(void) {
  locals = NULL;
  all_locals = NULL;
  locals_offset = 0;
  lvar_scope_depth = 0;
  for (unsigned i = 0; i < LVAR_HASH_BUCKETS; i++) {
    lvars_by_bucket[i] = NULL;
    lvars_by_offset[i] = NULL;
  }
  g_lvar_scope_seq = 0;
  cur_lvar_scope_seq = 0;
  g_var_typesig_count = 0;   /* _Generic 用の型シグネチャ表も関数境界でリセット */
  g_decl_typespec_start = NULL;
}

void psx_decl_enter_scope(void) {
  if (lvar_scope_depth < LVAR_SCOPE_STACK_MAX) {
    lvar_scope_stack[lvar_scope_depth] = locals;
    lvar_scope_seq_stack[lvar_scope_depth] = cur_lvar_scope_seq;
  }
  lvar_scope_depth++;
  cur_lvar_scope_seq = ++g_lvar_scope_seq;
}

void psx_decl_leave_scope(void) {
  if (lvar_scope_depth <= 0) return;
  lvar_scope_depth--;
  if (lvar_scope_depth < LVAR_SCOPE_STACK_MAX) {
    /* このスコープで追加された変数 (locals head .. 退避点) を名前索引から外す。 */
    lvar_t *restore = lvar_scope_stack[lvar_scope_depth];
    for (lvar_t *v = locals; v != restore; v = v->next) lvar_index_on_remove(v);
    locals = restore;
    cur_lvar_scope_seq = lvar_scope_seq_stack[lvar_scope_depth];
  }
}

// For variadic functions: reserve slots for all 8 argument registers
// (8 regs × 8 bytes = 64 bytes at offsets 8..64) so that body-local
// variables don't overlap with the variadic register save area.
void psx_decl_reserve_variadic_regs(void) {
  if (locals_offset < 64) locals_offset = 64;
}

lvar_t *psx_decl_get_locals(void) { return all_locals; }

lvar_t *psx_decl_find_lvar_by_offset(int offset) {
  /* offset bucket は all_locals と同じく MRU 順なので、最初の offset 一致が
   * 旧 all_locals 線形走査と同じ変数になる (offset 重複時の挙動も一致)。 */
  unsigned oh = lvar_offset_hash(offset);
  for (lvar_t *var = lvars_by_offset[oh]; var; var = var->next_offhash) {
    if (var->offset == offset) return var;
  }
  return NULL;
}

lvar_t *psx_decl_find_lvar(char *name, int len) {
  /* 名前 bucket は MRU 順 (内側スコープが先頭) かつ可視な変数のみ。
   * 最初の名前一致が `locals` 線形走査と同じ「最も内側の見える変数」になる。 */
  unsigned h = lvar_name_hash(name, len);
  for (lvar_t *var = lvars_by_bucket[h]; var; var = var->next_hash) {
    if (var->len == len && memcmp(var->name, name, len) == 0) {
      return var;
    }
  }
  return NULL;
}

lvar_t *psx_decl_register_lvar_sized(char *name, int len, int size, int elem_size, int is_array) {
  return psx_decl_register_lvar_sized_align(name, len, size, elem_size, is_array, 0);
}

lvar_t *psx_decl_register_lvar_sized_align(char *name, int len, int size, int elem_size, int is_array, int align) {
  /* C11 6.7p3: 同一スコープで同名のオブジェクト/関数を重複宣言してはならない。
   * 名前索引で最も内側の同名変数を引き、それが現在スコープ (= 同じ scope_seq) の
   * ものなら重複。外側スコープのものはシャドーイングなので許可。 */
  lvar_t *prev = psx_decl_find_lvar(name, len);
  if (prev && prev->scope_seq == cur_lvar_scope_seq) {
    psx_diag_duplicate_with_name(curtok(), "variable", name, len);
    /* psx_diag_duplicate_with_name は exit するため後続には到達しない */
  }

  lvar_t *var = calloc(1, sizeof(lvar_t));
  var->next = locals;
  var->next_all = all_locals;
  all_locals = var;
  var->name = name;
  var->len = len;
  if (align > 1) {
    locals_offset = (locals_offset + align - 1) & ~(align - 1);
  }
  var->offset = locals_offset;  // BASE of variable (address = x29 + 16 + var->offset)
  locals_offset += size;
  var->size = size;
  var->elem_size = elem_size;
  var->is_array = is_array;
  var->align_bytes = align;
  locals = var;
  lvar_index_on_add(var);
  return var;
}

lvar_t *psx_decl_register_lvar(char *name, int len) {
  return psx_decl_register_lvar_sized(name, len, 8, 8, 0);
}

node_t *psx_decl_parse_initializer_for_var(lvar_t *var, int is_pointer) {
  if (var->is_array) {
    return parse_array_initializer(var);
  }
  if (!is_pointer && var->tag_kind == TK_STRUCT) {
    if (curtok()->kind != TK_LBRACE) {
      return parse_struct_copy_initializer(var);
    }
    return parse_struct_initializer(var);
  }
  if (!is_pointer && var->tag_kind == TK_UNION) {
    return parse_union_initializer(var);
  }
  node_t *lvar = psx_node_new_lvar_typed(var->offset, is_pointer ? 8 : var->elem_size);
  lvar->fp_kind = var->fp_kind;
  ((node_lvar_t *)lvar)->mem.tag_kind = var->tag_kind;
  ((node_lvar_t *)lvar)->mem.tag_name = var->tag_name;
  ((node_lvar_t *)lvar)->mem.tag_len = var->tag_len;
  ((node_lvar_t *)lvar)->mem.is_tag_pointer = var->is_tag_pointer;
  ((node_lvar_t *)lvar)->mem.is_complex = var->is_complex;
  ((node_lvar_t *)lvar)->mem.is_atomic = var->is_atomic;
  ((node_lvar_t *)lvar)->mem.is_unsigned = var->is_unsigned ? 1 : 0;
  ((node_lvar_t *)lvar)->mem.pointee_is_unsigned = var->is_unsigned ? 1 : 0;
  lvar->is_complex = var->is_complex;
  lvar->is_atomic = var->is_atomic;
  ((node_lvar_t *)lvar)->mem.is_const_qualified = var->is_const_qualified;
  ((node_lvar_t *)lvar)->mem.is_volatile_qualified = var->is_volatile_qualified;
  ((node_lvar_t *)lvar)->mem.is_pointer_const_qualified = var->is_pointer_const_qualified;
  ((node_lvar_t *)lvar)->mem.is_pointer_volatile_qualified = var->is_pointer_volatile_qualified;
  ((node_lvar_t *)lvar)->mem.pointer_const_qual_mask = var->pointer_const_qual_mask;
  ((node_lvar_t *)lvar)->mem.pointer_volatile_qual_mask = var->pointer_volatile_qual_mask;
  ((node_lvar_t *)lvar)->mem.pointer_qual_levels = var->pointer_qual_levels;
  /* `_Complex z = {re, im}` 初期化。複素数は {実部, 虚部} の連続レイアウト
   * (double _Complex は 8+8、float _Complex は 4+4) なので、実部スロット (offset) と
   * 虚部スロット (offset+half) へそれぞれ fp スカラ store を生成する (既存の fp 代入を
   * 再利用)。im を省略した `{re}` は虚部 0。これがないとスカラ初期化子扱いで
   * `{0,1}` が E3064 になり、虚数単位 `I` (= {0,1}) を定義できなかった。 */
  if (var->is_complex && curtok()->kind == TK_LBRACE) {
    tk_consume('{');
    int half = var->elem_size > 0 ? var->elem_size / 2 : 8;
    node_t *re = psx_expr_assign();
    node_t *im = NULL;
    if (tk_consume(',') && curtok()->kind != TK_RBRACE) im = psx_expr_assign();
    tk_expect('}');
    node_t *re_lv = psx_node_new_lvar_typed(var->offset, half);
    re_lv->fp_kind = var->fp_kind;
    node_mem_t *re_as = psx_node_new_assign(re_lv, re);
    re_as->base.fp_kind = var->fp_kind;
    re_as->type_size = half;
    node_t *im_lv = psx_node_new_lvar_typed(var->offset + half, half);
    im_lv->fp_kind = var->fp_kind;
    node_mem_t *im_as = psx_node_new_assign(im_lv, im ? im : psx_node_new_num(0));
    im_as->base.fp_kind = var->fp_kind;
    im_as->type_size = half;
    return psx_node_new_binary(ND_COMMA, (node_t *)re_as, (node_t *)im_as);
  }
  node_t *init_expr = parse_scalar_brace_initializer();
  /* C11 6.3.1.2: _Bool への変換は (x != 0) を 0/1 で表現したもの。
   * `_Bool b = 42;` を 1 にするため、ND_NE で wrap してから store する。 */
  if (var->is_bool && init_expr) {
    init_expr = psx_node_new_binary(ND_NE, init_expr, psx_node_new_num(0));
  }
  if (is_pointer) {
    psx_node_reject_const_qual_discard(lvar, init_expr);
    /* C11 6.5.16.1: ポインタ変数を非ゼロ整数定数で初期化するのは制約違反。
     * NULL ポインタ定数 (整数 0) のみ例外として許可する。
     * 例外: `(void*)0xdeadbeefL` のように明示キャスト経由でポインタ型に変換された
     * 整数定数 (apply_cast が num->from_pointer_cast=1 を立てる) は許容。 */
    if (init_expr && init_expr->kind == ND_NUM) {
      node_num_t *num = (node_num_t *)init_expr;
      if (num->val != 0 && !num->from_pointer_cast) {
        psx_diag_ctx(curtok(), "init",
                     "ポインタ変数を非ゼロ整数定数 (%lld) で初期化できません (C11 6.5.16.1)",
                     num->val);
      }
    }
  } else if (var->tag_kind == TK_EOF && !var->is_array && init_expr) {
    /* C11 6.5.16.1: スカラ非ポインタ変数を文字列リテラル (char*) など
     * ポインタ型で初期化するのは互換性のない型の制約違反。
     * 明示キャスト (int)"hello" は apply_cast で is_pointer がクリアされるので
     * ここでは ps_node_is_pointer を見て暗黙変換のみを検出する。 */
    if (ps_node_is_pointer(init_expr)) {
      psx_diag_ctx(curtok(), "init",
                   "スカラ変数をポインタ型で初期化できません (C11 6.5.16.1)");
    }
    /* 縮小変換 (narrowing) warning: 整数変数を浮動小数点値で初期化。
     * - `int x = 1.5;` のような小数値リテラル: clang -Wliteral-conversion 相当
     * - `int z = d;` のような double 変数からの初期化: clang -Wfloat-conversion 相当
     * `int y = 2.0;` (整数値リテラル) は値が変わらないため除外、明示キャスト
     * `int x = (int)d;` も apply_cast 後の fp_kind が NONE になるため除外。 */
    if (var->fp_kind == TK_FLOAT_KIND_NONE &&
        init_expr->fp_kind != TK_FLOAT_KIND_NONE) {
      if (init_expr->kind == ND_NUM) {
        double f = ((node_num_t *)init_expr)->fval;
        if (f != (double)(long long)f) {
          diag_warn_tokf(DIAG_WARN_PARSER_FLOAT_TO_INT_NARROWING, NULL,
                         "整数変数を浮動小数点リテラル %g で初期化しています (小数部が切り捨てられます)",
                         f);
        }
      } else {
        diag_warn_tokf(DIAG_WARN_PARSER_FLOAT_TO_INT_NARROWING, NULL,
                       "整数変数を浮動小数点値で初期化しています (小数部が切り捨てられます)");
      }
    }
    /* 整数リテラルの範囲オーバー: `char c = 300;`、`short s = 70000;` 等
     * (clang -Wconstant-conversion 相当)。signed/unsigned で範囲が違うため var の符号性
     * (is_unsigned) を見て判定する。値が型に収まらない場合に warning。
     * `unsigned char uc = -1;` は意図的な「全ビット 1」(=255) のイディオムなので除外
     * (clang も -Wconstant-conversion では出さない)。`int x = ...` (4 バイト) は値域が広く
     * 範囲外リテラルは tokenizer で long に昇格されるためこの check に乗らない。 */
    if (var->fp_kind == TK_FLOAT_KIND_NONE && init_expr->kind == ND_NUM &&
        init_expr->fp_kind == TK_FLOAT_KIND_NONE && var->elem_size > 0 &&
        var->elem_size < 4) {
      long long v = ((node_num_t *)init_expr)->val;
      int bits = var->elem_size * 8;
      long long max_signed = (1LL << (bits - 1)) - 1;
      long long min_signed = -(1LL << (bits - 1));
      long long max_unsigned = (1LL << bits) - 1;
      int out_of_range;
      if (var->is_unsigned) {
        out_of_range = (v < 0 || v > max_unsigned);
        /* -1 / 0xFF パターンは意図的なので除外 (signed-rep の負値そのもの) */
        if (v < 0 && v >= min_signed) out_of_range = 0;
      } else {
        out_of_range = (v < min_signed || v > max_signed);
      }
      if (out_of_range) {
        diag_warn_tokf(DIAG_WARN_PARSER_CONSTANT_OVERFLOW, NULL,
                       "整数リテラル %lld は %d バイト型に収まりません (値が切り詰められます)",
                       v, var->elem_size);
      }
    }
    /* C11 6.5.16.1: struct/union 値をスカラに代入することはできない。
     * RHS の node_mem_t::tag_kind が TK_STRUCT/TK_UNION かつ is_tag_pointer=0
     * の場合、構造体実体を整数に変換しようとしているので拒否する。 */
    if ((init_expr->kind == ND_LVAR || init_expr->kind == ND_GVAR ||
         init_expr->kind == ND_DEREF || init_expr->kind == ND_FUNCALL) &&
        (init_expr->kind != ND_FUNCALL)) {
      node_mem_t *m = (node_mem_t *)init_expr;
      if ((m->tag_kind == TK_STRUCT || m->tag_kind == TK_UNION) &&
          !m->is_tag_pointer && !m->is_pointer) {
        psx_diag_ctx(curtok(), "init",
                     "スカラ変数を %s 値で初期化できません (C11 6.5.16.1)",
                     m->tag_kind == TK_STRUCT ? "struct" : "union");
      }
    }
  }
  node_mem_t *assign_node = psx_node_new_assign(lvar, init_expr);
  assign_node->type_size = is_pointer ? 8 : var->elem_size;
  assign_node->base.fp_kind = var->fp_kind;
  return (node_t *)assign_node;
}

node_t *psx_decl_parse_declaration_after_type(int elem_size, tk_float_kind_t decl_fp_kind,
                                              token_kind_t tag_kind, char *tag_name, int tag_len,
                                              int base_is_pointer,
                                              int is_const_qualified, int is_volatile_qualified,
                                              int decl_is_unsigned_hint) {
  return psx_decl_parse_declaration_after_type_ex(elem_size, decl_fp_kind,
                                                  tag_kind, tag_name, tag_len,
                                                  base_is_pointer,
                                                  is_const_qualified, is_volatile_qualified,
                                                  decl_is_unsigned_hint, NULL, 0,
                                                  /* decl_base_is_void = */ 0,
                                                  /* decl_base_is_bool = */ 0);
}

/* `static int n = 5;` のような単純スカラ static ローカルをグローバルに lowering する。
 * 戻り値: 1 = 処理した (登録 + alias 作成済)、0 = 非対応形式なので呼び出し側で fallback。
 * 対応範囲: スカラ整数 / 浮動小数点 / ポインタ。`=` の右辺は数値定数、
 * またはポインタ用のアドレス定数 (`&g` / 関数参照 / 文字列リテラル等)。
 * 配列・struct・union・複合型は未対応。 */
static int try_lower_static_local_scalar(token_ident_t *tok, int var_size, int deref_size,
                                          tk_float_kind_t fp_kind, int is_unsigned,
                                          int is_pointer,
                                          unsigned short funcptr_param_fp_mask,
                                          unsigned short funcptr_param_int_mask,
                                          unsigned char funcptr_ret_int_width,
                                          int funcptr_ret_is_void,
                                          int funcptr_ret_is_complex,
                                          int is_variadic_funcptr,
                                          short funcptr_nargs_fixed) {
  if (var_size <= 0) return 0;
  /* peek フェーズ: 受理できる init 形 (なし、`=` + 単純な数値リテラル、
   * または pointer のアドレス定数式) のみ scalar 経路で処理する。 */
  int init_is_addr = 0;
  {
    token_t *p = curtok();
    if (p && p->kind == TK_ASSIGN) {
      token_t *a = p->next;
      if (!a) return 0;
      /* 数値リテラル (`=5`) または符号付き数値リテラル (`= -5`, `= +5`) のみ受理。
       * 文字列・複合リテラル・識別子参照・関数呼び出し等は fallback。 */
      token_t *num_tok = a;
      if (a->kind == TK_MINUS || a->kind == TK_PLUS) num_tok = a->next;
      token_t *tail = NULL;
      if (num_tok && num_tok->kind == TK_NUM) {
        tail = num_tok->next;
      } else if (is_pointer) {
        int depth = 0;
        for (token_t *q = a; q; q = q->next) {
          if (q->kind == TK_LPAREN || q->kind == TK_LBRACKET || q->kind == TK_LBRACE) depth++;
          else if (q->kind == TK_RPAREN || q->kind == TK_RBRACKET || q->kind == TK_RBRACE) {
            if (depth > 0) depth--;
          } else if (depth == 0 && (q->kind == TK_SEMI || q->kind == TK_COMMA)) {
            tail = q;
            break;
          }
        }
        if (!tail) return 0;
        init_is_addr = 1;
      } else {
        return 0;
      }
      if (!tail || (tail->kind != TK_SEMI && tail->kind != TK_COMMA)) return 0;
    }
  }
  /* mangled name: `<funcname>.<varname>.<seq>` を arena に組み立てる。seq は重複防止用。 */
  static int g_static_seq = 0;
  char *funcname = NULL;
  int funcname_len = 0;
  psx_expr_get_current_funcname(&funcname, &funcname_len);
  int seq = g_static_seq++;
  char seq_buf[12];
  int seq_len = snprintf(seq_buf, sizeof(seq_buf), "%d", seq);
  /* funcname (or "anon") + "." + tok->name + "." + seq */
  const char *fname = funcname && funcname_len > 0 ? funcname : "anon";
  int fname_len = funcname && funcname_len > 0 ? funcname_len : 4;
  int total_len = fname_len + 1 + tok->len + 1 + seq_len;
  /* 永続: static local を lowering したグローバルの名前で global_var_t->name が
   * 参照する (コンパイル末尾の gen_global_vars まで生存)。関数ごとに AST arena を
   * リセットしても消えないよう、arena でなく malloc で確保する。 */
  char *mangled = malloc((size_t)total_len + 1);
  int off = 0;
  memcpy(mangled + off, fname, (size_t)fname_len); off += fname_len;
  mangled[off++] = '.';
  memcpy(mangled + off, tok->str, (size_t)tok->len); off += tok->len;
  mangled[off++] = '.';
  memcpy(mangled + off, seq_buf, (size_t)seq_len); off += seq_len;
  mangled[off] = '\0';

  /* 初期化子 (`= N`) があれば NUM をパースして init_val に取り込む。
   * 整数式は psx_decl_eval_const_int で folding (`= -5` は ND_SUB(0,5) として
   * パースされるため、ND_NUM だけ見ると 0 に化けていた)。
   * float/double の static ローカルはリテラル値が ->fval にあるので、ND_NUM のときに
   * fval を取り出す。 */
  long long init_val = 0;
  double init_fval = 0;
  char *init_symbol = NULL;
  int init_symbol_len = 0;
  long long init_symbol_offset = 0;
  int has_init = 0;
  if (tk_consume('=')) {
    node_t *e = psx_expr_assign();
    if (init_is_addr) {
      long long off = 0;
      char *sym = NULL;
      int sym_len = 0;
      if (!psx_resolve_global_addr_init(e, &sym, &sym_len, &off)) {
        psx_diag_ctx(curtok(), "decl",
                     "static local pointer initializer must be an address constant");
      }
      init_symbol = sym;
      init_symbol_len = sym_len;
      init_symbol_offset = off;
      has_init = 1;
    } else if (fp_kind != TK_FLOAT_KIND_NONE) {
      if (e && e->kind == ND_NUM) init_fval = ((node_num_t *)e)->fval;
      has_init = 1;
    } else {
      int ok = 1;
      long long v = psx_decl_eval_const_int(e, &ok);
      if (ok) init_val = v;
      has_init = 1;
    }
  }

  /* global_var_t を作って global_vars に追加。 */
  global_var_t *gv = calloc(1, sizeof(global_var_t));
  gv->is_static = 1;  /* 関数内 static は内部リンケージ: .global を出さない (別 TU と衝突しない)。 */
  gv->name = mangled;
  gv->name_len = total_len;
  gv->type_size = var_size;
  gv->deref_size = (short)deref_size;
  gv->has_init = has_init;
  gv->init_val = init_val;
  gv->init_symbol = init_symbol;
  gv->init_symbol_len = init_symbol_len;
  gv->init_symbol_offset = init_symbol_offset;
  gv->fp_kind = (unsigned char)fp_kind;
  gv->fval = init_fval;
  if (is_pointer) {
    gv->funcptr_param_fp_mask = funcptr_param_fp_mask;
    gv->funcptr_param_int_mask = funcptr_param_int_mask;
    gv->funcptr_ret_int_width = funcptr_ret_int_width;
    gv->funcptr_ret_is_void = funcptr_ret_is_void ? 1 : 0;
    gv->funcptr_ret_is_complex = funcptr_ret_is_complex ? 1 : 0;
    gv->is_variadic_funcptr = is_variadic_funcptr ? 1 : 0;
    gv->funcptr_nargs_fixed = funcptr_nargs_fixed;
  }
  psx_register_global_var(gv);

  /* lvar を「alias」として登録 — frame には置かないが、short name で引けるよう
   * locals に挿入する。is_static_local を立てて、識別子解決時に ND_GVAR に
   * 切り替える。size=0、offset は意味を持たない (=0)。 */
  lvar_t *var = calloc(1, sizeof(lvar_t));
  var->next = locals;
  var->next_all = all_locals;
  all_locals = var;
  var->name = tok->str;
  var->len = tok->len;
  var->offset = 0;
  var->size = var_size;
  var->elem_size = deref_size;
  var->fp_kind = fp_kind;
  var->is_unsigned = is_unsigned ? 1 : 0;
  var->is_static_local = 1;
  var->is_initialized = 1;
  if (is_pointer) {
    var->funcptr_param_fp_mask = funcptr_param_fp_mask;
    var->funcptr_param_int_mask = funcptr_param_int_mask;
    var->funcptr_ret_int_width = funcptr_ret_int_width;
    var->funcptr_ret_is_void = funcptr_ret_is_void ? 1 : 0;
    var->funcptr_ret_is_complex = funcptr_ret_is_complex ? 1 : 0;
    var->is_variadic_funcptr = is_variadic_funcptr ? 1 : 0;
    var->funcptr_nargs_fixed = funcptr_nargs_fixed;
  }
  var->static_global_name = mangled;
  var->static_global_name_len = total_len;
  locals = var;
  lvar_index_on_add(var);
  return 1;
}

static int try_lower_static_local_array_consumed(token_ident_t *tok, int elem_size,
                                                 tk_float_kind_t fp_kind,
                                                 int array_count, int is_unsigned,
                                                 int pointer_elem_pointee_size);

/* `static int t[N] = {...};` / `static char s[] = "..."` の 1D scalar 配列 static local をグローバルに lowering する。
 * スコープ: 1D scalar 配列 (`int/long/short/char [unsigned]` / float/double)、ゼロ初期化 or brace init。
 * 文字列リテラル init は要素幅と文字列幅が一致する場合だけ扱う。struct / 関数ポインタは
 * fallback (0 を返す)。
 * 0 を返すときは curtok を変えない (呼び出し側の auto array 経路に流す)。
 *
 * 前提:
 *   - curtok() == TK_LBRACKET ('[' の直前)
 *   - tag_kind == TK_EOF、is_pointer == 0
 *   - 多次元・struct 等は呼び出し側ゲートで除外済み */
static int try_lower_static_local_array(token_ident_t *tok, int elem_size,
                                         tk_float_kind_t fp_kind,
                                         int is_unsigned,
                                         int pointer_elem_pointee_size) {
  if (elem_size <= 0) return 0;
  /* --- peek フェーズ: curtok 不変で scope 内/外を判定する。--- */
  token_t *p = curtok();
  if (!p || p->kind != TK_LBRACKET) return 0;
  /* `[` の次は数値リテラル or `]` (要素数推定) のみ受け付ける。複雑な式は fallback。 */
  token_t *after_lb = p->next;
  if (!after_lb) return 0;
  int has_size_token = 0;
  long long arr_count = 0;
  token_t *after_rb = NULL;
  if (after_lb->kind == TK_RBRACKET) {
    after_rb = after_lb->next;
  } else if (after_lb->kind == TK_NUM) {
    /* num の次がすぐ `]` であること。`[3+1]` などは fallback。 */
    token_t *after_num = after_lb->next;
    if (!after_num || after_num->kind != TK_RBRACKET) return 0;
    has_size_token = 1;
    /* TK_NUM は num_kind で int/float 分岐。配列サイズは整数のみ受け付ける。 */
    if (((token_num_t *)after_lb)->num_kind != TK_NUM_KIND_INT) return 0;
    arr_count = ((token_num_int_t *)after_lb)->val;
    if (arr_count <= 0) return 0;
    after_rb = after_num->next;
  } else {
    return 0;
  }
  if (!after_rb) return 0;
  if (after_rb->kind == TK_LBRACKET) {
    if (!has_size_token) return 0;
    int dims[8] = {0};
    int dim_count = 1;
    long long total_count = arr_count;
    dims[0] = (int)arr_count;
    token_t *q = after_rb;
    while (q && q->kind == TK_LBRACKET) {
      if (dim_count >= 8) return 0;
      token_t *num = q->next;
      if (!num || num->kind != TK_NUM ||
          ((token_num_t *)num)->num_kind != TK_NUM_KIND_INT) {
        return 0;
      }
      token_t *rb = num->next;
      if (!rb || rb->kind != TK_RBRACKET) return 0;
      long long dim = ((token_num_int_t *)num)->val;
      if (dim <= 0) return 0;
      dims[dim_count++] = (int)dim;
      total_count *= dim;
      q = rb->next;
    }
    if (!q) return 0;
    if (q->kind == TK_ASSIGN) {
      token_t *after_eq = q->next;
      if (!after_eq || after_eq->kind != TK_LBRACE) return 0;
    } else if (q->kind != TK_COMMA && q->kind != TK_SEMI) {
      return 0;
    }

    for (int i = 0; i < dim_count; i++) {
      tk_expect('[');
      set_curtok(curtok()->next); /* skip NUM */
      tk_expect(']');
    }
    g_inner_array_dim_count = dim_count;
    for (int i = 0; i < 8; i++) g_inner_array_dims[i] = i < dim_count ? dims[i] : 0;
    return try_lower_static_local_array_consumed(tok, elem_size, fp_kind,
                                                 (int)total_count, is_unsigned,
                                                 pointer_elem_pointee_size);
  }
  /* `=` の後の形を peek。`{`= brace OK、`TK_STRING`=文字列 init、その他は fallback。 */
  int has_init = 0;
  int has_string_init = 0;
  if (after_rb->kind == TK_ASSIGN) {
    token_t *after_eq = after_rb->next;
    if (!after_eq) return 0;
    if (after_eq->kind == TK_STRING) {
      int cw = (int)((token_string_t *)after_eq)->char_width;
      if (cw <= 0) cw = 1;
      if (elem_size != cw) return 0;
      if (!has_size_token) {
        arr_count = infer_array_count_from_initializer_at(after_rb, elem_size);
        if (arr_count <= 0) return 0;
      }
      has_string_init = 1;
    } else if (after_eq->kind != TK_LBRACE) {
      return 0; /* スカラ式は fallback */
    }
    has_init = 1;
  } else if (after_rb->kind == TK_COMMA || after_rb->kind == TK_SEMI) {
    has_init = 0;
  } else {
    return 0;
  }

  /* --- 実処理フェーズ。--- */
  tk_expect('[');
  if (has_size_token) {
    set_curtok(curtok()->next); /* skip NUM */
  }
  tk_expect(']');

  /* global_var_t を構築。 */
  global_var_t *gv = calloc(1, sizeof(global_var_t));
  gv->is_static = 1;  /* 関数内 static は内部リンケージ: .global を出さない。 */
  gv->deref_size = (short)elem_size;
  gv->is_array = 1;
  gv->tag_kind = TK_EOF;
  gv->fp_kind = (unsigned char)fp_kind;
  if (has_size_token || (has_string_init && arr_count > 0)) {
    gv->type_size = (int)arr_count * elem_size;
  } else {
    gv->type_size = 0; /* brace 後に確定 */
  }

  if (has_init && has_string_init) {
    tk_expect('=');
    node_t *rhs = psx_expr_assign();
    if (!rhs || rhs->kind != ND_STRING) return 0;
    node_string_t *s = (node_string_t *)rhs;
    string_lit_t *lit = find_string_lit_by_label(s->string_label);
    if (!lit) {
      psx_diag_ctx(curtok(), "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_STRING_INIT_RESOLVE_FAILED));
    }
    gv->has_init = 1;
    gv->init_values = calloc((size_t)arr_count, sizeof(long long));
    gv->init_value_symbols = calloc((size_t)arr_count, sizeof(char *));
    gv->init_value_symbol_lens = calloc((size_t)arr_count, sizeof(int));
    gv->init_union_ordinals = malloc((size_t)arr_count * sizeof(int));
    for (int i = 0; i < arr_count; i++) gv->init_union_ordinals[i] = -1;
    if (fp_kind != TK_FLOAT_KIND_NONE) {
      gv->init_fvalues = calloc((size_t)arr_count, sizeof(double));
    }
    int idx = 0;
    int src_pos = 0;
    while (src_pos < lit->len && idx < arr_count) {
      uint32_t units[2];
      int nu = tk_next_string_code_units(lit->str, lit->len, &src_pos, elem_size, units);
      for (int k = 0; k < nu && idx < arr_count; k++) {
        gv->init_values[idx++] = (long long)units[k];
      }
    }
    if (idx < arr_count) gv->init_values[idx++] = 0;
    gv->init_count = idx;
  } else if (has_init) {
    tk_expect('=');
    gv->has_init = 1;
    int cap = 16;
    gv->init_values = calloc((size_t)cap, sizeof(long long));
    gv->init_value_symbols = calloc((size_t)cap, sizeof(char *));
    gv->init_value_symbol_lens = calloc((size_t)cap, sizeof(int));
    gv->init_union_ordinals = malloc((size_t)cap * sizeof(int));
    for (int i = 0; i < cap; i++) gv->init_union_ordinals[i] = -1;
    if (fp_kind != TK_FLOAT_KIND_NONE) {
      gv->init_fvalues = calloc((size_t)cap, sizeof(double));
    }
    gv->init_count = 0;
    psx_parse_global_brace_init_flat(gv, &cap, -1);
    if (gv->type_size == 0 && gv->init_count > 0) {
      gv->type_size = gv->init_count * elem_size;
    }
  }
  if (gv->type_size == 0) {
    /* サイズが確定できないケース (`[];` で init もなし) は scope 外として
     * 受け付けたくない — gv を破棄して呼び出し側 fallback に戻したいが、
     * curtok は既に進めてしまっているため戻せない。診断を出して 0 で続行。 */
    gv->type_size = elem_size; /* 暫定 1 要素 */
  }

  /* mangled name: スカラ版と同じ "funcname.varname.<seq>" スキーム。配列用に
   * 'a' プレフィックスを付けてスカラの seq と衝突を避ける。 */
  static int g_static_arr_seq = 0;
  char *funcname = NULL;
  int funcname_len = 0;
  psx_expr_get_current_funcname(&funcname, &funcname_len);
  int seq = g_static_arr_seq++;
  char seq_buf[12];
  int seq_len = snprintf(seq_buf, sizeof(seq_buf), "a%d", seq);
  const char *fname = funcname && funcname_len > 0 ? funcname : "anon";
  int fname_len = funcname && funcname_len > 0 ? funcname_len : 4;
  int total_len = fname_len + 1 + tok->len + 1 + seq_len;
  /* 永続: static local を lowering したグローバルの名前で global_var_t->name が
   * 参照する (コンパイル末尾の gen_global_vars まで生存)。関数ごとに AST arena を
   * リセットしても消えないよう、arena でなく malloc で確保する。 */
  char *mangled = malloc((size_t)total_len + 1);
  int off = 0;
  memcpy(mangled + off, fname, (size_t)fname_len); off += fname_len;
  mangled[off++] = '.';
  memcpy(mangled + off, tok->str, (size_t)tok->len); off += tok->len;
  mangled[off++] = '.';
  memcpy(mangled + off, seq_buf, (size_t)seq_len); off += seq_len;
  mangled[off] = '\0';

  gv->name = mangled;
  gv->name_len = total_len;
  psx_register_global_var(gv);

  /* alias lvar を locals に登録。`is_array=0, size=0` にすることで
   * codegen のフレーム割当 (auto array 経路) を抑制する。
   * resolve_identifier は is_static_local + static_global_name + elem_size>0
   * の組み合わせで「配列の static_local」を識別し、global_vars から
   * 名前で gv を引いて size 等を取得する。 */
  lvar_t *var = calloc(1, sizeof(lvar_t));
  var->next = locals;
  var->next_all = all_locals;
  all_locals = var;
  var->name = tok->str;
  var->len = tok->len;
  var->offset = 0;
  var->size = 0;
  var->elem_size = elem_size; /* 0 だとスカラ扱いになるので elem_size を残す */
  var->is_array = 0;          /* frame 割当抑制のため敢えて 0 */
  var->is_static_local = 1;
  var->is_initialized = 1;
  var->is_unsigned = is_unsigned ? 1 : 0;
  var->fp_kind = fp_kind;
  if (pointer_elem_pointee_size > 0) {
    var->pointer_qual_levels = 1;
    var->base_deref_size = (short)pointer_elem_pointee_size;
  }
  var->static_global_name = mangled;
  var->static_global_name_len = total_len;
  locals = var;
  lvar_index_on_add(var);
  return 1;
}

static int try_lower_static_local_array_consumed(token_ident_t *tok, int elem_size,
                                                 tk_float_kind_t fp_kind,
                                                 int array_count, int is_unsigned,
                                                 int pointer_elem_pointee_size) {
  if (elem_size <= 0) return 0;
  if (array_count == 0 || array_count < -1) return 0;
  long long arr_count = array_count;
  int has_init = 0;
  int has_string_init = 0;
  if (curtok()->kind == TK_ASSIGN) {
    token_t *after_eq = curtok()->next;
    if (!after_eq) return 0;
    if (after_eq->kind == TK_STRING) {
      int cw = (int)((token_string_t *)after_eq)->char_width;
      if (cw <= 0) cw = 1;
      if (elem_size != cw) return 0;
      if (arr_count == -1) {
        arr_count = infer_array_count_from_initializer(elem_size);
        if (arr_count <= 0) return 0;
      }
      has_string_init = 1;
    } else if (after_eq->kind == TK_LBRACE) {
      if (arr_count == -1) {
        arr_count = infer_array_count_from_initializer(elem_size);
        if (arr_count <= 0) return 0;
      }
    } else {
      return 0;
    }
    has_init = 1;
  } else if (curtok()->kind == TK_COMMA || curtok()->kind == TK_SEMI) {
    if (arr_count == -1) return 0;
  } else {
    return 0;
  }
  if (arr_count <= 0) return 0;

  global_var_t *gv = calloc(1, sizeof(global_var_t));
  gv->is_static = 1;
  gv->deref_size = (short)elem_size;
  gv->is_array = 1;
  gv->tag_kind = TK_EOF;
  gv->fp_kind = (unsigned char)fp_kind;
  gv->type_size = (int)arr_count * elem_size;
  if (g_inner_array_dim_count >= 2) {
    int outer_mul = 1;
    for (int i = 1; i < g_inner_array_dim_count; i++) {
      if (g_inner_array_dims[i] > 0) outer_mul *= g_inner_array_dims[i];
    }
    gv->outer_stride = outer_mul * elem_size;
  }
  if (g_inner_array_dim_count >= 3) {
    int mid_mul = 1;
    for (int i = 2; i < g_inner_array_dim_count; i++) {
      if (g_inner_array_dims[i] > 0) mid_mul *= g_inner_array_dims[i];
    }
    gv->mid_stride = mid_mul * elem_size;
  }
  if (g_inner_array_dim_count >= 4) {
    int idx_in_extras = 0;
    for (int start = 3; start < g_inner_array_dim_count && idx_in_extras < 5; start++) {
      int rest_mul = 1;
      for (int j = start; j < g_inner_array_dim_count; j++) {
        if (g_inner_array_dims[j] > 0) rest_mul *= g_inner_array_dims[j];
      }
      gv->extra_strides[idx_in_extras++] = rest_mul * elem_size;
    }
    gv->extra_strides_count = (unsigned char)idx_in_extras;
  }

  if (has_init && has_string_init) {
    tk_expect('=');
    node_t *rhs = psx_expr_assign();
    if (!rhs || rhs->kind != ND_STRING) return 0;
    node_string_t *s = (node_string_t *)rhs;
    string_lit_t *lit = find_string_lit_by_label(s->string_label);
    if (!lit) {
      psx_diag_ctx(curtok(), "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_STRING_INIT_RESOLVE_FAILED));
    }
    gv->has_init = 1;
    gv->init_values = calloc((size_t)arr_count, sizeof(long long));
    gv->init_value_symbols = calloc((size_t)arr_count, sizeof(char *));
    gv->init_value_symbol_lens = calloc((size_t)arr_count, sizeof(int));
    gv->init_union_ordinals = malloc((size_t)arr_count * sizeof(int));
    for (int i = 0; i < arr_count; i++) gv->init_union_ordinals[i] = -1;
    if (fp_kind != TK_FLOAT_KIND_NONE) {
      gv->init_fvalues = calloc((size_t)arr_count, sizeof(double));
    }
    int idx = 0;
    int src_pos = 0;
    while (src_pos < lit->len && idx < arr_count) {
      uint32_t units[2];
      int nu = tk_next_string_code_units(lit->str, lit->len, &src_pos, elem_size, units);
      for (int k = 0; k < nu && idx < arr_count; k++) {
        gv->init_values[idx++] = (long long)units[k];
      }
    }
    if (idx < arr_count) gv->init_values[idx++] = 0;
    gv->init_count = idx;
  } else if (has_init) {
    tk_expect('=');
    gv->has_init = 1;
    int cap = 16;
    gv->init_values = calloc((size_t)cap, sizeof(long long));
    gv->init_value_symbols = calloc((size_t)cap, sizeof(char *));
    gv->init_value_symbol_lens = calloc((size_t)cap, sizeof(int));
    gv->init_union_ordinals = malloc((size_t)cap * sizeof(int));
    for (int i = 0; i < cap; i++) gv->init_union_ordinals[i] = -1;
    if (fp_kind != TK_FLOAT_KIND_NONE) {
      gv->init_fvalues = calloc((size_t)cap, sizeof(double));
    }
    gv->init_count = 0;
    psx_parse_global_brace_init_flat(gv, &cap, -1);
  }

  static int g_static_arr_consumed_seq = 0;
  char *funcname = NULL;
  int funcname_len = 0;
  psx_expr_get_current_funcname(&funcname, &funcname_len);
  int seq = g_static_arr_consumed_seq++;
  char seq_buf[12];
  int seq_len = snprintf(seq_buf, sizeof(seq_buf), "ac%d", seq);
  const char *fname = funcname && funcname_len > 0 ? funcname : "anon";
  int fname_len = funcname && funcname_len > 0 ? funcname_len : 4;
  int total_len = fname_len + 1 + tok->len + 1 + seq_len;
  char *mangled = malloc((size_t)total_len + 1);
  int off = 0;
  memcpy(mangled + off, fname, (size_t)fname_len); off += fname_len;
  mangled[off++] = '.';
  memcpy(mangled + off, tok->str, (size_t)tok->len); off += tok->len;
  mangled[off++] = '.';
  memcpy(mangled + off, seq_buf, (size_t)seq_len); off += seq_len;
  mangled[off] = '\0';

  gv->name = mangled;
  gv->name_len = total_len;
  psx_register_global_var(gv);

  lvar_t *var = calloc(1, sizeof(lvar_t));
  var->next = locals;
  var->next_all = all_locals;
  all_locals = var;
  var->name = tok->str;
  var->len = tok->len;
  var->offset = 0;
  var->size = 0;
  var->elem_size = elem_size;
  var->is_array = 0;
  var->is_static_local = 1;
  var->is_initialized = 1;
  var->is_unsigned = is_unsigned ? 1 : 0;
  var->fp_kind = fp_kind;
  if (pointer_elem_pointee_size > 0) {
    var->pointer_qual_levels = 1;
    var->base_deref_size = (short)pointer_elem_pointee_size;
  }
  if (g_inner_array_dim_count >= 2) {
    int outer_mul = 1;
    for (int i = 1; i < g_inner_array_dim_count; i++) {
      if (g_inner_array_dims[i] > 0) outer_mul *= g_inner_array_dims[i];
    }
    var->outer_stride = outer_mul * elem_size;
  }
  if (g_inner_array_dim_count >= 3) {
    int mid_mul = 1;
    for (int i = 2; i < g_inner_array_dim_count; i++) {
      if (g_inner_array_dims[i] > 0) mid_mul *= g_inner_array_dims[i];
    }
    var->mid_stride = mid_mul * elem_size;
  }
  if (g_inner_array_dim_count >= 4) {
    var->extra_strides = calloc(5, sizeof(int));
    int idx_in_extras = 0;
    for (int start = 3; start < g_inner_array_dim_count && idx_in_extras < 5; start++) {
      int rest_mul = 1;
      for (int j = start; j < g_inner_array_dim_count; j++) {
        if (g_inner_array_dims[j] > 0) rest_mul *= g_inner_array_dims[j];
      }
      var->extra_strides[idx_in_extras++] = rest_mul * elem_size;
    }
    var->extra_strides_count = (unsigned char)idx_in_extras;
  }
  var->static_global_name = mangled;
  var->static_global_name_len = total_len;
  locals = var;
  lvar_index_on_add(var);
  return 1;
}

static int try_lower_static_local_typedef_array(token_ident_t *tok, int elem_size,
                                                tk_float_kind_t fp_kind, int is_unsigned,
                                                const int *td_array_dims, int td_array_dim_count,
                                                int td_array_elem_size) {
  if (elem_size <= 0 || !td_array_dims || td_array_dim_count <= 0 || td_array_dim_count > 8) {
    return 0;
  }
  int eff_elem = elem_size;
  long long total_count = 1;
  for (int i = 0; i < td_array_dim_count; i++) {
    if (td_array_dims[i] <= 0) return 0;
    total_count *= td_array_dims[i];
  }
  if (total_count <= 0 || total_count > INT_MAX) return 0;
  if (td_array_elem_size > 0) {
    int trailing_mul = 1;
    for (int i = 1; i < td_array_dim_count; i++) trailing_mul *= td_array_dims[i];
    int leaf_elem = td_array_elem_size / trailing_mul;
    if (leaf_elem > elem_size) eff_elem = leaf_elem;
  }
  g_inner_array_dim_count = td_array_dim_count;
  for (int i = 0; i < 8; i++) {
    g_inner_array_dims[i] = i < td_array_dim_count ? td_array_dims[i] : 0;
  }
  int pointer_elem_pointee_size = eff_elem > elem_size ? elem_size : 0;
  return try_lower_static_local_array_consumed(tok, eff_elem, fp_kind,
                                               (int)total_count, is_unsigned,
                                               pointer_elem_pointee_size);
}

/* `static struct S a = {...};` / `static union U u = {...};` の struct/union
 * static local をグローバルに lowering する。スカラ/配列の static local と同じく
 * mangled global へ実体を置き、識別子は alias lvar (is_static_local) 経由で
 * ND_GVAR に解決する。これがないと auto 局所として扱われ呼び出し跨ぎで永続せず、
 * 毎回初期化子で再初期化されていた。
 * 初期化子 (`= {...}`) はトップレベル global struct と同じ psx_parse_global_brace_init_flat
 * で flat 値列へ落とし、codegen の emit_global_struct_init がレイアウトに沿って出力する。
 * ポインタ・配列の struct (`static struct S *p` / `static struct S arr[N]`) は呼び出し側
 * ゲートで除外済み。前提: tag_kind が struct/union、is_pointer==0、配列でない。 */
static int try_lower_static_local_struct(token_ident_t *tok, token_kind_t tag_kind,
                                          char *tag_name, int tag_len) {
  int struct_size = psx_ctx_get_tag_size(tag_kind, tag_name, tag_len);
  if (struct_size <= 0) return 0;
  if (tag_name && tag_len >= 11 && memcmp(tag_name, "__anon_tag_", 11) == 0) {
    psx_ctx_promote_tag_to_file_scope(tag_kind, tag_name, tag_len);
  }

  /* global_var_t を構築。tag 情報と struct サイズを設定する。 */
  global_var_t *gv = calloc(1, sizeof(global_var_t));
  gv->is_static = 1;  /* 関数内 static は内部リンケージ: .global を出さない。 */
  psx_decl_set_gvar_tag(gv, tag_kind, tag_name, tag_len, 0);
  gv->type_size = struct_size;
  gv->deref_size = (short)struct_size;
  gv->fp_kind = (unsigned char)TK_FLOAT_KIND_NONE;

  if (curtok()->kind == TK_ASSIGN && curtok()->next &&
      curtok()->next->kind == TK_LBRACE) {
    tk_expect('=');
    gv->has_init = 1;
    int cap = 16;
    gv->init_values = calloc((size_t)cap, sizeof(long long));
    gv->init_value_symbols = calloc((size_t)cap, sizeof(char *));
    gv->init_value_symbol_lens = calloc((size_t)cap, sizeof(int));
    gv->init_union_ordinals = malloc((size_t)cap * sizeof(int));
    for (int i = 0; i < cap; i++) gv->init_union_ordinals[i] = -1;
    /* struct は float/double メンバを持ち得るので fvalues も並行確保する
     * (トップレベル global struct と同じ。codegen が fp メンバをビット出力)。 */
    gv->init_fvalues = calloc((size_t)cap, sizeof(double));
    gv->init_count = 0;
    psx_parse_global_brace_init_flat(gv, &cap, -1);
  }
  /* `= 式` (非 brace) の struct コピー初期化や init 無しは has_init=0 のまま
   * (codegen が .zero でゼロ初期化)。前者は将来課題。 */

  /* mangled name: 配列版と同じスキームで 's' プレフィックス。 */
  static int g_static_struct_seq = 0;
  char *funcname = NULL;
  int funcname_len = 0;
  psx_expr_get_current_funcname(&funcname, &funcname_len);
  int seq = g_static_struct_seq++;
  char seq_buf[12];
  int seq_len = snprintf(seq_buf, sizeof(seq_buf), "s%d", seq);
  const char *fname = funcname && funcname_len > 0 ? funcname : "anon";
  int fname_len = funcname && funcname_len > 0 ? funcname_len : 4;
  int total_len = fname_len + 1 + tok->len + 1 + seq_len;
  /* 永続: static local を lowering したグローバルの名前で global_var_t->name が
   * 参照する (コンパイル末尾の gen_global_vars まで生存)。関数ごとに AST arena を
   * リセットしても消えないよう、arena でなく malloc で確保する。 */
  char *mangled = malloc((size_t)total_len + 1);
  int off = 0;
  memcpy(mangled + off, fname, (size_t)fname_len); off += fname_len;
  mangled[off++] = '.';
  memcpy(mangled + off, tok->str, (size_t)tok->len); off += tok->len;
  mangled[off++] = '.';
  memcpy(mangled + off, seq_buf, (size_t)seq_len); off += seq_len;
  mangled[off] = '\0';

  gv->name = mangled;
  gv->name_len = total_len;
  psx_register_global_var(gv);

  /* alias lvar を locals に登録。size=struct_size, elem_size=struct_size で、
   * is_static_local + static_global_name + tag 情報を持たせる。識別子解決は
   * build_lvar_or_vla_node の static_local 分岐で ND_GVAR (tag 付き) を返す。 */
  lvar_t *var = calloc(1, sizeof(lvar_t));
  var->next = locals;
  var->next_all = all_locals;
  all_locals = var;
  var->name = tok->str;
  var->len = tok->len;
  var->offset = 0;
  var->size = struct_size;
  var->elem_size = struct_size;
  var->is_array = 0;
  var->is_static_local = 1;
  var->is_initialized = 1;
  var->fp_kind = TK_FLOAT_KIND_NONE;
  psx_decl_set_var_tag(var, tag_kind, tag_name, tag_len, 0);
  var->static_global_name = mangled;
  var->static_global_name_len = total_len;
  locals = var;
  lvar_index_on_add(var);
  return 1;
}

/* `static struct S a[N] = {...};` / `static union U a[] = {...};` を
 * static local scalar/array と同じ mangled global に lowering する。
 * file-scope named tag に限定し、1D 配列だけ扱う。 */
static int try_lower_static_local_aggregate_array(token_ident_t *tok, token_kind_t tag_kind,
                                                  char *tag_name, int tag_len) {
  int elem_size = psx_ctx_get_tag_size(tag_kind, tag_name, tag_len);
  if (elem_size <= 0) return 0;
  if (tag_name && tag_len >= 11 && memcmp(tag_name, "__anon_tag_", 11) == 0) {
    psx_ctx_promote_tag_to_file_scope(tag_kind, tag_name, tag_len);
  }
  token_t *p = curtok();
  if (!p || p->kind != TK_LBRACKET) return 0;
  token_t *after_lb = p->next;
  if (!after_lb) return 0;
  int has_size_token = 0;
  long long arr_count = 0;
  token_t *rb = NULL;
  if (after_lb->kind == TK_RBRACKET) {
    rb = after_lb;
  } else if (after_lb->kind == TK_NUM &&
             ((token_num_t *)after_lb)->num_kind == TK_NUM_KIND_INT) {
    has_size_token = 1;
    arr_count = ((token_num_int_t *)after_lb)->val;
    if (arr_count <= 0) return 0;
    rb = after_lb->next;
  } else {
    return 0;
  }
  if (!rb || rb->kind != TK_RBRACKET || !rb->next) return 0;
  if (rb->next->kind == TK_LBRACKET) return 0;
  int has_init = 0;
  if (rb->next->kind == TK_ASSIGN) {
    if (!rb->next->next || rb->next->next->kind != TK_LBRACE) return 0;
    has_init = 1;
    if (!has_size_token) {
      arr_count = psx_decl_count_brace_init_elements(rb->next->next);
      if (arr_count <= 0) return 0;
    }
  } else if (rb->next->kind == TK_COMMA || rb->next->kind == TK_SEMI) {
    if (!has_size_token) return 0;
    has_init = 0;
  } else {
    return 0;
  }

  tk_expect('[');
  if (has_size_token) set_curtok(curtok()->next);
  tk_expect(']');

  global_var_t *gv = calloc(1, sizeof(global_var_t));
  gv->is_static = 1;
  gv->is_array = 1;
  psx_decl_set_gvar_tag(gv, tag_kind, tag_name, tag_len, 0);
  gv->type_size = (int)arr_count * elem_size;
  gv->deref_size = (short)elem_size;
  gv->fp_kind = (unsigned char)TK_FLOAT_KIND_NONE;

  if (has_init) {
    tk_expect('=');
    gv->has_init = 1;
    int cap = 16;
    gv->init_values = calloc((size_t)cap, sizeof(long long));
    gv->init_value_symbols = calloc((size_t)cap, sizeof(char *));
    gv->init_value_symbol_lens = calloc((size_t)cap, sizeof(int));
    gv->init_union_ordinals = malloc((size_t)cap * sizeof(int));
    for (int i = 0; i < cap; i++) gv->init_union_ordinals[i] = -1;
    gv->init_fvalues = calloc((size_t)cap, sizeof(double));
    gv->init_count = 0;
    psx_parse_global_brace_init_flat(gv, &cap, -1);
  }

  static int g_static_aggregate_arr_seq = 0;
  char *funcname = NULL;
  int funcname_len = 0;
  psx_expr_get_current_funcname(&funcname, &funcname_len);
  int seq = g_static_aggregate_arr_seq++;
  char seq_buf[12];
  int seq_len = snprintf(seq_buf, sizeof(seq_buf), "sa%d", seq);
  const char *fname = funcname && funcname_len > 0 ? funcname : "anon";
  int fname_len = funcname && funcname_len > 0 ? funcname_len : 4;
  int total_len = fname_len + 1 + tok->len + 1 + seq_len;
  char *mangled = malloc((size_t)total_len + 1);
  int off = 0;
  memcpy(mangled + off, fname, (size_t)fname_len); off += fname_len;
  mangled[off++] = '.';
  memcpy(mangled + off, tok->str, (size_t)tok->len); off += tok->len;
  mangled[off++] = '.';
  memcpy(mangled + off, seq_buf, (size_t)seq_len); off += seq_len;
  mangled[off] = '\0';

  gv->name = mangled;
  gv->name_len = total_len;
  psx_register_global_var(gv);

  lvar_t *var = calloc(1, sizeof(lvar_t));
  var->next = locals;
  var->next_all = all_locals;
  all_locals = var;
  var->name = tok->str;
  var->len = tok->len;
  var->offset = 0;
  var->size = 0;
  var->elem_size = elem_size;
  var->is_array = 0;
  var->is_static_local = 1;
  var->is_initialized = 1;
  var->fp_kind = TK_FLOAT_KIND_NONE;
  psx_decl_set_var_tag(var, tag_kind, tag_name, tag_len, 0);
  var->static_global_name = mangled;
  var->static_global_name_len = total_len;
  locals = var;
  lvar_index_on_add(var);
  return 1;
}

/* typedef が配列型 (`typedef int M[2][3][4]; M m;`) のときの lvar 登録。
 * td_array_dims をそのまま多次元配列の dims として扱い、
 * outer_stride / mid_stride / extra_strides (8 次元まで) を計算する。 */
static lvar_t *register_typedef_array_lvar(token_ident_t *tok, int elem_size,
                                            const int *td_array_dims, int td_array_dim_count,
                                            int alignas_val) {
  int arr_total = 1;
  for (int di = 0; di < td_array_dim_count; di++) {
    if (td_array_dims[di] > 0) arr_total *= td_array_dims[di];
  }
  int arr_elem_size = elem_size;
  lvar_t *var = psx_decl_register_lvar_sized_align(tok->str, tok->len,
      arr_total * arr_elem_size, arr_elem_size, 1, alignas_val);
  if (td_array_dim_count >= 2) {
    int outer_mul = 1;
    for (int i = 1; i < td_array_dim_count; i++) {
      if (td_array_dims[i] > 0) outer_mul *= td_array_dims[i];
    }
    var->outer_stride = outer_mul * arr_elem_size;
  }
  if (td_array_dim_count >= 3) {
    int mid_mul = 1;
    for (int i = 2; i < td_array_dim_count; i++) {
      if (td_array_dims[i] > 0) mid_mul *= td_array_dims[i];
    }
    var->mid_stride = mid_mul * arr_elem_size;
  }
  if (td_array_dim_count >= 4) {
    var->extra_strides = calloc(5, sizeof(int));
    int idx_in_extras = 0;
    for (int start = 3; start < td_array_dim_count && idx_in_extras < 5; start++) {
      int rest_mul = 1;
      for (int j = start; j < td_array_dim_count; j++) {
        if (td_array_dims[j] > 0) rest_mul *= td_array_dims[j];
      }
      var->extra_strides[idx_in_extras++] = rest_mul * arr_elem_size;
    }
    var->extra_strides_count = (unsigned char)idx_in_extras;
  }
  return var;
}

/* 多次元配列 `[N1][N2][N3]...` の trailing dim 列を読み、stride を含めた lvar を
 * 登録する (最大 8 次元)。outer `[N1]` は呼出側が消費済みで、array_size_inout に
 * その個数が入っている。size_inferred_from_init=true なら `[]` で初期化子から推定。
 * typedef array (td_array_dims) が指定されていれば trailing dims に追加連結する。 */
static lvar_t *register_multidim_array_lvar(token_ident_t *tok, int elem_size,
                                             long long *array_size_inout,
                                             bool size_inferred_from_init, int is_pointer,
                                             int td_array_dim_count, const int *td_array_dims,
                                             int alignas_val) {
  // 多次元配列の trailing dim 列を全て読む（最大 7 段、配列全体では 8 次元まで）。
  int trailing_dims[7] = {0};
  int trailing_count = 0;
  int trailing_mul = parse_decl_constexpr_array_suffix_product_n(trailing_dims, 7, &trailing_count);
  // typedef が配列型のとき (`typedef int M[3][4]; M arr[2];`) は、
  // ユーザーが書いた suffix `[2]` の後ろに typedef dims `[3][4]` を連結する。
  if (!is_pointer && td_array_dim_count > 0) {
    for (int di = 0; di < td_array_dim_count && trailing_count < 7; di++) {
      int dim = td_array_dims[di];
      if (dim > 0) {
        trailing_dims[trailing_count++] = dim;
        trailing_mul *= dim;
      }
    }
  }
  int inner_dim_size = trailing_count >= 1 ? trailing_dims[0] : 0; // 内側次元の要素数（0: 1次元配列）
  long long array_size = *array_size_inout;
  if (size_inferred_from_init) {
    // 外側 `[]` を初期化子から推定する。
    long long top_count = infer_array_count_from_initializer(elem_size);
    if (top_count <= 0) {
      psx_diag_ctx(curtok(), "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
      array_size = 1; // フォールバック
    } else if (inner_dim_size > 0 && !init_first_element_is_brace()) {
      // 多次元のフラット初期化 `int a[][3]={1,2,3,4,5,6}`: 推定値は総要素数。
      array_size = top_count;
    } else {
      // ネスト初期化 `{{...},{...}}` または 1D `int a[]={...}`:
      // top_count は外側次元の要素数なので trailing_mul を掛ける。
      array_size = top_count * trailing_mul;
    }
  } else {
    array_size *= trailing_mul;
  }
  *array_size_inout = array_size;
  int arr_elem_size = is_pointer ? 8 : elem_size;
  lvar_t *var = psx_decl_register_lvar_sized_align(tok->str, tok->len,
      (int)array_size * arr_elem_size, arr_elem_size, 1, alignas_val);
  if (inner_dim_size > 0) {
    // outer_stride = 1 次サブスクリプトのストライド (= 内側全体のバイト数)。
    var->outer_stride = trailing_mul * arr_elem_size;
  }
  if (trailing_count >= 2) {
    // mid_stride = 2 次サブスクリプトのストライド = trailing_dims[1..]*elem。
    int mid_mul = 1;
    for (int i = 1; i < trailing_count; i++) {
      if (trailing_dims[i] > 0) mid_mul *= trailing_dims[i];
    }
    var->mid_stride = mid_mul * arr_elem_size;
  }
  // 4 次元以上: 3 段目以降のストライドを順に extra_strides に格納する。
  if (trailing_count >= 3) {
    var->extra_strides = calloc(5, sizeof(int));
    int idx_in_extras = 0;
    for (int start = 2; start < trailing_count && idx_in_extras < 5; start++) {
      int rest_mul = 1;
      for (int j = start; j < trailing_count; j++) {
        if (trailing_dims[j] > 0) rest_mul *= trailing_dims[j];
      }
      var->extra_strides[idx_in_extras++] = rest_mul * arr_elem_size;
    }
    var->extra_strides_count = (unsigned char)idx_in_extras;
  }
  return var;
}

/* 可変長配列 (VLA) 宣言子の登録。`int a[n]` / `int a[n][M]` / `int a[n][m]` の
 * 3 形態に応じてフレームスロット (16B or 24B) を確保し、ND_VLA_ALLOC ノードを
 * init_chain に append する。 outer `[` は呼出側が消費済み、size_node は
 * その式 (size_ok=0 だった場合の AST)。inner `[...]` はこの helper 内で消費する。
 * 戻り値: 登録した lvar_t (is_vla=1 が立つ)。 */
static lvar_t *register_vla_lvar_and_append_alloc(token_ident_t *tok, int elem_size,
                                                   node_t *size_node, node_t **init_chain_inout) {
  /* N-D VLA 宣言子の登録 (N <= MAX_VLA_DIMS)。
   *   1D                 : 16B slot ([base][bytesize])。subscript は const elem stride。
   *   2D const inner     : 16B slot ([base][bytesize])。subscript は const outer_stride。
   *   2D runtime inner   : 24B slot ([base][bytesize][row_stride])。
   *   3D 以上            : (16 + 8*(N-1))B slot ([base][bytesize][stride_0][stride_1]...
   *                       [stride_{N-2}])。各 subscript で stride スロットを順に消費する。 */
  enum { MAX_VLA_DIMS = 8 };
  node_t *dim_nodes[MAX_VLA_DIMS];
  int dim_is_const[MAX_VLA_DIMS];
  long long dim_const_vals[MAX_VLA_DIMS];
  /* 第 1 dim は size_node として渡される。const-first ケース (size_node が ND_NUM) も
   * 同様に扱う (是非を判定するのは難しいので一律 expr として扱う)。 */
  dim_nodes[0] = size_node;
  dim_is_const[0] = (size_node && size_node->kind == ND_NUM) ? 1 : 0;
  dim_const_vals[0] = dim_is_const[0] ? ((node_num_t *)size_node)->val : 0;
  int dim_count = 1;
  while (tk_consume('[') && dim_count < MAX_VLA_DIMS) {
    node_t *n = NULL;
    int ok = 1;
    long long v = parse_array_size_expr_decl(&n, &ok);
    tk_expect(']');
    dim_nodes[dim_count] = ok ? psx_node_new_num((int)v) : n;
    dim_is_const[dim_count] = ok ? 1 : 0;
    dim_const_vals[dim_count] = ok ? v : 0;
    dim_count++;
  }
  /* 9 次元以上は読み飛ばし (E3064 を残す)。8 次元あれば実用上十分。 */
  parse_decl_skip_constexpr_array_suffixes();

  int vla_row_stride_frame_off = 0;
  int outer_stride = 0;
  int vla_strides_remaining = 0;
  lvar_t *var;
  if (dim_count == 1) {
    /* 1D VLA */
    outer_stride = elem_size;
    var = psx_decl_register_lvar_sized_align(tok->str, tok->len, 16, elem_size, 1, 0);
  } else if (dim_count == 2 && dim_is_const[1]) {
    /* 2D const-inner: outer_stride = M * elem (const). No runtime stride slot. */
    outer_stride = (int)dim_const_vals[1] * elem_size;
    var = psx_decl_register_lvar_sized_align(tok->str, tok->len, 16, elem_size, 1, 0);
  } else if (dim_count == 2) {
    /* 2D runtime-inner: 24B slot, runtime stride at slot+16. */
    var = psx_decl_register_lvar_sized_align(tok->str, tok->len, 24, elem_size, 1, 0);
    vla_row_stride_frame_off = var->offset + 16;
  } else {
    /* 3D 以上: (16 + 8*(N-1))B slot。N-1 個の runtime stride を slot+16, slot+24, ... に保存する
     * (たとえ const dim を含んでいても、運用を統一するため全て runtime として store する)。
     * subscript chain では vla_row を +=8 でシフトし vla_strides_remaining を -1 ずつ消費する。 */
    int n_strides = dim_count - 1;
    int slot_bytes = 16 + 8 * n_strides;
    var = psx_decl_register_lvar_sized_align(tok->str, tok->len, slot_bytes, elem_size, 1, 0);
    vla_row_stride_frame_off = var->offset + 16;
    vla_strides_remaining = n_strides - 1;
  }
  var->is_vla = 1;
  var->outer_stride = outer_stride;
  var->vla_row_stride_frame_off = vla_row_stride_frame_off;
  var->vla_strides_remaining = vla_strides_remaining;

  /* VLA_ALLOC 確保ノード */
  node_mem_t *alloc_node = arena_alloc(sizeof(node_mem_t));
  alloc_node->base.kind = ND_VLA_ALLOC;
  alloc_node->type_size = var->offset;
  alloc_node->vla_row_stride_frame_off = vla_row_stride_frame_off;
  if (dim_count == 1) {
    /* 1D: byte_size = n * elem */
    alloc_node->base.lhs = psx_node_new_binary(ND_MUL, dim_nodes[0], psx_node_new_num(elem_size));
  } else if (dim_count == 2 && dim_is_const[1]) {
    /* 2D const-inner: byte_size = n * outer_stride (const) */
    alloc_node->base.lhs = psx_node_new_binary(ND_MUL, dim_nodes[0], psx_node_new_num(outer_stride));
  } else if (dim_count == 2) {
    /* 2D runtime-inner: lhs=n, rhs=m*elem。VLA_ALLOC が n*rhs=total を計算し
     * rhs を slot+16 (vla_row) に store する。 */
    alloc_node->base.lhs = dim_nodes[0];
    alloc_node->base.rhs = psx_node_new_binary(ND_MUL, dim_nodes[1], psx_node_new_num(elem_size));
  } else {
    /* N-D (N>=3): lhs = dim[0], rhs = outer_stride_expr = dim[1]*dim[2]*...*dim[N-1]*elem。
     * VLA_ALLOC が lhs*rhs=total を計算し rhs を slot+16 (= 最外 stride) に store。
     * 残り N-2 段の stride は init_chain に STORE を注入 (slot+24, slot+32, ...)。
     * 注: dim_nodes は AST ノードを多段で再利用するので、副作用のある式は未サポート。 */
    node_t *outer_stride_expr = psx_node_new_num(elem_size);
    for (int i = dim_count - 1; i >= 1; i--) {
      outer_stride_expr = psx_node_new_binary(ND_MUL, outer_stride_expr, dim_nodes[i]);
    }
    alloc_node->base.lhs = dim_nodes[0];
    alloc_node->base.rhs = outer_stride_expr;
  }
  if (!*init_chain_inout) *init_chain_inout = (node_t *)alloc_node;
  else *init_chain_inout = psx_node_new_binary(ND_COMMA, *init_chain_inout, (node_t *)alloc_node);

  /* N>=3: stride[1..N-2] を slot+24, slot+32, ... に store する STORE 列を注入する。
   * stride[k] = dim[k+1] * dim[k+2] * ... * dim[N-1] * elem。stride[0] は VLA_ALLOC の rhs と
   * 同値で既に slot+16 に store 済み (rsf 経路)、stride[N-1]=elem は最終 subscript で
   * const として扱われるので store 不要。 */
  if (dim_count >= 3) {
    for (int level = 1; level < dim_count - 1; level++) {
      node_t *expr = psx_node_new_num(elem_size);
      for (int i = dim_count - 1; i >= level + 1; i--) {
        expr = psx_node_new_binary(ND_MUL, expr, dim_nodes[i]);
      }
      int slot_off = var->offset + 16 + 8 * level;
      node_t *slot = psx_node_new_lvar_typed(slot_off, 8);
      node_t *st = (node_t *)psx_node_new_assign(slot, expr);
      *init_chain_inout = psx_node_new_binary(ND_COMMA, *init_chain_inout, st);
    }
  }
  return var;
}

/* ---- _Generic 用: 局所変数の型を正規化トークン文字列で記録する ----
 * 関数ポインタ (`int(*)(int,int)`) や深いネスト型 (`int(*(*)(void))[3]`) は
 * generic_type_t の構造的フィールドでは区別できないため、宣言子を「型をそのまま
 * トークン列で書き起こした正規化文字列」にして _Generic の照合で比較する。
 * 制御式が単一の局所変数のときに名前で引く。関数ごとにリセットする。
 * (グローバル g_decl_typespec_start / g_var_typesigs はファイル先頭で宣言。) */

/* [start, end) のトークン綴りを単一スペースで連結する。skip のトークンは除外。
 * '(' を含まない単純型 (scalar / `int*` / `int[3]`) は NULL を返し、従来の構造的照合に
 * 委ねる (修飾子/typedef の細かな差を string 化で誤判定しないため)。malloc した文字列を返す。 */
char *psx_serialize_decl_type_tokens(token_t *start, token_t *end, token_t *skip) {
  int has_paren = 0;
  for (token_t *t = start; t && t != end; t = t->next) {
    if (t->kind == TK_LPAREN) { has_paren = 1; break; }
  }
  if (!has_paren) return NULL;
  size_t cap = 64, len = 0;
  char *buf = malloc(cap);
  if (!buf) return NULL;
  buf[0] = '\0';
  for (token_t *t = start; t && t != end; t = t->next) {
    if (t == skip) continue;
    const char *sp = NULL;
    int sl = 0;
    if (t->kind == TK_IDENT) { token_ident_t *id = (token_ident_t *)t; sp = id->str; sl = id->len; }
    else if (t->kind == TK_NUM) { token_num_t *n = (token_num_t *)t; sp = n->str; sl = n->len; }
    else { sp = tk_token_kind_str((token_kind_t)t->kind, &sl); }
    if (!sp || sl <= 0) continue;
    if (len + (size_t)sl + 2 >= cap) {
      cap = (len + (size_t)sl + 2) * 2;
      char *nb = realloc(buf, cap);
      if (!nb) { free(buf); return NULL; }
      buf = nb;
    }
    if (len > 0) buf[len++] = ' ';
    memcpy(buf + len, sp, (size_t)sl);
    len += (size_t)sl;
    buf[len] = '\0';
  }
  return buf;
}

static void record_var_type_sig(char *name, int len, char *sig) {
  if (!sig || g_var_typesig_count >= VAR_TYPESIG_MAX) return;
  g_var_typesigs[g_var_typesig_count].name = name;
  g_var_typesigs[g_var_typesig_count].len = len;
  g_var_typesigs[g_var_typesig_count].sig = sig;
  g_var_typesig_count++;
}

/* グローバル変数の型シグネチャを記録する (トップレベル宣言から呼ぶ。翻訳単位を通じて永続)。 */
void psx_record_global_type_sig(char *name, int len, char *sig) {
  if (!sig || g_global_typesig_count >= VAR_TYPESIG_MAX) return;
  g_global_typesigs[g_global_typesig_count].name = name;
  g_global_typesigs[g_global_typesig_count].len = len;
  g_global_typesigs[g_global_typesig_count].sig = sig;
  g_global_typesig_count++;
}

/* 単一識別子の制御式 `_Generic(var, ...)` から var の型シグネチャを引く。局所を優先し、
 * 無ければグローバルを引く。どちらにも無ければ NULL。 */
char *psx_lookup_var_type_sig(char *name, int len) {
  for (int i = g_var_typesig_count - 1; i >= 0; i--) {
    if (g_var_typesigs[i].len == len &&
        memcmp(g_var_typesigs[i].name, name, (size_t)len) == 0) {
      return g_var_typesigs[i].sig;
    }
  }
  for (int i = g_global_typesig_count - 1; i >= 0; i--) {
    if (g_global_typesigs[i].len == len &&
        memcmp(g_global_typesigs[i].name, name, (size_t)len) == 0) {
      return g_global_typesigs[i].sig;
    }
  }
  return NULL;
}

node_t *psx_decl_parse_declaration_after_type_ex(int elem_size, tk_float_kind_t decl_fp_kind,
                                                 token_kind_t tag_kind, char *tag_name, int tag_len,
                                                 int base_is_pointer,
                                                 int is_const_qualified, int is_volatile_qualified,
                                                 int decl_is_unsigned_hint,
                                                 const int *td_array_dims, int td_array_dim_count,
                                                 int decl_base_is_void,
                                                 int decl_base_is_bool) {
  node_t *init_chain = NULL;
  /* _Generic 用: 型シグネチャ文字列化のための型開始トークン (呼び出し元が設定)。
   * 一度だけ使い、stale 参照を避けるためここで取り出してクリアする。 */
  token_t *ts_start = g_decl_typespec_start;
  g_decl_typespec_start = NULL;
  int alignas_val = 0;
  /* 基底ポインタ typedef の段数を消費する (read-and-reset)。多段 typedef
   * `typedef int **PP; PP p;` で base が 2 段ぶん寄与する。非ポインタ基底や直書きは 0
   * (= base_is_pointer ? 1 : 0 と一致し従来挙動を保つ)。 */
  int base_pointer_levels = g_decl_base_pointer_levels;
  g_decl_base_pointer_levels = 0;
  unsigned short base_funcptr_param_fp_mask = g_decl_base_funcptr_param_fp_mask;
  unsigned short base_funcptr_param_int_mask = g_decl_base_funcptr_param_int_mask;
  unsigned char base_funcptr_ret_int_width = g_decl_base_funcptr_ret_int_width;
  psx_ret_pointee_array_t base_funcptr_ret_pointee_array =
      g_decl_base_funcptr_ret_pointee_array;
  int base_funcptr_ret_is_void = g_decl_base_funcptr_ret_is_void;
  int base_funcptr_ret_is_pointer = g_decl_base_funcptr_ret_is_pointer;
  int base_funcptr_ret_is_complex = g_decl_base_funcptr_ret_is_complex;
  int base_is_variadic_funcptr = g_decl_base_is_variadic_funcptr;
  short base_funcptr_nargs_fixed = g_decl_base_funcptr_nargs_fixed;
  g_decl_base_funcptr_param_fp_mask = 0;
  g_decl_base_funcptr_param_int_mask = 0;
  g_decl_base_funcptr_ret_int_width = 0;
  g_decl_base_funcptr_ret_pointee_array = psx_ret_pointee_array_make(0, 0, 0);
  g_decl_base_funcptr_ret_is_void = 0;
  g_decl_base_funcptr_ret_is_pointer = 0;
  g_decl_base_funcptr_ret_is_complex = 0;
  g_decl_base_is_variadic_funcptr = 0;
  g_decl_base_funcptr_nargs_fixed = 0;
  /* td_array_elem_size も同様に「宣言文全体の typedef 由来」なので read-and-reset
   * (declarator ループ後にもう一度宣言文があれば、その spec で立て直す)。
   * 非 typedef spec で前回値が残ると 3522 経路が誤検出するため、ここでクリアする。
   * 一旦ローカル変数に取って declarator ループ内で参照する形にする。 */
  int td_array_elem_size_for_this_decl = g_decl_td_array_elem_size;
  g_decl_td_array_elem_size = 0;
  int td_is_array_for_this_decl = g_decl_td_is_array;
  g_decl_td_is_array = 0;
  if (base_is_pointer && base_pointer_levels < 1) base_pointer_levels = 1;
  if (!base_is_pointer) base_pointer_levels = 0;
  int decl_is_unsigned = psx_last_type_is_unsigned() || decl_is_unsigned_hint;
  int decl_is_complex = psx_last_type_is_complex();
  int decl_is_long_long = psx_last_type_is_long_long();
  int decl_is_plain_char = psx_last_type_is_plain_char();
  int decl_is_long_double = psx_last_type_is_long_double();
  if (g_decl_td_is_long_double) decl_is_long_double = 1;
  g_decl_td_is_long_double = 0;
  int decl_is_atomic = psx_last_type_is_atomic();
  psx_take_alignas_value(&alignas_val);
  int decl_is_extern = 0;
  psx_take_extern_flag(&decl_is_extern);
  if (decl_is_extern) {
    local_decl_spec_t ds = {0};
    init_local_decl_spec(&ds);
    ds.elem_size = elem_size;
    ds.fp_kind = decl_fp_kind;
    ds.tag_kind = tag_kind;
    ds.tag_name = tag_name;
    ds.tag_len = tag_len;
    ds.base_is_pointer = base_is_pointer;
    ds.is_unsigned = decl_is_unsigned;
    parse_local_extern_declarator_list(&ds);
    tk_expect(';');
    return psx_node_new_num(0);
  }
  int decl_is_static = 0;
  psx_take_static_flag(&decl_is_static);

  // _Complex 型: サイズを基底型の2倍にする（実部 + 虚部）
  if (decl_is_complex && !base_is_pointer) {
    elem_size *= 2;
  }

  int declarator_count = 0;
  for (;;) {
    declarator_count++;
    if (declarator_count > PS_MAX_DECLARATOR_COUNT) {
      psx_diag_ctx(curtok(), "decl",
                   diag_message_for(DIAG_ERR_PARSER_DECLARATOR_LIST_TOO_LONG),
                   PS_MAX_DECLARATOR_COUNT);
    }
    int is_pointer = base_is_pointer;
    unsigned int ptr_const_mask = 0;
    unsigned int ptr_volatile_mask = 0;
    int ptr_levels = 0;
    consume_pointer_chain_decl(&is_pointer, &ptr_const_mask, &ptr_volatile_mask, &ptr_levels);
    if (tag_kind != TK_EOF && !is_pointer && elem_size <= 0) {
      psx_diag_ctx(curtok(), "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_INCOMPLETE_OBJECT_FORBIDDEN));
    }

    int paren_array_mul = 0;
    int inner_array_mul = 0;
    g_paren_array_first_dim = 0;
    g_paren_array_second_dim = 0;
    g_paren_array_dim_count = 0;
    g_paren_array_vla_dim = NULL;
    g_inner_array_dim_count = 0;
    g_decl_trailing_func_suffix = 0;
    g_decl_had_paren_group = 0;
    g_decl_func_suffix_count = 0;
    /* td_array_elem_size は宣言文 (spec 共有) ごとに valid なので、各 declarator では
     * リセットしない。type spec 解析時に parse_local_decl_spec_from_typedef が立てる。 */
    for (int i = 0; i < 8; i++) g_inner_array_dims[i] = 0;
    token_ident_t *tok = consume_decl_name_ex(&is_pointer, &ptr_const_mask, &ptr_volatile_mask, &ptr_levels,
                                              &paren_array_mul, &inner_array_mul);
    /* `int (* (*p)(int,int))(int,int)` は declarator 上 `*` が 2 つあるが、p は
     * pointer-to-function 1 段のみ (戻り funcptr)。`**pp` や `struct S *(*f)(int)` は
     * 対象外 (後者は戻りデータポインタで pql=2 が要る)。 */
    if (ptr_levels >= 2 && g_decl_trailing_func_suffix && g_decl_had_paren_group &&
        paren_array_mul == 0 && inner_array_mul == 0 &&
        g_decl_func_suffix_count >= 2) {
      ptr_levels = 1;
    }
    /* 関数内ローカル関数プロトタイプ宣言 (`int f1(char *);`): C11 6.2.2p5 で暗黙 extern。
     * declarator が non-pointer の関数 (`(...)` 付きで `*` なし) のとき、ローカル変数として
     * 登録せず宣言を読み飛ばすだけにする。グローバル関数テーブルには別途関数定義
     * (`int f1(char *p){...}`) で登録されているので、呼び出し時は通常の関数呼び出し経路。
     * 関数ポインタ変数 (`int (*fp)(char *);`) は is_pointer=1 なので除外。 */
    if (tok && g_decl_trailing_func_suffix && !is_pointer) {
      /* 初期化子は許されないが防御的に skip、次の declarator または `;` へ。 */
      if (curtok()->kind == TK_ASSIGN) {
        set_curtok(curtok()->next);
        psx_expr_assign();
      }
      if (curtok()->kind == TK_COMMA) {
        set_curtok(curtok()->next);
        continue;
      }
      tk_expect(';');
      return init_chain ? init_chain : psx_node_new_num(0);
    }
    /* typedef が配列で base もポインタ (pointer-element 配列 typedef: `typedef IP IPA[3]`
     * など) のケース: declarator に `*` を追加していない (= IPA arr / OpArr3 arr) なら宣言は
     * 配列であり、is_pointer は本来立つべきではない (`IP arr[3]` 相当 = `int *arr[3]`)。
     * base 由来の is_pointer をそのままにすると 3522 経路 (配列ポインタ) に流れて
     * 「配列宣言」経路 (3616) に乗れず、`IPA arr = {&s,...}` の brace init が「スカラ初期化子」
     * と誤判定 (E3064)。declarator に `*` 追加 (`IPA *pa`) なら is_pointer のままで配列
     * ポインタ扱い。 */
    if (is_pointer && ptr_levels == 0 && td_array_dim_count > 0 &&
        td_array_elem_size_for_this_decl > 0) {
      is_pointer = 0;
    }
    int var_size = is_pointer ? 8 : elem_size;
    /* 基底が多段ポインタ typedef なら段数ぶん寄与する (`PP p` = int**)。単段 typedef・
     * 非ポインタ基底は base_pointer_levels が 1/0 で従来の `(base_is_pointer?1:0)` と一致。 */
    int total_pointer_levels = ptr_levels + base_pointer_levels;
    int pointer_deref_size = (total_pointer_levels >= 2) ? 8 : elem_size;
    int ptr_is_const_qualified = (ptr_const_mask & 1u) ? 1 : 0;
    int ptr_is_volatile_qualified = (ptr_volatile_mask & 1u) ? 1 : 0;

    /* C11 6.7.2p2: void は不完全型なので、それ自体でオブジェクトを宣言できない。
     * `void x;` はエラー、`void *p;` は可。is_pointer は宣言子のポインタチェーン
     * (`*` 列) を含んだ後の値なので、ここで判定できる。 */
    if (decl_base_is_void && !is_pointer) {
      psx_diag_ctx(curtok(), "decl",
                   diag_message_for(DIAG_ERR_PARSER_VOID_OBJECT_FORBIDDEN),
                   tok ? tok->len : 0, tok ? tok->str : "");
    }

    int static_scalar_tag_ok = (tag_kind == TK_EOF || tag_kind == TK_ENUM);

    /* `static` ローカル: 配列や struct でない単純スカラ (int/long/short/char/pointer)
     * はグローバルに lowering する。配列・struct 等の複雑形は現状フォールバック
     * (= 既存の auto と同じ挙動になる; 既知の制約)。 */
    if (decl_is_static && static_scalar_tag_ok &&
        inner_array_mul == 0 && paren_array_mul == 0 &&
        curtok()->kind != TK_LBRACKET && td_array_dim_count == 0) {
      unsigned short static_funcptr_param_fp_mask =
          is_pointer ? (g_decl_trailing_func_suffix ? g_last_funcptr_param_fp_mask
                                                    : base_funcptr_param_fp_mask)
                     : 0;
      unsigned short static_funcptr_param_int_mask =
          is_pointer ? (g_decl_trailing_func_suffix ? g_last_funcptr_param_int_mask
                                                    : base_funcptr_param_int_mask)
                     : 0;
      unsigned char static_funcptr_ret_int_width =
          is_pointer ? (g_decl_trailing_func_suffix
                            ? ((!((ptr_levels > 1 || base_is_pointer) ? 1 : 0) &&
                                !decl_base_is_void &&
                                tag_kind == TK_EOF &&
                                decl_fp_kind == TK_FLOAT_KIND_NONE)
                                   ? (elem_size >= 8 ? 8 : 4)
                                   : 0)
                            : base_funcptr_ret_int_width)
                     : 0;
      int static_funcptr_ret_is_void =
          is_pointer ? (g_decl_trailing_func_suffix ? (decl_base_is_void ? 1 : 0)
                                                    : (base_funcptr_ret_is_void ? 1 : 0))
                     : 0;
      int static_funcptr_ret_is_complex =
          is_pointer ? (g_decl_trailing_func_suffix
                            ? ((decl_is_complex &&
                                !((ptr_levels > 1 || base_is_pointer) ? 1 : 0))
                                   ? 1
                                   : 0)
                            : (base_funcptr_ret_is_complex ? 1 : 0))
                     : 0;
      int static_is_variadic_funcptr =
          is_pointer && (g_last_funcptr_is_variadic || base_is_variadic_funcptr);
      short static_funcptr_nargs_fixed =
          g_last_funcptr_is_variadic ? (short)g_last_funcptr_nfixed
                                     : base_funcptr_nargs_fixed;
      if (try_lower_static_local_scalar(tok, var_size,
                                         is_pointer ? pointer_deref_size : var_size,
                                         is_pointer ? TK_FLOAT_KIND_NONE : decl_fp_kind,
                                         decl_is_unsigned,
                                         is_pointer,
                                         static_funcptr_param_fp_mask,
                                         static_funcptr_param_int_mask,
                                         static_funcptr_ret_int_width,
                                         static_funcptr_ret_is_void,
                                         static_funcptr_ret_is_complex,
                                         static_is_variadic_funcptr,
                                         static_funcptr_nargs_fixed)) {
        if (!tk_consume(',')) break;
        continue;
      }
    }
    /* `static int t[N] = {...};` 1D 整数配列 static local をグローバル化。
     * curtok が '[' のときのみ試行 (struct/typedef-array/pointer は除外)。
     * 関数は peek だけして scope 外なら 0 を返し curtok を変えないため、
     * 既存の auto 配列経路 (line 2297 `tk_consume('[')`) に安全に fall through する。 */
    if (decl_is_static && static_scalar_tag_ok && !is_pointer &&
        inner_array_mul == 0 && paren_array_mul == 0 &&
        td_array_dim_count == 0 &&
        curtok()->kind == TK_LBRACKET) {
      if (try_lower_static_local_array(tok, elem_size, decl_fp_kind, decl_is_unsigned, 0)) {
        if (!tk_consume(',')) break;
        continue;
      }
    }
    if (decl_is_static && static_scalar_tag_ok && is_pointer &&
        !g_decl_trailing_func_suffix &&
        inner_array_mul == 0 && paren_array_mul == 0 &&
        td_array_dim_count == 0 &&
        curtok()->kind == TK_LBRACKET) {
      if (try_lower_static_local_array(tok, 8, TK_FLOAT_KIND_NONE, 0, elem_size)) {
        if (!tk_consume(',')) break;
        continue;
      }
    }
    if (decl_is_static && static_scalar_tag_ok && !is_pointer &&
        (inner_array_mul > 0 || inner_array_mul == -1) &&
        paren_array_mul == 0 &&
        td_array_dim_count == 0 &&
        curtok()->kind != TK_LBRACKET) {
      if (try_lower_static_local_array_consumed(tok, elem_size, decl_fp_kind,
                                                inner_array_mul, decl_is_unsigned, 0)) {
        if (!tk_consume(',')) break;
        continue;
      }
    }
    if (decl_is_static && static_scalar_tag_ok && is_pointer &&
        !g_decl_trailing_func_suffix &&
        (inner_array_mul > 0 || inner_array_mul == -1) &&
        paren_array_mul == 0 &&
        td_array_dim_count == 0 &&
        curtok()->kind != TK_LBRACKET) {
      if (try_lower_static_local_array_consumed(tok, 8, TK_FLOAT_KIND_NONE,
                                                inner_array_mul, 0, elem_size)) {
        if (!tk_consume(',')) break;
        continue;
      }
    }
    if (decl_is_static && static_scalar_tag_ok && !is_pointer &&
        inner_array_mul == 0 && paren_array_mul == 0 &&
        td_array_dim_count > 0 && curtok()->kind != TK_LBRACKET) {
      if (try_lower_static_local_typedef_array(tok, elem_size, decl_fp_kind,
                                                decl_is_unsigned, td_array_dims,
                                                td_array_dim_count,
                                                td_array_elem_size_for_this_decl)) {
        if (!tk_consume(',')) break;
        continue;
      }
    }
    /* `static struct S a[N] = {...};` / `static union U a[] = {...};` の 1D aggregate
     * static local をグローバル化する。 */
    if (decl_is_static && (tag_kind == TK_STRUCT || tag_kind == TK_UNION) &&
        !is_pointer && inner_array_mul == 0 && paren_array_mul == 0 &&
        td_array_dim_count == 0 && curtok()->kind == TK_LBRACKET) {
      if (try_lower_static_local_aggregate_array(tok, tag_kind, tag_name, tag_len)) {
        if (!tk_consume(',')) break;
        continue;
      }
    }
    /* `static struct S a = {...};` / `static union U u = {...};` の struct/union
     * static local をグローバル化。ポインタ (`static struct S *p`、上の scalar 経路で
     * 処理) と、上の aggregate array 経路に入らない配列形は除外する。 */
    if (decl_is_static && (tag_kind == TK_STRUCT || tag_kind == TK_UNION) &&
        !is_pointer && inner_array_mul == 0 && paren_array_mul == 0 &&
        td_array_dim_count == 0 && curtok()->kind != TK_LBRACKET) {
      if (try_lower_static_local_struct(tok, tag_kind, tag_name, tag_len)) {
        if (!tk_consume(',')) break;
        continue;
      }
    }

    lvar_t *var = NULL;
    {
      if ((inner_array_mul > 0 || inner_array_mul == -1) && is_pointer) {
        // `int (*ops[N])(int,int)` 関数ポインタ配列、または
        // `int (*p[N])[M]` array-of-pointer-to-array。
        // どちらも配列要素は 8 バイトのポインタなので elem_size=8 で is_array を立てる。
        // inner_array_mul==-1 は `int (*ops[])(...)={f,g,...}` の形で、
        // 要素数を初期化子から推定する必要がある。
        int effective_count = inner_array_mul;
        if (inner_array_mul == -1) {
          long long inferred = infer_array_count_from_initializer(8);
          if (inferred > 0) {
            effective_count = (int)inferred;
          } else {
            psx_diag_ctx(curtok(), "decl", "%s",
                         diag_message_for(DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
            effective_count = 1;
          }
        }
        int arr_total_bytes = effective_count * 8;
        var = psx_decl_register_lvar_sized_align(tok->str, tok->len, arr_total_bytes, 8, 1, alignas_val);
        psx_decl_set_var_tag(var, tag_kind, tag_name, tag_len, tag_kind != TK_EOF);
        int pointee_array_bytes = (!g_decl_trailing_func_suffix && paren_array_mul > 0)
                                      ? paren_array_mul * elem_size
                                      : 0;
        var->base_deref_size = (short)(pointee_array_bytes > 0 ? elem_size : 8);
        var->ptr_array_pointee_bytes = pointee_array_bytes;
        var->is_const_qualified = is_const_qualified;
        var->is_volatile_qualified = is_volatile_qualified;
        /* 2 次元以上の関数ポインタ配列 `int (*t[2][2])(void)`: 括弧内個別次元から多次元
         * ストライドを立てる (要素は 8B funcptr)。これがないと flat 1D 配列扱いで `t[i][j]` が
         * 誤計算/SIGSEGV になる。1 次元 `int(*ops[N])(...)` は dim_count<2 で従来どおり。
         * グローバル paren funcptr 配列 (320e0ff) の局所版。 */
        if (g_inner_array_dim_count >= 2) {
          int outer_mul = 1;
          for (int i = 1; i < g_inner_array_dim_count; i++) outer_mul *= g_inner_array_dims[i];
          var->outer_stride = outer_mul * 8;
          if (g_inner_array_dim_count >= 3) {
            int mid_mul = 1;
            for (int i = 2; i < g_inner_array_dim_count; i++) mid_mul *= g_inner_array_dims[i];
            var->mid_stride = mid_mul * 8;
          }
        }
      } else if (paren_array_mul > 0 && g_paren_array_vla_dim != NULL) {
        /* pointer-to-VLA `int (*p)[m]` (m はランタイム値)。行ストライド (m*elem) は
         * コンパイル時に決まらないので、ポインタ値 + 行ストライドの隠しスロットを 16B 確保し
         * (offset=ポインタ値, offset+8=行ストライド)、宣言時に `*(off+8)=m*elem` を init_chain に
         * 注入する。subscript は vla_row_stride_frame_off を実行時参照する (定数版の outer_stride
         * 相当)。outer_stride は 0 のままにして「実行時ストライド」経路に乗せる。 */
        var = psx_decl_register_lvar_sized_align(tok->str, tok->len, 16, elem_size, 0, alignas_val);
        psx_decl_set_var_tag(var, tag_kind, tag_name, tag_len, 0);
        var->base_deref_size = (short)elem_size;
        var->is_const_qualified = is_const_qualified;
        var->is_volatile_qualified = is_volatile_qualified;
        var->is_pointer_const_qualified = ptr_is_const_qualified;
        var->is_pointer_volatile_qualified = ptr_is_volatile_qualified;
        var->pointer_const_qual_mask = ptr_const_mask;
        var->pointer_volatile_qual_mask = ptr_volatile_mask;
        var->vla_row_stride_frame_off = var->offset + 8;
        var->vla_row_stride_elem_size = (short)elem_size;
        node_t *slot = psx_node_new_lvar_typed(var->vla_row_stride_frame_off, 8);
        node_t *stride_val = psx_node_new_binary(ND_MUL, g_paren_array_vla_dim,
                                                 psx_node_new_num(elem_size));
        node_t *store = (node_t *)psx_node_new_assign(slot, stride_val);
        init_chain = init_chain ? psx_node_new_binary(ND_COMMA, init_chain, store) : store;
        g_paren_array_vla_dim = NULL;
      } else if (paren_array_mul > 0) {
        // (*p)[N] パターン: 配列へのポインタ
        /* 配列要素がポインタ (関数ポインタや typedef 経由のデータポインタ) の場合、
         * 要素サイズは「ポインタサイズ = 8」になる。elem_size は基底 (戻り型 or pointee 型、
         * 例 int=4) で来るので上書きする。条件:
         *   (a) `int (*(*pa)[N])(args)` 形式: trailing 部に関数シグネチャ
         *   (b) `BinOp (*pa)[N]` / `typedef int *IP; IP (*pa)[N]` 形式: base_is_pointer
         * これがないと (*pa)[i] の deref が 4B (ldrsw) で出力され、ポインタを下位 4B だけ
         * load して SIGSEGV / 誤値になる。 */
        int element_is_pointer_paren = ((g_decl_trailing_func_suffix && is_pointer) ||
                                        base_is_pointer ||
                                        (ptr_levels >= 2 && is_pointer))
                                           ? 1 : 0;
        int eff_elem = element_is_pointer_paren ? 8 : elem_size;
        int row_size = paren_array_mul * eff_elem;
        var = psx_decl_register_lvar_sized_align(tok->str, tok->len, 8, eff_elem, 0, alignas_val);
        psx_decl_set_var_tag(var, tag_kind, tag_name, tag_len, 0);
        /* データポインタ要素 (`IP (*pia)[N]`, base_is_pointer のみ): pointer_qual_levels=1 と
         * base_deref_size=pointee サイズ (= 基底 elem_size、IP なら int=4) を立てて、
         * `*(*pia)[0]` の最終 deref が pointee サイズで出力されるよう build_subscript_deref
         * の「要素はポインタ」分岐に乗せる。関数ポインタ要素は call で 8B 値をそのまま使う
         * ため、bds=8 (= eff_elem) のままで OK (既存挙動)。 */
        if (base_is_pointer && !g_decl_trailing_func_suffix && elem_size > 0 && elem_size < 8) {
          var->base_deref_size = (short)elem_size;
          var->pointer_qual_levels = 1;
        } else {
          var->base_deref_size = (short)eff_elem;
        }
        if (ptr_levels >= 2 && !g_decl_trailing_func_suffix && paren_array_mul > 0) {
          var->ptr_array_pointee_bytes = paren_array_mul * elem_size;
          var->base_deref_size = (short)elem_size;
        }
        (void)element_is_pointer_paren;
        var->outer_stride = row_size;
        var->is_const_qualified = is_const_qualified;
        var->is_volatile_qualified = is_volatile_qualified;
        var->is_pointer_const_qualified = ptr_is_const_qualified;
        var->is_pointer_volatile_qualified = ptr_is_volatile_qualified;
        var->pointer_const_qual_mask = ptr_const_mask;
        var->pointer_volatile_qual_mask = ptr_volatile_mask;
        /* 多次元 inner (`(*p)[N][M]`): p[i] は [N][M] 全体 (outer_stride)、p[i][j] は
         * 内側 1 行 ([M]) ぶん進む。mid_stride = (積/先頭次元)*elem を設定する。 */
        if (g_paren_array_dim_count >= 2 && g_paren_array_first_dim > 0) {
          var->mid_stride = (paren_array_mul / g_paren_array_first_dim) * eff_elem;
        }
      } else if (is_pointer && td_array_dim_count > 0 &&
                 (total_pointer_levels == 1 ||
                  (ptr_levels == 1 && td_is_array_for_this_decl)) &&
                 curtok()->kind != TK_LBRACKET) {
        /* `row3 *p` (row3 = typedef int[3]): 配列へのポインタ。paren 形 `int (*p)[3]`
         * と同じく outer_stride=配列全体バイト数、base_deref_size=要素サイズ、
         * 多次元 typedef なら mid_stride を設定する。これがないと p+1 / p[i] が
         * 要素 1 個分しか進まず、直書き `int(*p)[3]` と挙動が食い違う。
         *
         * 要素がポインタの 1 次元配列 typedef (`typedef BinOp OpArr3[3]; OpArr3 *pa`):
         * td_array_elem_size が要素サイズ (8) を返す。条件で eff_elem に上書き:
         *  (a) td_array_dim_count == 1 (1 次元 typedef、内側次元が無い)
         *  (b) td_array_elem_size > elem_size (要素が基底型より大きい = ポインタ要素)
         *      または typedef 基底自体が pointer (関数ポインタ要素など同サイズの pointer 要素)
         * 多次元 typedef (`typedef int m23[2][3]; m23 *q`、td_array_dim_count==2) は既存の
         * elem_size + outer_stride / mid_stride 経路を使うので上書きしない。基底と要素が
         * 同サイズ (`typedef long L[3]; L *p`) も elem_size のままで正しい。
         * pointer-to-array typedef (`typedef int (*PA)[3]; PA p`) は td_array_elem_size=0。 */
        int eff_elem = elem_size;
        int element_is_pointer = 0;
        if (td_array_elem_size_for_this_decl > 0 &&
            td_array_dim_count == 1 &&
            (td_array_elem_size_for_this_decl > elem_size || base_is_pointer)) {
          eff_elem = td_array_elem_size_for_this_decl;
          element_is_pointer = 1;
        }
        int td_count = 1;
        for (int di = 0; di < td_array_dim_count; di++) {
          if (td_array_dims[di] > 0) td_count *= td_array_dims[di];
        }
        int row_size = td_count * eff_elem;
        var = psx_decl_register_lvar_sized_align(tok->str, tok->len, 8, eff_elem, 0, alignas_val);
        psx_decl_set_var_tag(var, tag_kind, tag_name, tag_len, 0);
        /* 要素がポインタ (pointer-element 配列 typedef + `*`) のとき、base_deref_size には
         * 「要素 (ポインタ) の pointee サイズ」(= 元の基底型 elem_size、IP なら int=4) を
         * 設定し、pointer_qual_levels=1 を立てる。これにより build_unary_deref_node の
         * 配列 decay 経路で pql/bds が carry され、build_subscript_deref の「要素はポインタ」
         * 分岐に乗って `*(*pia)[0]` の最終 deref が pointee サイズ (int=4) で出力される。 */
        var->base_deref_size = element_is_pointer ? (short)elem_size : (short)eff_elem;
        if (element_is_pointer) var->pointer_qual_levels = 1;
        var->outer_stride = row_size;
        if (td_array_dim_count >= 2 && td_array_dims[0] > 0) {
          var->mid_stride = (td_count / td_array_dims[0]) * eff_elem;
        }
        var->is_const_qualified = is_const_qualified;
        var->is_volatile_qualified = is_volatile_qualified;
        var->is_pointer_const_qualified = ptr_is_const_qualified;
        var->is_pointer_volatile_qualified = ptr_is_volatile_qualified;
        /* 要素ポインタ配列だけ pointer_qual_levels=1 を立てる。通常の多次元 typedef 配列では
         * 立てない: base_deref_size>0 と合わさって build_subscript_deref の「要素がポインタ」
         * 分岐に乗り、q[i] が多段ストライド連鎖 (inner_deref_size 経由) でなく
         * base_deref_size 単段になって 2D 以上の `q[i][j][k]` が壊れる。 */
      } else if (!is_pointer && td_array_dim_count > 0 && curtok()->kind != TK_LBRACKET) {
        /* typedef 配列型 (`typedef int M[2][3][4]; M m;`): td_array_dims を
         * そのまま使って stride を計算しつつ lvar を登録する。
         * 要素がポインタの 1 次元配列 typedef (`typedef IP IPA[3]`、続き20 で td_array_elem_size
         * を取得済み) は要素サイズが 8 (ポインタ) なので elem_size の代わりに使う。条件:
         *  (a) td_array_dim_count == 1 (1 次元、多次元 typedef は td_array_elem_size が「行
         *      サイズ」になるので除外。`typedef int M[3][4]; M m` で 16 を使うと壊れる)
         *  (b) td_array_elem_size > elem_size (要素サイズが基底型より大きい = ポインタ要素) */
        int eff_elem_for_arr = (td_array_elem_size_for_this_decl > 0 &&
                                td_array_dim_count == 1 &&
                                td_array_elem_size_for_this_decl > elem_size)
                                   ? td_array_elem_size_for_this_decl : elem_size;
        var = register_typedef_array_lvar(tok, eff_elem_for_arr, td_array_dims,
                                           td_array_dim_count, alignas_val);
        psx_decl_set_var_tag(var, tag_kind, tag_name, tag_len, 0);
        var->is_const_qualified = is_const_qualified;
        var->is_volatile_qualified = is_volatile_qualified;
        /* 要素がポインタの場合: pointer_qual_levels=1 / base_deref_size=pointee サイズを立て、
         * `*arr[i]` の subscript+deref で「要素はポインタ」分岐に乗せて pointee で deref する。 */
        if (eff_elem_for_arr > elem_size && elem_size > 0) {
          var->pointer_qual_levels = 1;
          var->base_deref_size = (short)elem_size;
        }
      } else if (tk_consume('[')) {
        node_t *size_node = NULL;
        int size_ok = 1;
        bool size_inferred_from_init = (curtok() && curtok()->kind == TK_RBRACKET);
        long long array_size = size_inferred_from_init
                                   ? 1
                                   : parse_array_size_expr_decl(&size_node, &size_ok);
        tk_expect(']');
        if (!size_ok) {
          /* 可変長配列 (VLA): フレームスロット (1D/2D 定数=16B, 2D 実行時=24B, 3D=32B)
           * を確保し、ND_VLA_ALLOC ノードを init_chain に append する。 */
          var = register_vla_lvar_and_append_alloc(tok, elem_size, size_node, &init_chain);
          /* VLA は continue で下の fp_kind/is_unsigned 設定をスキップする。VLA 記述子
           * 自体はベースポインタ (整数) なので fp_kind は NONE のまま、要素型は
           * pointee_fp_kind に入れて subscript の fp load/store に伝播させる。 */
          var->pointee_fp_kind = decl_fp_kind;
          var->is_unsigned = decl_is_unsigned;
          /* タグ情報も carry (struct/union 要素 VLA `struct P arr[n]` で `arr[i].m` を解決可能に)。
           * is_tag_pointer=0: 配列なので tag ポインタではない。 */
          psx_decl_set_var_tag(var, tag_kind, tag_name, tag_len, 0);
          if (!tk_consume(',')) break;
          continue;
        }
        /* 第 1 dim が const でも、後の dim に VLA があれば配列全体は VLA (C11 6.7.6.2)。
         * peek で trailing にIDENT (= enum 定数以外) があれば VLA 経路へ redirect し、
         * const 第 1 dim を ND_NUM ノードとして size_node に詰める。これがないと
         * register_multidim_array_lvar が parse_decl_constexpr_array_suffix_product_n で
         * VLA dim を非定数と判定し E3064 を出していた。 */
        if (!size_inferred_from_init && decl_peek_trailing_array_dims_have_vla()) {
          node_t *first_size_node = psx_node_new_num((int)array_size);
          var = register_vla_lvar_and_append_alloc(tok, elem_size, first_size_node, &init_chain);
          var->pointee_fp_kind = decl_fp_kind;
          var->is_unsigned = decl_is_unsigned;
          psx_decl_set_var_tag(var, tag_kind, tag_name, tag_len, 0);
          if (!tk_consume(',')) break;
          continue;
        }
        /* 多次元配列 `[N1][N2][N3]...` の dim 列と stride 計算を集約。
         * outer `[N1]` 部分は呼出側で消費済み (array_size に格納)。
         * helper は trailing dims (`[N2][N3]...`) を消費しつつ lvar を登録し、
         * outer_stride / mid_stride / extra_strides を設定する。 */
        var = register_multidim_array_lvar(tok, elem_size, &array_size,
                                            size_inferred_from_init, is_pointer,
                                            td_array_dim_count, td_array_dims, alignas_val);
        psx_decl_set_var_tag(var, tag_kind, tag_name, tag_len, is_pointer);
        var->is_const_qualified = is_const_qualified;
        var->is_volatile_qualified = is_volatile_qualified;
        var->is_pointer_const_qualified = ptr_is_const_qualified;
        var->is_pointer_volatile_qualified = ptr_is_volatile_qualified;
        var->pointer_const_qual_mask = ptr_const_mask;
        var->pointer_volatile_qual_mask = ptr_volatile_mask;
        var->pointer_qual_levels = total_pointer_levels;
        if (is_pointer) {
          var->base_deref_size = (short)elem_size;
        }
      } else {
        var = psx_decl_register_lvar_sized_align(tok->str, tok->len, var_size,
                                           is_pointer ? pointer_deref_size : var_size, 0, alignas_val);
        psx_decl_set_var_tag(var, tag_kind, tag_name, tag_len, is_pointer);
        var->is_const_qualified = is_const_qualified;
        var->is_volatile_qualified = is_volatile_qualified;
        var->is_pointer_const_qualified = ptr_is_const_qualified;
        var->is_pointer_volatile_qualified = ptr_is_volatile_qualified;
        var->pointer_const_qual_mask = ptr_const_mask;
        var->pointer_volatile_qual_mask = ptr_volatile_mask;
        var->pointer_qual_levels = total_pointer_levels;
        if (is_pointer && total_pointer_levels >= 2) {
          var->base_deref_size = (short)elem_size;
        }
      }
    }

    if (!is_pointer) {
      var->fp_kind = decl_fp_kind;
      var->pointee_fp_kind = TK_FLOAT_KIND_NONE;
    } else {
      var->fp_kind = TK_FLOAT_KIND_NONE;
      /* 多段ポインタ (`double **pp`) でも最内 pointee の fp 種別を保持する。
       * build_unary_deref_node が 1 段ずつ引き継ぎ、最終 deref で fp load/store に
       * する。中間段は pql>=2 のため fp 値化されない (deref 結果はポインタのまま)。 */
      var->pointee_fp_kind = decl_fp_kind;
    }
    if (is_pointer && base_is_pointer && !td_is_array_for_this_decl &&
        td_array_dim_count > 0 && ptr_levels >= 1 && var->ptr_array_pointee_bytes == 0) {
      int td_pointee_count = 1;
      for (int di = 0; di < td_array_dim_count; di++) {
        if (td_array_dims[di] > 0) td_pointee_count *= td_array_dims[di];
      }
      var->ptr_array_pointee_bytes = td_pointee_count * elem_size;
      var->base_deref_size = (short)elem_size;
    }
    var->is_unsigned = decl_is_unsigned;
    if (decl_is_complex && !is_pointer) var->is_complex = 1;
    if (decl_is_atomic) var->is_atomic = 1;
    /* _Generic で long/long long, char/signed char を区別するための型識別 (非ポインタの
     * スカラ変数のみ。サイズは同じなので別フラグで持つ)。 */
    if (!is_pointer) {
      var->is_long_long = decl_is_long_long ? 1 : 0;
      var->is_plain_char = decl_is_plain_char ? 1 : 0;
      var->is_long_double = decl_is_long_double ? 1 : 0;
    }
    /* 可変長関数ポインタ (`int (*f)(int, ...)`): 経由呼び出しで variadic ABI を
     * 選べるよう、固定引数数と共に記録する。 */
    int has_base_funcptr_sig =
        base_funcptr_param_fp_mask || base_funcptr_param_int_mask ||
        base_funcptr_ret_int_width || base_funcptr_ret_is_void ||
        base_funcptr_ret_is_pointer || base_funcptr_ret_is_complex || base_is_variadic_funcptr ||
        psx_ret_pointee_array_has_dims(base_funcptr_ret_pointee_array);
    int is_funcptr_decl = is_pointer || g_decl_trailing_func_suffix || has_base_funcptr_sig;
    if (is_funcptr_decl && (g_last_funcptr_is_variadic || base_is_variadic_funcptr)) {
      var->is_variadic_funcptr = 1;
      var->funcptr_nargs_fixed = g_last_funcptr_is_variadic
                                      ? (short)g_last_funcptr_nfixed
                                      : base_funcptr_nargs_fixed;
    }
    /* 関数ポインタの仮引数 fp マスク (経由呼び出しの int→fp 昇格用)。 */
    if (is_funcptr_decl) {
      var->funcptr_param_fp_mask = g_decl_trailing_func_suffix
                                       ? g_last_funcptr_param_fp_mask
                                       : base_funcptr_param_fp_mask;
      var->funcptr_param_int_mask = g_decl_trailing_func_suffix
                                       ? g_last_funcptr_param_int_mask
                                       : base_funcptr_param_int_mask;
      int direct_ret_is_data_pointer =
          (g_decl_trailing_func_suffix && (ptr_levels > 1 || base_is_pointer)) ? 1 : 0;
      var->funcptr_ret_is_data_pointer =
          g_decl_trailing_func_suffix ? direct_ret_is_data_pointer
                                      : (base_funcptr_ret_is_pointer ? 1 : 0);
      var->funcptr_ret_is_void =
          g_decl_trailing_func_suffix
              ? (decl_base_is_void && !direct_ret_is_data_pointer)
              : (base_funcptr_ret_is_void ? 1 : 0);
      var->funcptr_ret_is_complex =
          g_decl_trailing_func_suffix
              ? ((decl_is_complex && !var->funcptr_ret_is_data_pointer) ? 1 : 0)
              : (base_funcptr_ret_is_complex ? 1 : 0);
      var->funcptr_ret_int_width = g_decl_trailing_func_suffix
                                       ? ((!var->funcptr_ret_is_data_pointer &&
                                           !decl_base_is_void &&
                                           tag_kind == TK_EOF &&
                                           decl_fp_kind == TK_FLOAT_KIND_NONE)
                                              ? (elem_size >= 8 ? 8 : 4)
                                              : 0)
                                       : base_funcptr_ret_int_width;
      psx_ret_pointee_array_t direct_ret_pointee_array =
          (g_decl_trailing_func_suffix && paren_array_mul > 0 && g_paren_array_first_dim > 0)
              ? psx_ret_pointee_array_make(g_paren_array_first_dim,
                                           g_paren_array_second_dim,
                                           elem_size)
              : psx_ret_pointee_array_make(0, 0, 0);
      psx_ret_pointee_array_t ret_pointee_array = {0};
      PSX_RET_POINTEE_ARRAY_SELECT_INTO(&ret_pointee_array,
                                        &direct_ret_pointee_array,
                                        &base_funcptr_ret_pointee_array);
      PSX_RET_POINTEE_ARRAY_STORE_SHORT_FIELDS_IF_PRESENT(var, ret_pointee_array);
    }
    if (is_pointer && g_decl_had_paren_group && g_decl_func_suffix_count >= 2) {
      var->funcptr_ret_is_pointer = 1;
    }
    /* `_Bool b = expr;` 代入/初期化時に rhs を 0/1 に正規化するため。 */
    if (decl_base_is_bool && !is_pointer) var->is_bool = 1;
    /* `void *p` (基底型 void + ポインタ宣言): pointee_is_void を立てる。
     * deref のエラー検出 (C11 6.5.3.2) で必要。 */
    if (decl_base_is_void && is_pointer && total_pointer_levels == 1) {
      var->pointee_is_void = 1;
    }

    /* _Generic 用: 先頭宣言子の型を name 抜きでトークン文字列化して名前で記録する。
     * 複雑な派生型 (関数ポインタ / ネスト宣言子, '(' を含む) のみ非 NULL。型開始トークン
     * を含むのは先頭宣言子だけなので declarator_count==1 に限定する。 */
    if (declarator_count == 1 && ts_start && var && tok) {
      char *sig = psx_serialize_decl_type_tokens(ts_start, curtok(), (token_t *)tok);
      if (sig) record_var_type_sig(tok->str, tok->len, sig);
    }

    if (tk_consume('=')) {
      var->is_initialized = 1;
      node_t *init_node = psx_decl_parse_initializer_for_var(var, is_pointer);
      if (!init_chain) init_chain = init_node;
      else init_chain = psx_node_new_binary(ND_COMMA, init_chain, init_node);
      if (!tk_consume(',')) break;
      continue;
    }

    if (!tk_consume(',')) break;
  }

  tk_expect(';');
  return init_chain ? init_chain : psx_node_new_num(0);
}

node_t *psx_decl_parse_declaration(void) {
  if (curtok()->kind == TK_TYPEDEF) {
    return parse_typedef_declaration_local();
  }

  if (curtok()->kind == TK_STATIC_ASSERT) {
    set_curtok(curtok()->next);
    tk_expect('(');
    int const_ok = 1;
    long long cond_val = eval_const_expr_decl(psx_expr_assign(), &const_ok);
    tk_expect(',');
    if (curtok()->kind != TK_STRING) {
      diag_emit_tokf(DIAG_ERR_PARSER_STATIC_ASSERT_MSG_NOT_STRING, curtok(), "%s",
                     diag_message_for(DIAG_ERR_PARSER_STATIC_ASSERT_MSG_NOT_STRING));
    }
    set_curtok(curtok()->next);
    tk_expect(')');
    tk_expect(';');
    if (!const_ok) {
      diag_emit_tokf(DIAG_ERR_PARSER_STATIC_ASSERT_COND_NOT_CONST, curtok(), "%s",
                     diag_message_for(DIAG_ERR_PARSER_STATIC_ASSERT_COND_NOT_CONST));
    }
    if (cond_val == 0) {
      diag_emit_tokf(DIAG_ERR_PARSER_STATIC_ASSERT_FAILED, curtok(), "%s",
                     diag_message_for(DIAG_ERR_PARSER_STATIC_ASSERT_FAILED));
    }
    return psx_node_new_num(0);
  }

  /* _Generic 用: 型シグネチャ文字列化のため、型指定子の開始トークンを記録する
   * (_ex が先頭宣言子の [型開始, 宣言子終端) を name 抜きで文字列化する)。 */
  g_decl_typespec_start = curtok();
  local_decl_spec_t ds = {0};
  if (!parse_local_decl_spec(&ds)) {
    diag_emit_tokf(DIAG_ERR_PARSER_TYPE_NAME_REQUIRED, curtok(), "%s",
                   diag_message_for(DIAG_ERR_PARSER_TYPE_NAME_REQUIRED));
  }

  if (ds.is_extern_decl) {
    // ローカルextern宣言: グローバルテーブルに登録してローカル変数は作らない
    parse_local_extern_declarator_list(&ds);
    tk_expect(';');
    return psx_node_new_num(0);
  }

  if (ds.td_funcptr_param_fp_mask || ds.td_funcptr_param_int_mask ||
      ds.td_funcptr_ret_int_width || ds.td_funcptr_ret_is_void ||
      ds.td_funcptr_ret_is_pointer || ds.td_funcptr_ret_is_complex ||
      ds.td_is_variadic_funcptr) {
    g_decl_base_funcptr_param_fp_mask = ds.td_funcptr_param_fp_mask;
    g_decl_base_funcptr_param_int_mask = ds.td_funcptr_param_int_mask;
    g_decl_base_funcptr_ret_int_width = ds.td_funcptr_ret_int_width;
    g_decl_base_funcptr_ret_is_void = ds.td_funcptr_ret_is_void ? 1 : 0;
    g_decl_base_funcptr_ret_is_pointer = ds.td_funcptr_ret_is_pointer ? 1 : 0;
    g_decl_base_funcptr_ret_is_complex = ds.td_funcptr_ret_is_complex ? 1 : 0;
    g_decl_base_is_variadic_funcptr = ds.td_is_variadic_funcptr ? 1 : 0;
    g_decl_base_funcptr_nargs_fixed = ds.td_funcptr_nargs_fixed;
  }

  return psx_decl_parse_declaration_after_type_ex(ds.elem_size, ds.fp_kind,
                                                  ds.tag_kind, ds.tag_name, ds.tag_len,
                                                  ds.base_is_pointer,
                                                  ds.is_const_qualified ? 1 : ds.td_pointee_const,
                                                  ds.is_volatile_qualified ? 1 : ds.td_pointee_volatile,
                                                  ds.is_unsigned,
                                                  ds.td_array_dims, ds.td_array_dim_count,
                                                  ds.type_kind == TK_VOID ? 1 : 0,
                                                  ds.type_kind == TK_BOOL ? 1 : 0);
}

static int parse_local_decl_spec(local_decl_spec_t *out) {
  init_local_decl_spec(out);

  out->type_kind = psx_consume_type_kind();
  out->is_unsigned = psx_last_type_is_unsigned();
  out->is_long_long = psx_last_type_is_long_long();
  out->is_plain_char = psx_last_type_is_plain_char();
  take_local_decl_prefix_flags(out);
  if (out->type_kind == TK_EOF) return parse_local_decl_spec_from_typedef(out);
  return parse_local_decl_spec_from_builtin(out);
}

static int parse_local_decl_spec_from_typedef(local_decl_spec_t *out) {
  if (!psx_ctx_is_typedef_name_token(curtok())) return 0;
  token_kind_t base_kind = TK_EOF;
  token_ident_t *id = (token_ident_t *)curtok();
  // 多次元配列 typedef (`typedef int M[2][3][4]`) の dims を取得して保持する。
  resolve_typedef_array_dims(id, out->td_array_dims, &out->td_array_dim_count);
  /* typedef が配列型の場合の要素 1 個のサイズ (要素がポインタなら 8 になる)。
   * pointer-to-array typedef (`typedef int (*PA)[3]`) は is_array=0 なので 0 を返す。
   * static global にも転記して、3522 経路の declarator 側からも参照できるようにする
   * (caller シグネチャを増やさずに済ませるため)。 */
  out->td_array_elem_size = resolve_typedef_array_element_size(id);
  g_decl_td_array_elem_size = out->td_array_elem_size;
  /* 多段ポインタ typedef (`typedef int **PP`) の段数を捕捉し、宣言経路へ受け渡す。
   * id はトークンなので resolve で curtok が進んでも文字列は有効。 */
  g_decl_base_pointer_levels = psx_ctx_get_typedef_pointer_levels(id->str, id->len);
  {
    psx_typedef_info_t _ti;
    if (psx_ctx_find_typedef_name(id->str, id->len, &_ti)) {
      g_decl_td_is_array = _ti.is_array ? 1 : 0;
      out->td_funcptr_param_fp_mask = _ti.funcptr_param_fp_mask;
      out->td_funcptr_param_int_mask = _ti.funcptr_param_int_mask;
      out->td_funcptr_ret_int_width = _ti.funcptr_ret_int_width;
      out->td_funcptr_ret_is_void = _ti.funcptr_ret_is_void ? 1 : 0;
      out->td_funcptr_ret_is_pointer = _ti.funcptr_ret_is_pointer ? 1 : 0;
      out->td_funcptr_ret_is_complex = _ti.funcptr_ret_is_complex ? 1 : 0;
      out->td_is_variadic_funcptr = _ti.is_variadic_funcptr ? 1 : 0;
      out->td_funcptr_nargs_fixed = _ti.funcptr_nargs_fixed;
      g_decl_base_funcptr_param_fp_mask = _ti.funcptr_param_fp_mask;
      g_decl_base_funcptr_param_int_mask = _ti.funcptr_param_int_mask;
      g_decl_base_funcptr_ret_int_width = _ti.funcptr_ret_int_width;
      g_decl_base_funcptr_ret_pointee_array = _ti.funcptr_ret_pointee_array;
      g_decl_base_funcptr_ret_is_void = _ti.funcptr_ret_is_void;
      g_decl_base_funcptr_ret_is_pointer = _ti.funcptr_ret_is_pointer;
      g_decl_base_funcptr_ret_is_complex = _ti.funcptr_ret_is_complex;
      g_decl_base_is_variadic_funcptr = _ti.is_variadic_funcptr ? 1 : 0;
      g_decl_base_funcptr_nargs_fixed = _ti.funcptr_nargs_fixed;
    }
  }
  resolve_typedef_name_ref_local(&base_kind, &out->elem_size, &out->fp_kind,
                                 &out->tag_kind, &out->tag_name, &out->tag_len,
                                 &out->base_is_pointer,
                                 &out->td_pointee_const, &out->td_pointee_volatile,
                                 &out->is_unsigned, &out->is_long_double);
  adjust_local_decl_spec_from_typedef(out, base_kind);
  g_decl_td_is_long_double = out->is_long_double ? 1 : 0;
  return 1;
}

static int parse_local_decl_spec_from_builtin(local_decl_spec_t *out) {
  g_decl_base_pointer_levels = 0;
  g_decl_base_funcptr_param_fp_mask = 0;
  g_decl_base_funcptr_param_int_mask = 0;
  g_decl_base_funcptr_ret_int_width = 0;
  g_decl_base_funcptr_ret_pointee_array = psx_ret_pointee_array_make(0, 0, 0);
  g_decl_base_funcptr_ret_is_void = 0;
  g_decl_base_funcptr_ret_is_pointer = 0;
  g_decl_base_funcptr_ret_is_complex = 0;
  g_decl_base_is_variadic_funcptr = 0;
  g_decl_base_funcptr_nargs_fixed = 0;
  g_decl_td_is_array = 0;
  g_decl_td_is_long_double = 0;
  resolve_builtin_type_local(out->type_kind, &out->elem_size, &out->fp_kind);
  return 1;
}

static void parse_local_extern_declarator_list(local_decl_spec_t *ds) {
  int declarator_count = 0;
  for (;;) {
    declarator_count++;
    if (declarator_count > PS_MAX_DECLARATOR_COUNT) {
      psx_diag_ctx(curtok(), "decl",
                   diag_message_for(DIAG_ERR_PARSER_DECLARATOR_LIST_TOO_LONG),
                   PS_MAX_DECLARATOR_COUNT);
    }
    int is_ptr = ds->base_is_pointer;
    unsigned int ptr_const_mask = 0;
    unsigned int ptr_volatile_mask = 0;
    int ptr_levels = 0;
    int paren_array_dim = 0;
    g_paren_array_first_dim = 0;
    g_paren_array_second_dim = 0;
    g_paren_array_dim_count = 0;
    g_paren_array_vla_dim = NULL;
    g_decl_trailing_func_suffix = 0;
    g_decl_had_paren_group = 0;
    g_decl_func_suffix_count = 0;
    consume_pointer_chain_decl(&is_ptr, &ptr_const_mask, &ptr_volatile_mask, &ptr_levels);
    token_ident_t *name = consume_decl_name(&is_ptr, &ptr_const_mask, &ptr_volatile_mask, &ptr_levels, &paren_array_dim);
    decl_array_suffix_t arr = parse_decl_array_suffixes(paren_array_dim);
    int is_function_prototype = g_decl_trailing_func_suffix && !is_ptr;
    if (!is_function_prototype) {
      register_local_extern_decl(name, is_ptr, arr, ds->elem_size, ds->fp_kind,
                                 ds->tag_kind, ds->tag_name, ds->tag_len,
                                 ds->is_unsigned);
    }
    if (curtok()->kind == TK_ASSIGN) {
      set_curtok(curtok()->next);
      psx_expr_assign();
    }
    if (curtok()->kind != TK_COMMA) break;
    set_curtok(curtok()->next);
  }
}

static void register_local_extern_decl(token_ident_t *name, int is_ptr, decl_array_suffix_t arr,
                                       int elem_size, tk_float_kind_t fp_kind,
                                       token_kind_t tag_kind, char *tag_name, int tag_len,
                                       int is_unsigned) {
  if (find_global_var_decl(name->str, name->len)) return;
  global_var_t *gv = calloc(1, sizeof(global_var_t));
  gv->name = name->str;
  gv->name_len = name->len;
  gv->type_size = arr.has_incomplete_array ? 0 :
                  (arr.is_array ? (is_ptr ? 8 : elem_size) * arr.arr_total : (is_ptr ? 8 : elem_size));
  gv->deref_size = elem_size;
  gv->is_array = arr.is_array;
  gv->is_extern_decl = 1;
  gv->fp_kind = is_ptr ? (unsigned char)TK_FLOAT_KIND_NONE : (unsigned char)fp_kind;
  gv->is_unsigned = is_unsigned ? 1 : 0;
  psx_decl_set_gvar_tag(gv, tag_kind, tag_name, tag_len, is_ptr);
  psx_register_global_var(gv);
}

static node_t *parse_typedef_declaration_local(void) {
  set_curtok(curtok()->next); // consume typedef

  token_kind_t base_kind = TK_EOF;
  int elem_size = 8;
  tk_float_kind_t fp_kind = TK_FLOAT_KIND_NONE;
  token_kind_t tag_kind = TK_EOF;
  char *tag_name = NULL;
  int tag_len = 0;
  int is_pointer_base = 0;
  int is_long_double_base = 0;
  resolve_local_typedef_decl_spec(&base_kind, &elem_size, &fp_kind,
                                  &tag_kind, &tag_name, &tag_len, &is_pointer_base,
                                  &is_long_double_base);

  int td_pointee_const = 0;
  int td_pointee_volatile = 0;
  psx_take_type_qualifiers(&td_pointee_const, &td_pointee_volatile);
  int td_is_unsigned = (base_kind == TK_UNSIGNED) || psx_last_type_is_unsigned();
  int td_is_long_double = psx_last_type_is_long_double() || is_long_double_base;

  parse_local_typedef_declarator_list(base_kind, elem_size, fp_kind, tag_kind, tag_name, tag_len,
                                      is_pointer_base,
                                      td_pointee_const, td_pointee_volatile,
                                      td_is_unsigned, td_is_long_double,
                                      base_kind == TK_VOID ? 1 : 0);
  tk_expect(';');
  return psx_node_new_num(0);
}

static void resolve_local_typedef_decl_spec(token_kind_t *base_kind, int *elem_size,
                                            tk_float_kind_t *fp_kind,
                                            token_kind_t *tag_kind, char **tag_name, int *tag_len,
                                            int *is_pointer_base, int *is_long_double_base) {
  if (psx_ctx_is_tag_keyword(curtok()->kind)) {
    *tag_kind = curtok()->kind;
    *base_kind = *tag_kind;
    set_curtok(curtok()->next);
    token_ident_t *tag = tk_consume_ident();
    if (!tag) {
      psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_TAG_NAME));
    }
    *tag_name = tag->str;
    *tag_len = tag->len;
    if (!psx_ctx_has_tag_type(*tag_kind, *tag_name, *tag_len)) {
      psx_ctx_define_tag_type(*tag_kind, *tag_name, *tag_len);
    }
    *elem_size = psx_ctx_get_tag_size(*tag_kind, *tag_name, *tag_len);
    return;
  }

  token_kind_t builtin_kind = psx_consume_type_kind();
  if (builtin_kind != TK_EOF) {
    *base_kind = builtin_kind;
    resolve_builtin_type_local(builtin_kind, elem_size, fp_kind);
    if (is_long_double_base) *is_long_double_base = psx_last_type_is_long_double();
    return;
  }
  if (psx_ctx_is_typedef_name_token(curtok())) {
    resolve_typedef_name_ref_local(base_kind, elem_size, fp_kind,
                                   tag_kind, tag_name, tag_len, is_pointer_base,
                                   NULL, NULL, NULL, is_long_double_base);
    return;
  }
  diag_emit_tokf(DIAG_ERR_PARSER_TYPE_NAME_REQUIRED, curtok(), "%s",
                 diag_message_for(DIAG_ERR_PARSER_TYPE_NAME_REQUIRED));
}

static void define_local_typedef_from_declarator(token_ident_t *name, int is_ptr, int paren_array_mul,
                                                 token_kind_t base_kind, int elem_size,
                                                 tk_float_kind_t fp_kind,
                                                 token_kind_t tag_kind, char *tag_name, int tag_len,
                                                 int td_pointee_const, int td_pointee_volatile,
                                                 int td_is_unsigned, int td_is_long_double,
                                                 int td_base_is_void,
                                                 int decl_added_pointer) {
  /* 配列要素がポインタの typedef (`typedef BinOp OpArr3[3]`): base が pointer typedef だが
   * declarator は `*` を追加していない (decl_added_pointer=0)。この場合の typedef は
   * 「pointer 配列」なので is_array=1、要素サイズはポインタサイズ (8)、sizeof_size=8*N。
   * 通常の「pointer typedef + 配列 suffix なし」(`BinOp f`) は arr.is_array=0 なので影響なし。
   * 直書きの「array of pointer typedef」(`typedef int *IP[5]`) も同じ扱い: declarator に
   * `*` と `[N]` の両方があり、結果は「N 個の int* 配列」。修正前はこの形が is_array=0 /
   * sizeof=8 で「単一ポインタ」と解釈され、`IP a; a[0] = &g; *a[0]` が SIGSEGV していた。 */
  int base_is_ptr_only = (is_ptr && !decl_added_pointer);
  decl_array_suffix_t arr = parse_decl_array_suffixes(paren_array_mul);
  int decl_has_array_with_ptr = (is_ptr && decl_added_pointer && arr.is_array);
  int elem_unit_size = is_ptr ? 8 : elem_size;
  int typedef_sizeof = elem_unit_size;
  if (!is_ptr && arr.has_incomplete_array) typedef_sizeof = 0;
  else if (!is_ptr && arr.is_array && arr.arr_total > 0) typedef_sizeof *= arr.arr_total;
  else if (base_is_ptr_only && arr.is_array && arr.arr_total > 0) typedef_sizeof = 8 * arr.arr_total;
  else if (decl_has_array_with_ptr && arr.arr_total > 0) typedef_sizeof = 8 * arr.arr_total;
  token_kind_t stored_base_kind = (td_is_unsigned && base_kind == TK_INT) ? TK_UNSIGNED : base_kind;
  // `typedef int row_t[3]` のように配列型を typedef した場合は is_array=1 で記録する。
  // 不完全配列 `typedef int A[]` も is_array=1 (sizeof_size は 0)。
  // `typedef BinOp OpArr3[3]` (ポインタ要素配列) も is_array=1。
  // `typedef int *IP[5]` (decl_has_array_with_ptr) も同様に is_array=1。
  int td_is_array = ((!is_ptr || base_is_ptr_only || decl_has_array_with_ptr) &&
                     (arr.is_array || arr.has_incomplete_array)) ? 1 : 0;
  int td_first_dim = td_is_array ? arr.first_dim : 0;
  int td_dim_count = td_is_array ? arr.dim_count : 0;
  int typedef_is_funcptr =
      g_decl_trailing_func_suffix && (is_ptr || g_decl_had_paren_group);
  psx_typedef_info_t _ti = {0};
  _ti.base_kind = stored_base_kind;
  _ti.elem_size = elem_size;
  _ti.fp_kind = fp_kind;
  _ti.tag_kind = tag_kind;
  _ti.tag_name = tag_name;
  _ti.tag_len = tag_len;
  _ti.is_pointer = (is_ptr || typedef_is_funcptr) ? 1 : 0;
  _ti.sizeof_size = typedef_sizeof;
  _ti.pointee_const_qualified = td_pointee_const;
  _ti.pointee_volatile_qualified = td_pointee_volatile;
  _ti.is_unsigned = td_is_unsigned;
  _ti.is_long_double = (!is_ptr && !td_is_array && td_is_long_double) ? 1 : 0;
  _ti.is_array = td_is_array;
  _ti.array_first_dim = td_first_dim;
  _ti.array_dim_count = td_dim_count;
  for (int i = 0; i < td_dim_count && i < 8; i++) _ti.array_dims[i] = arr.dims[i];
  if (typedef_is_funcptr && td_base_is_void) {
    _ti.is_funcptr = 1;
    _ti.funcptr_ret_is_void = 1;
  }
  if (typedef_is_funcptr) {
    _ti.is_funcptr = 1;
    int ret_is_data_pointer = (paren_array_mul == 0 && !td_base_is_void &&
                               g_decl_had_paren_group && g_decl_func_suffix_count == 1 &&
                               psx_funcptr_ret_int_width_from_kind(base_kind, 0, fp_kind) == 0)
                                  ? 1
                                  : 0;
    _ti.funcptr_ret_is_pointer = ret_is_data_pointer;
    _ti.funcptr_ret_is_complex =
        (psx_last_type_is_complex() && !ret_is_data_pointer) ? 1 : 0;
    _ti.funcptr_ret_int_width =
        psx_funcptr_ret_int_width_from_kind(base_kind, ret_is_data_pointer, fp_kind);
  }
  if (typedef_is_funcptr && psx_last_funcptr_is_variadic()) {
    _ti.is_funcptr = 1;
    _ti.is_variadic_funcptr = 1;
    _ti.funcptr_nargs_fixed = (short)psx_last_funcptr_nargs_fixed();
  }
  if (typedef_is_funcptr && g_last_funcptr_param_fp_mask) {
    _ti.is_funcptr = 1;
    _ti.funcptr_param_fp_mask = g_last_funcptr_param_fp_mask;
  }
  if (typedef_is_funcptr && g_last_funcptr_param_int_mask) {
    _ti.is_funcptr = 1;
    _ti.funcptr_param_int_mask = g_last_funcptr_param_int_mask;
  }
  if (typedef_is_funcptr && paren_array_mul > 0 &&
      g_paren_array_first_dim > 0) {
    _ti.funcptr_ret_pointee_array =
        psx_ret_pointee_array_make(g_paren_array_first_dim, g_paren_array_second_dim,
                                   elem_size);
  }
  if (!psx_ctx_define_typedef_name(name->str, name->len, &_ti)) {
    psx_diag_duplicate_with_name(curtok(), "typedef", name->str, name->len);
  }
}

static void parse_local_typedef_declarator_list(token_kind_t base_kind, int elem_size,
                                                tk_float_kind_t fp_kind,
                                                token_kind_t tag_kind, char *tag_name, int tag_len,
                                                int is_pointer_base,
                                                int td_pointee_const, int td_pointee_volatile,
                                                int td_is_unsigned, int td_is_long_double,
                                                int td_base_is_void) {
  for (;;) {
    int is_ptr = is_pointer_base;
    unsigned int ptr_const_mask = 0;
    unsigned int ptr_volatile_mask = 0;
    int ptr_levels = 0;
    int paren_array_mul = 0;
    g_paren_array_first_dim = 0;
    g_paren_array_second_dim = 0;
    g_paren_array_dim_count = 0;
    g_paren_array_vla_dim = NULL;
    g_decl_trailing_func_suffix = 0;
    g_decl_had_paren_group = 0;
    g_decl_func_suffix_count = 0;
    consume_pointer_chain_decl(&is_ptr, &ptr_const_mask, &ptr_volatile_mask, &ptr_levels);
    token_ident_t *name = consume_decl_name(&is_ptr, &ptr_const_mask, &ptr_volatile_mask, &ptr_levels, &paren_array_mul);
    /* declarator が `*` を 1 つでも追加していれば decl_added_pointer=1。is_ptr が base 由来
     * のみか declarator 由来かを判別する (ptr_levels は declarator 側の `*` 個数を持つ)。 */
    int decl_added_ptr = (ptr_levels > 0) ? 1 : 0;
    define_local_typedef_from_declarator(name, is_ptr, paren_array_mul,
                                         base_kind, elem_size, fp_kind,
                                         tag_kind, tag_name, tag_len,
                                         td_pointee_const, td_pointee_volatile,
                                         td_is_unsigned, td_is_long_double, td_base_is_void,
                                         decl_added_ptr);
    if (!tk_consume(',')) break;
  }
}
