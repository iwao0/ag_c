#include "parser.h"
#include "parser_public.h"  /* ps_iter_globals prototype */
#include "arena.h"
#include "node_utils.h"
#include "semantic_ctx.h"
#include "decl.h"
#include "core.h"
#include "alignas_value.h"
#include "anon_tag.h"
#include "array_suffixes.h"
#include "diag.h"
#include "dynarray.h"
#include "enum_const.h"
#include "expr.h"
#include "loop_ctx.h"
#include "ret_pointee_array.h"
#include "stmt.h"
#include "struct_layout.h"
#include "switch_ctx.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include "../tokenizer/escape.h"
#include "../tokenizer/literals.h"
#include "../pragma_pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

string_lit_t *string_literals = NULL;
float_lit_t *float_literals = NULL;
global_var_t *global_vars = NULL;

/* グローバル変数の名前ハッシュ索引。グローバル参照の解決 (try_build_global_var_node)
 * や登録時の重複チェックが global_vars を線形走査しており、グローバル N 個・参照 M 回で
 * O(N*M) になっていた。名前を 256 バケットへハッシュして O(1) 化する。global_vars は
 * TU 全体で生存しスコープも無いので、登録時に挿入するだけ (除去・リセット不要)。 */
#define GVAR_HASH_BUCKETS 256u
static global_var_t *gvars_by_bucket[GVAR_HASH_BUCKETS];

static unsigned gvar_name_hash(const char *name, int len) {
  unsigned h = 2166136261u;
  for (int i = 0; i < len; i++) h = (h ^ (unsigned char)name[i]) * 16777619u;
  return h & (GVAR_HASH_BUCKETS - 1u);
}

void psx_register_global_var(global_var_t *gv) {
  gv->next = global_vars;
  global_vars = gv;
  unsigned h = gvar_name_hash(gv->name, gv->name_len);
  gv->next_hash = gvars_by_bucket[h];
  gvars_by_bucket[h] = gv;
}

global_var_t *psx_find_global_var(char *name, int len) {
  /* bucket は MRU 順 (登録順) なので、最初の名前一致が global_vars 線形走査と
   * 同じ変数 (重複登録時も先頭 = 同一挙動)。 */
  unsigned h = gvar_name_hash(name, len);
  for (global_var_t *gv = gvars_by_bucket[h]; gv; gv = gv->next_hash) {
    if (gv->name_len == len && memcmp(gv->name, name, (size_t)len) == 0) {
      return gv;
    }
  }
  return NULL;
}

/* parser_public.h で宣言した visitor の実装 (Phase C3-1)。
 * codegen 側が global_vars / string_literals / float_literals リストを
 * 直接舐めるのを廃して、走査経路を 1 箇所にまとめる。 */
void ps_iter_globals(global_var_visitor_t fn, void *user) {
  for (global_var_t *gv = global_vars; gv; gv = gv->next) {
    fn(gv, user);
  }
}

bool ps_iter_string_literals(string_lit_visitor_t fn, void *user) {
  if (!string_literals) return false;
  for (string_lit_t *lit = string_literals; lit; lit = lit->next) {
    fn(lit, user);
  }
  return true;
}

bool ps_iter_float_literals(float_lit_visitor_t fn, void *user) {
  if (!float_literals) return false;
  for (float_lit_t *lit = float_literals; lit; lit = lit->next) {
    fn(lit, user);
  }
  return true;
}

bool ps_has_string_literals(void) { return string_literals != NULL; }
bool ps_has_float_literals(void) { return float_literals != NULL; }
static int g_last_type_const_qualified = 0;
static int g_last_type_volatile_qualified = 0;
static int g_last_alignas_value = 0;
/* funcdef の外側 declarator (`int (*f(...))(...)`) で `(*` を見たら 1。
 * 戻り値型を関数ポインタ (= ポインタ) として扱うため、parse_func_declarator
 * から funcdef へ伝える。各 funcdef 開始時にリセットする。 */
static int g_last_outer_declarator_is_ptr = 0;
static int g_func_ret_is_funcptr = 0;
static int g_func_funcptr_ret_is_pointer = 0;
/* 戻り値型基底の `*` 段数 (`int **g()` で 2)。parse_pointer_suffix_flags が数え、
 * funcdef が多段ポインタ戻りの記録に使う。各 funcdef 開始時にリセットする。 */
static int g_last_ret_ptr_levels = 0;
/* 戻り値型が「配列へのポインタ」`int (*f())[N]` / `int (*f())[N][M]` のとき、
 * pointee 配列の先頭次元 N と第2次元 M、`[` suffix の個数を捕捉する。 */
static int g_func_ret_pointee_first_dim = 0;
static int g_func_ret_pointee_second_dim = 0;
static int g_func_ret_pointee_dim_count = 0;
static int g_last_decl_is_extern = 0;
static int g_last_decl_is_static = 0;
static int g_toplevel_decl_elem_size = 8;
static int g_toplevel_decl_is_extern = 0;
static int g_toplevel_decl_is_static = 0;
/* _Generic 用: トップレベル宣言の型開始トークン。apply_toplevel_object_from_head が
 * 先頭宣言子の [型開始, 宣言子終端) を name 抜きで文字列化してグローバル sig 表に記録する
 * (consume-once: 捕捉後 NULL にする)。 */
static token_t *g_toplevel_typespec_start = NULL;
static int g_toplevel_decl_is_thread_local = 0;
static int g_toplevel_decl_is_typedef = 0;
static token_kind_t g_toplevel_decl_base_kind = TK_EOF;
static int g_toplevel_decl_is_unsigned = 0;
static tk_float_kind_t g_toplevel_decl_fp_kind = TK_FLOAT_KIND_NONE;
static token_kind_t g_toplevel_decl_tag_kind = TK_EOF;
static char *g_toplevel_decl_tag_name = NULL;
static int g_toplevel_decl_tag_len = 0;
static int g_toplevel_decl_base_is_ptr = 0;
/* 基底型がポインタ typedef のときの段数 (`typedef int **PP; PP gp;` で 2)。多段ポインタ
 * typedef の段数を宣言へ伝えるのと、合成 typedef (`typedef PP X;`) の段数加算に使う。
 * resolve_toplevel_typedef_ref で設定、reset_toplevel_decl_spec_state でクリア。 */
static int g_toplevel_decl_base_pointer_levels = 0;
static unsigned short g_toplevel_decl_base_funcptr_param_fp_mask = 0;
static unsigned short g_toplevel_decl_base_funcptr_param_int_mask = 0;
static psx_ret_pointee_array_t g_toplevel_decl_base_funcptr_ret_pointee_array = {0};
/* 現在パース中のトップレベル宣言子が関数サフィックス `(...)` を持つか。
 * `double (*gops)(double)` のような関数ポインタグローバルを `double *dp` のような
 * データポインタと区別し、戻り型 fp_kind を gv->pointee_fp_kind に保存するのに使う。
 * 宣言子ごとに parse_toplevel_declarator_head でリセットされる。 */
static int g_toplevel_decl_has_func_suffix = 0;
static int g_toplevel_decl_funcptr_ret_is_pointer = 0;
/* 現在パース中のトップレベル宣言子のポインタ段数 (`double *dp`=1, `double **dpp`=2)。
 * psx_consume_pointer_prefix が消費した `*` の数を積算する。宣言子ごとに
 * parse_toplevel_declarator_head でリセットする。単段ポインタ (`double *dp`) に限って
 * pointee fp_kind を保存する判定に使う (多段の pointee は double ではないため)。 */
static int g_toplevel_decl_ptr_levels = 0;
/* 現在パース中のトップレベル宣言子で、ポインタ `*` が括弧グループ内に宣言されたか
 * (`int (*pa)[3]` は 1、`int *pa[3]` は 0)。後続の `[N]` 配列サフィックスと組み合わせて
 * 「配列へのポインタ」(`int (*pa)[3]`) と「ポインタの配列」(`int *pa[3]`) を区別する。
 * 宣言子ごとに parse_toplevel_declarator_head でリセットする。 */
static int g_toplevel_decl_ptr_in_paren_group = 0;
/* 現在パース中のトップレベル宣言子で、括弧内に配列サフィックス `[N]` が現れたか
 * (`int (*g[N])(...)` のような「関数ポインタ/ポインタの配列」)。paren_array_mul は積を
 * 返すだけで N==1 と「配列なし」を区別できないため、要素数 1 の配列 `(*g[1])(...)` が
 * スカラ誤登録されて crash していた。このフラグで配列を保証する。宣言子ごとにリセット。 */
static int g_toplevel_decl_paren_array_present = 0;
/* 括弧内配列の個別次元 (`int (*t[2][2])(void)` で {2,2})。paren_array_mul は積しか持たず
 * 2 次元以上の funcptr/ポインタ配列でストライドが立てられなかったため、dims を別途保持する。
 * 宣言子ごとにリセット。 */
static int g_toplevel_decl_paren_array_dims[8] = {0};
static int g_toplevel_decl_paren_array_dim_count = 0;
static int g_toplevel_decl_pointee_const = 0;
static int g_toplevel_decl_pointee_volatile = 0;
// typedef 由来の配列型の dims (使用側 `M2 g;` で typedef の `[2][3]` を保持)。
// reset_toplevel_decl_spec_state でクリア、resolve_toplevel_typedef_ref で
// 設定。parse_toplevel_array_suffixes が dims を append する。
static int g_toplevel_decl_td_array_dims[8] = {0};
static int g_toplevel_decl_td_array_dim_count = 0;
/* pointer-to-array typedef (`typedef int (*PA)[3]; PA gp;`) のポインティ各次元数。配列
 * typedef の dims (上) とは別管理する: 配列サフィックスへ連結すると gp が「配列」に
 * 誤登録されるため、apply_toplevel_object_from_head の pointer-to-array 経路でのみ使う。
 * dims 自体は g_toplevel_decl_td_array_dims に入る (find が書き込む buffer を共用)。 */
static int g_toplevel_decl_td_ptr_pointee_dim_count = 0;

static node_t *funcdef(void);
static void parse_toplevel_decl_after_type(void);
static int has_next_toplevel_declarator(void);
static token_kind_t resolve_toplevel_typedef_base_kind_for_store(void);
typedef struct {
  token_ident_t *name;
  int is_ptr;
  int paren_array_mul;
} toplevel_declarator_head_t;
static toplevel_declarator_head_t new_toplevel_declarator_head(int base_is_ptr);
static toplevel_declarator_head_t parse_toplevel_declarator_head(int base_is_ptr, int require_name);
static void parse_toplevel_declarator_stmt(int base_is_ptr,
                                           void (*apply)(toplevel_declarator_head_t));
static void parse_toplevel_declarator_list_with_apply(int base_is_ptr,
                                                      void (*apply)(toplevel_declarator_head_t));
static void apply_toplevel_typedef_from_head(toplevel_declarator_head_t head);
static void define_toplevel_typedef_from_declarator(token_ident_t *name, int is_ptr,
                                                    int paren_array_mul);
static void register_toplevel_typedef_name(token_ident_t *name, token_kind_t stored_base_kind,
                                           int is_ptr, int typedef_sizeof, int td_is_array,
                                           int td_first_dim,
                                           const int *td_dims, int td_dim_count,
                                           psx_ret_pointee_array_t funcptr_ret_pointee_array);
static int is_toplevel_typedef_unsigned(token_kind_t stored_base_kind);
static void guard_toplevel_declarator_count(int declarator_count);
static void apply_toplevel_object_from_head(toplevel_declarator_head_t head);
static void finalize_toplevel_object_declarator(global_var_t *gv);
static void apply_toplevel_object_initializer(global_var_t *gv);
static void consume_toplevel_extern_initializer_if_any(void);
static int parse_toplevel_declaration_like(void);
static void parse_toplevel_decl_spec(void);
static int is_toplevel_decl_like_start(token_t *tok);
static void consume_toplevel_typedef_storage_class(void);
static void apply_toplevel_builtin_decl_spec(token_kind_t type_kind);
static void apply_toplevel_typedef_decl_spec(token_kind_t td_base, int td_elem, tk_float_kind_t td_fp,
                                             token_kind_t td_tag, char *td_tag_name, int td_tag_len,
                                             int td_is_ptr, int td_is_unsigned);
static void apply_toplevel_typedef_prefix_flags(void);
static void resolve_toplevel_tag_decl_layout_or_ref(void);
static void reset_toplevel_decl_spec_state(void);
static void skip_post_type_cv_qualifiers(void);
static int parse_toplevel_tag_decl_spec(void);
static int parse_toplevel_typedef_name_spec(void);
static void parse_toplevel_tag_head(token_kind_t *out_kind, char **out_name, int *out_len);
static void parse_toplevel_tag_decl(void);
static token_ident_t *parse_toplevel_decl_name(int *is_ptr, int *out_paren_array_mul);
static token_ident_t *consume_decl_ident_or_error(int require_name);
static void emit_decl_name_required_diag(void);
static void consume_toplevel_paren_decl_func_suffixes_if_any(int had_parens);
static token_ident_t *parse_decl_name_recursive(int *is_ptr, int require_name, int *out_paren_array_mul);
static int is_toplevel_function_signature(token_t *tok);
static int is_tag_return_function_signature(token_t *tok);
static void skip_balanced_group(token_kind_t lkind, token_kind_t rkind);
static token_ident_t *parse_param_declarator_name(int *out_is_array_declarator, int *out_is_pointer_declarator,
                                                  int *out_pointer_levels,
                                                  int *out_inner_first_dim, int *out_inner_second_dim,
                                                  token_ident_t **out_inner_first_dim_ident,
                                                  token_ident_t **out_inner_second_dim_ident,
                                                  int *out_has_func_suffix);
static token_ident_t *parse_param_declarator_name_recursive(int *out_is_array_declarator,
                                                            int *out_is_pointer_declarator,
                                                            int *out_pointer_levels,
                                                            int *out_inner_first_dim,
                                                            int *out_inner_second_dim,
                                                            token_ident_t **out_inner_first_dim_ident,
                                                            token_ident_t **out_inner_second_dim_ident,
                                                            int *out_has_func_suffix);
static int parse_param_decl(node_func_t *node, int *nargs, int *arg_cap, int count_unnamed);
static int is_param_decl_spec_start(void);
typedef struct {
  token_kind_t base_type_kind;
  int saw_typedef_name;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int struct_size;
  int elem_size;
  // typedef した型が配列の場合に typedef 名から取り出した情報を保持する。
  // 例: `typedef int row_t[3]` → typedef_is_array=1, typedef_sizeof_size=12
  // 仮引数 `row_t *a` で pointer-to-array として扱うため。
  int typedef_is_array;
  int typedef_sizeof_size;
  // 多次元 typedef array (`typedef int M[3][4]`) のとき M *p で
  // mid_stride = sizeof_size / first_dim = 16 を計算するのに使う。
  int typedef_array_first_dim;
  // 多次元 typedef array (3+ 次元) の dims を仮引数 (`M *p`) に
  // 反映するため保持する。
  int typedef_array_dims[8];
  int typedef_array_dim_count;
  // float/double 仮引数を ABI に従い d0..d7 で受け取るための種別。
  tk_float_kind_t fp_kind;
  // `double _Complex` / `float _Complex` 仮引数。HFA として 2 FP レジスタで受ける。
  int is_complex;
  // 基底型 (typedef 名) がポインタのとき (`typedef char* Str; f(Str s)`) のポインタ段数。
  // 宣言子側の `*` (param_is_ptr) と合成して実効ポインタ性を決める。非ポインタは 0。
  int base_is_pointer;
  int base_pointer_levels;
  // 基底型が unsigned か (`unsigned char* p` の pointee zero-extend に使う)。
  int is_unsigned;
} param_decl_spec_t;
static int parse_param_tag_decl_spec(param_decl_spec_t *out);
static void parse_param_scalar_decl_spec(param_decl_spec_t *out);
static void parse_param_decl_spec(param_decl_spec_t *out);
static void parse_func_decl_spec(token_kind_t *ret_kind, tk_float_kind_t *ret_fp_kind,
                                 token_ident_t **ret_tag, int *ret_is_ptr,
                                 int *ret_is_unsigned);
static void parse_pointer_suffix_flags(int *out_is_ptr);
static void resolve_func_ret_typedef(token_kind_t *ret_kind, tk_float_kind_t *ret_fp_kind,
                                     token_ident_t **ret_tag, int *ret_is_ptr,
                                     int *ret_is_unsigned);
static void resolve_func_ret_tag_spec(token_kind_t *ret_kind, token_ident_t **ret_tag);
static token_ident_t *parse_func_declarator(int *out_is_variadic, int *out_has_unnamed_param,
                                            node_t ***out_args, int *out_nargs);
static token_ident_t *parse_func_name_declarator_recursive(void);
static void parse_static_assert_toplevel(void);
static token_t *skip_decl_prefix_lookahead(token_t *t);
static token_kind_t parse_atomic_type_specifier(void);
static tk_float_kind_t fp_kind_for_type_kind_toplevel(token_kind_t type_kind);
static void apply_toplevel_decl_prefix_flags(void);
static void resolve_toplevel_typedef_ref(void);
typedef struct {
  int arr_total;
  int is_array;
  int has_incomplete_array;
  // 多次元 typedef array (`typedef int M[3][4]`) で M *p の mid_stride を
  // 求めるため、最も外側 [N] の N を保持する。
  int first_dim;
  // 全次元のサイズを左から順に。dim_count = 個数 (上限 8)。
  int dims[8];
  int dim_count;
} toplevel_array_suffix_t;
static int compute_toplevel_typedef_sizeof(int is_ptr, toplevel_array_suffix_t arr);
static void validate_toplevel_object_array_suffix(toplevel_array_suffix_t arr);
static toplevel_array_suffix_t parse_toplevel_array_suffixes(int base_mul);
static void register_toplevel_function_prototype(token_ident_t *tok, int declarator_is_ptr);
static global_var_t *register_toplevel_object_from_declarator(token_ident_t *name, int is_ptr,
                                                               toplevel_array_suffix_t arr);
static int current_toplevel_extern_flag(void);
static inline token_t *curtok(void);
static inline void set_curtok(token_t *tok);
static int g_last_type_atomic;
static int g_last_type_thread_local;

static tk_float_kind_t fp_kind_for_type_kind_toplevel(token_kind_t type_kind) {
  if (type_kind == TK_FLOAT) return TK_FLOAT_KIND_FLOAT;
  if (type_kind == TK_DOUBLE) return TK_FLOAT_KIND_DOUBLE;
  return TK_FLOAT_KIND_NONE;
}

static void apply_toplevel_decl_prefix_flags(void) {
  psx_take_type_qualifiers(&g_toplevel_decl_pointee_const, &g_toplevel_decl_pointee_volatile);
  g_toplevel_decl_is_extern = g_last_decl_is_extern;
  g_toplevel_decl_is_static = g_last_decl_is_static;
  g_toplevel_decl_is_thread_local = g_last_type_thread_local;
}

static void resolve_toplevel_typedef_ref(void) {
  token_ident_t *id = (token_ident_t *)curtok();
  token_kind_t td_base = TK_EOF;
  int td_elem = 8;
  tk_float_kind_t td_fp = TK_FLOAT_KIND_NONE;
  token_kind_t td_tag = TK_EOF;
  char *td_tag_name = NULL;
  int td_tag_len = 0;
  int td_is_ptr = 0;
  int td_is_array = 0;
  int td_dim_count = 0;
  int td_is_unsigned = 0;
  psx_typedef_info_t _ti;
  if (psx_ctx_find_typedef_name(id->str, id->len, &_ti)) {
    td_base = _ti.base_kind; td_elem = _ti.elem_size; td_fp = _ti.fp_kind;
    td_tag = _ti.tag_kind; td_tag_name = _ti.tag_name; td_tag_len = _ti.tag_len;
    td_is_ptr = _ti.is_pointer; td_is_unsigned = _ti.is_unsigned;
    td_is_array = _ti.is_array; td_dim_count = _ti.array_dim_count;
    g_toplevel_decl_base_funcptr_param_fp_mask = _ti.funcptr_param_fp_mask;
    g_toplevel_decl_base_funcptr_param_int_mask = _ti.funcptr_param_int_mask;
    g_toplevel_decl_base_funcptr_ret_pointee_array =
        psx_ret_pointee_array_make(_ti.funcptr_ret_pointee_array_first_dim,
                                   _ti.funcptr_ret_pointee_array_second_dim,
                                   _ti.funcptr_ret_pointee_array_elem_size);
    for (int i = 0; i < td_dim_count && i < 8; i++) g_toplevel_decl_td_array_dims[i] = _ti.array_dims[i];
  }
  /* 多段ポインタ typedef の段数 (`typedef int **PP` で 2)。単段/非ポインタは 1/0。 */
  g_toplevel_decl_base_pointer_levels = psx_ctx_get_typedef_pointer_levels(id->str, id->len);
  g_toplevel_decl_td_array_dim_count = (td_is_array && td_dim_count > 0) ? td_dim_count : 0;
  /* pointer-to-array typedef (is_ptr かつ is_array でないのに dims を持つ): ポインティ
   * dims を別管理する。配列サフィックス連結 (parse_toplevel_array_suffixes) には混ぜない。 */
  g_toplevel_decl_td_ptr_pointee_dim_count =
      (td_is_ptr && !td_is_array && td_dim_count > 0) ? td_dim_count : 0;
  set_curtok(curtok()->next);
  apply_toplevel_typedef_decl_spec(td_base, td_elem, td_fp, td_tag, td_tag_name, td_tag_len,
                                   td_is_ptr, td_is_unsigned);
}

static void apply_toplevel_typedef_decl_spec(token_kind_t td_base, int td_elem, tk_float_kind_t td_fp,
                                             token_kind_t td_tag, char *td_tag_name, int td_tag_len,
                                             int td_is_ptr, int td_is_unsigned) {
  g_toplevel_decl_base_kind = td_base;
  g_toplevel_decl_is_unsigned = td_is_unsigned ? 1 : 0;
  g_toplevel_decl_fp_kind = td_fp;
  g_toplevel_decl_tag_kind = td_tag;
  g_toplevel_decl_tag_name = td_tag_name;
  g_toplevel_decl_tag_len = td_tag_len;
  g_toplevel_decl_base_is_ptr = td_is_ptr;
  g_toplevel_decl_elem_size = td_elem;
  if ((td_tag == TK_STRUCT || td_tag == TK_UNION) &&
      td_tag_name && td_tag_len > 0 &&
      psx_ctx_has_tag_type(td_tag, td_tag_name, td_tag_len)) {
    int tag_sz = psx_ctx_get_tag_size(td_tag, td_tag_name, td_tag_len);
    if (tag_sz > 0) g_toplevel_decl_elem_size = tag_sz;
  }
}

static void reset_toplevel_decl_spec_state(void) {
  /* storage class フラグを宣言ごとにクリアする。tag/typedef の「修飾子なし」経路は
   * skip_cv_qualifiers を通らない (line 662 の条件付き呼び出しのみ) ため、前の宣言の
   * extern/static が漏れる。例: `extern struct S es; struct S es={7};` の 2 行目が
   * extern 扱いされ finalize が extern 分岐 (consume_toplevel_extern_initializer_if_any)
   * に入り `={7}` の brace を psx_expr_assign で食べて E3064 になっていた。builtin 経路は
   * psx_consume_type_kind 内の skip_cv_qualifiers が毎回リセットするので元から漏れない。 */
  g_last_decl_is_extern = 0;
  g_last_decl_is_static = 0;
  g_toplevel_decl_is_extern = 0;
  g_toplevel_decl_is_static = 0;
  g_toplevel_decl_is_typedef = 0;
  g_toplevel_decl_base_kind = TK_EOF;
  g_toplevel_decl_is_unsigned = 0;
  g_toplevel_decl_fp_kind = TK_FLOAT_KIND_NONE;
  g_toplevel_decl_tag_kind = TK_EOF;
  g_toplevel_decl_tag_name = NULL;
  g_toplevel_decl_tag_len = 0;
  g_toplevel_decl_base_is_ptr = 0;
  g_toplevel_decl_base_pointer_levels = 0;
  g_toplevel_decl_base_funcptr_param_fp_mask = 0;
  g_toplevel_decl_base_funcptr_param_int_mask = 0;
  g_toplevel_decl_base_funcptr_ret_pointee_array = psx_ret_pointee_array_make(0, 0, 0);
  g_toplevel_decl_pointee_const = 0;
  g_toplevel_decl_pointee_volatile = 0;
  g_toplevel_decl_td_array_dim_count = 0;
  g_toplevel_decl_td_ptr_pointee_dim_count = 0;
  for (int i = 0; i < 8; i++) g_toplevel_decl_td_array_dims[i] = 0;
}

static int parse_toplevel_tag_decl_spec(void) {
  if (!psx_ctx_is_tag_keyword(curtok()->kind)) return 0;
  parse_toplevel_tag_head(&g_toplevel_decl_tag_kind, &g_toplevel_decl_tag_name, &g_toplevel_decl_tag_len);
  g_toplevel_decl_base_kind = g_toplevel_decl_tag_kind;
  resolve_toplevel_tag_decl_layout_or_ref();
  skip_post_type_cv_qualifiers();
  g_toplevel_decl_elem_size = psx_ctx_get_tag_size(g_toplevel_decl_tag_kind,
                                                   g_toplevel_decl_tag_name, g_toplevel_decl_tag_len);
  apply_toplevel_decl_prefix_flags();
  return 1;
}

static void resolve_toplevel_tag_decl_layout_or_ref(void) {
  if (tk_consume('{')) {
    int member_count = 0;
    int tag_size = 0;
    member_count = psx_parse_tag_definition_body(g_toplevel_decl_tag_kind, g_toplevel_decl_tag_name,
                                                      g_toplevel_decl_tag_len, &tag_size);
    psx_ctx_define_tag_type_with_layout(g_toplevel_decl_tag_kind, g_toplevel_decl_tag_name,
                                        g_toplevel_decl_tag_len, member_count, tag_size);
    return;
  }
  if (psx_ctx_has_tag_type(g_toplevel_decl_tag_kind, g_toplevel_decl_tag_name, g_toplevel_decl_tag_len)) return;
  if (g_toplevel_decl_is_typedef &&
      (g_toplevel_decl_tag_kind == TK_STRUCT || g_toplevel_decl_tag_kind == TK_UNION)) {
    psx_ctx_define_tag_type(g_toplevel_decl_tag_kind, g_toplevel_decl_tag_name, g_toplevel_decl_tag_len);
    return;
  }
  /* 未完了タグの前方宣言 (`enum E *e;` / `struct S *s;` 等)。`enum E;` と同様に登録する。 */
  psx_ctx_define_tag_type(g_toplevel_decl_tag_kind, g_toplevel_decl_tag_name, g_toplevel_decl_tag_len);
}

static void parse_toplevel_tag_head(token_kind_t *out_kind, char **out_name, int *out_len) {
  *out_kind = curtok()->kind;
  set_curtok(curtok()->next);
  psx_skip_gnu_attributes();
  token_ident_t *tag = tk_consume_ident();
  if (!tag && curtok()->kind != TK_LBRACE) {
    psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_TAG_NAME));
  }
  if (tag) {
    *out_name = tag->str;
    *out_len = tag->len;
  } else {
    psx_make_anonymous_tag_name(out_name, out_len);
  }
}

static int parse_toplevel_typedef_name_spec(void) {
  if (!psx_ctx_is_typedef_name_token(curtok())) return 0;
  resolve_toplevel_typedef_ref();
  apply_toplevel_typedef_prefix_flags();
  return 1;
}

static void apply_toplevel_typedef_prefix_flags(void) {
  /* extern/static を伝播する (builtin/tag 経路の apply_toplevel_decl_prefix_flags と同じ)。
   * 以前は extern を無条件に 0 にしていたため `extern S es; S es={9};` (S は typedef) の
   * extern 宣言が tentative 定義扱いになり `.comm _es` を出し、本定義の data 出力と重複
   * シンボルで ASSEMBLE_FAIL していた。前宣言からのフラグ漏れは reset_toplevel_decl_spec_state
   * が宣言ごとに 0 クリアするので、ここで g_last_* を伝播しても誤って extern にはならない。 */
  g_toplevel_decl_is_extern = g_last_decl_is_extern;
  g_toplevel_decl_is_static = g_last_decl_is_static;
  g_toplevel_decl_is_thread_local = 0;
  psx_take_type_qualifiers(&g_toplevel_decl_pointee_const, &g_toplevel_decl_pointee_volatile);
}

bool psx_is_decl_prefix_token(token_kind_t k) {
  return k == TK_CONST || k == TK_VOLATILE || k == TK_EXTERN || k == TK_STATIC ||
         k == TK_AUTO || k == TK_REGISTER || k == TK_INLINE || k == TK_NORETURN ||
         k == TK_THREAD_LOCAL || k == TK_ALIGNAS || k == TK_ATOMIC;
}

bool psx_is_gnu_attribute_token(const token_t *t) {
  if (!t || t->kind != TK_IDENT) return 0;
  const token_ident_t *id = (const token_ident_t *)t;
  return id->len == 13 && memcmp(id->str, "__attribute__", 13) == 0;
}

void psx_skip_gnu_attributes_at(token_t **t) {
  while (*t && psx_is_gnu_attribute_token(*t)) {
    *t = (*t)->next;
    if (!*t || (*t)->kind != TK_LPAREN) continue;
    int depth = 0;
    while (*t) {
      if ((*t)->kind == TK_LPAREN) depth++;
      else if ((*t)->kind == TK_RPAREN) {
        depth--;
        *t = (*t)->next;
        if (depth == 0) break;
        continue;
      }
      *t = (*t)->next;
    }
  }
}

void psx_skip_gnu_attributes(void) {
  while (psx_is_gnu_attribute_token(curtok())) {
    token_t *t = curtok();
    psx_skip_gnu_attributes_at(&t);
    set_curtok(t);
  }
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

static inline token_t *curtok(void) {
  return tk_get_current_token();
}

static inline void set_curtok(token_t *tok) {
  tk_set_current_token(tok);
}

static void skip_cv_qualifiers(void) {
  g_last_type_const_qualified = 0;
  g_last_type_volatile_qualified = 0;
  g_last_alignas_value = 0;
  g_last_decl_is_extern = 0;
  g_last_decl_is_static = 0;
  /* C11 6.7.1p2: 宣言指定子に storage class 指定子は高々 1 個。
   * 例外として _Thread_local は static / extern と一緒に書ける。 */
  int storage_count = 0;
  int saw_thread_local = 0;
  token_t *first_storage_tok = NULL;
  while (psx_is_decl_prefix_token(curtok()->kind)) {
    if (curtok()->kind == TK_CONST) g_last_type_const_qualified = 1;
    if (curtok()->kind == TK_VOLATILE) g_last_type_volatile_qualified = 1;
    if (curtok()->kind == TK_EXTERN) g_last_decl_is_extern = 1;
    if (curtok()->kind == TK_STATIC) g_last_decl_is_static = 1;
    if (curtok()->kind == TK_EXTERN || curtok()->kind == TK_STATIC ||
        curtok()->kind == TK_AUTO || curtok()->kind == TK_REGISTER) {
      if (!first_storage_tok) first_storage_tok = curtok();
      storage_count++;
    }
    if (curtok()->kind == TK_THREAD_LOCAL) saw_thread_local = 1;
    if (curtok()->kind == TK_ALIGNAS) {
      set_curtok(curtok()->next);
      if (curtok()->kind != TK_LPAREN) {
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_ALIGNAS_LPAREN_REQUIRED));
      }
      int av = psx_parse_alignas_value();
      if (av > g_last_alignas_value) g_last_alignas_value = av;
      continue;
    }
    if (curtok()->kind == TK_ATOMIC && curtok()->next && curtok()->next->kind == TK_LPAREN) {
      return;
    }
    if (curtok()->kind == TK_ATOMIC) {
      g_last_type_atomic = 1;
    }
    if (curtok()->kind == TK_THREAD_LOCAL) {
      g_last_type_thread_local = 1;
    }
    set_curtok(curtok()->next);
  }
  /* storage class が 2 個以上同時指定されているとエラー。
   * `_Thread_local` 単独は storage_count に数えていないので
   * `_Thread_local int x;` は 0 で通り、`static _Thread_local int x;` は 1 で通る。 */
  if (storage_count > 1) {
    psx_diag_ctx(first_storage_tok, "decl",
                 "storage class 指定子は1つまでです (C11 6.7.1p2)");
  }
  (void)saw_thread_local;
  psx_skip_gnu_attributes();
}

void psx_take_type_qualifiers(int *is_const_qualified, int *is_volatile_qualified) {
  if (is_const_qualified) *is_const_qualified = g_last_type_const_qualified;
  if (is_volatile_qualified) *is_volatile_qualified = g_last_type_volatile_qualified;
}

void psx_take_alignas_value(int *align) {
  if (align) *align = g_last_alignas_value;
}

void psx_take_extern_flag(int *is_extern) {
  if (is_extern) *is_extern = g_last_decl_is_extern;
}

void psx_take_static_flag(int *is_static) {
  if (is_static) *is_static = g_last_decl_is_static;
  g_last_decl_is_static = 0;
}

/* `static struct T x;` のように storage class を tag-keyword 経路 (stmt.c) で
 * 手動スキップする場合、skip_cv_qualifiers を経由しないため g_last_decl_is_static が
 * 立たない。スキップ時に static を検出したらこの setter で記録する。 */
void psx_set_static_flag(int is_static) {
  g_last_decl_is_static = is_static ? 1 : 0;
}

void psx_set_extern_flag(int is_extern) {
  g_last_decl_is_extern = is_extern ? 1 : 0;
}

/* tag 経路 (`_Alignas(N) struct T x;`) で、プレフィックス先読み消費時に捕捉した
 * _Alignas 値を、tag 定義パース後 (skip_cv_qualifiers がリセットする) に復元する。 */
void psx_set_alignas_value(int align) {
  if (align > g_last_alignas_value) g_last_alignas_value = align;
}

static void skip_ptr_qualifiers(void) {
  while (curtok()->kind == TK_CONST || curtok()->kind == TK_VOLATILE || curtok()->kind == TK_RESTRICT) {
    set_curtok(curtok()->next);
  }
}

/* 型指定子の直後 (`enum E const *p` 等) の cv 修飾子を読み飛ばす。 */
static void skip_post_type_cv_qualifiers(void) {
  while (curtok()->kind == TK_CONST || curtok()->kind == TK_VOLATILE ||
         curtok()->kind == TK_RESTRICT) {
    if (curtok()->kind == TK_CONST) g_last_type_const_qualified = 1;
    if (curtok()->kind == TK_VOLATILE) g_last_type_volatile_qualified = 1;
    set_curtok(curtok()->next);
  }
  if (curtok()->kind == TK_ATOMIC && curtok()->next && curtok()->next->kind != TK_LPAREN) {
    g_last_type_atomic = 1;
    set_curtok(curtok()->next);
  }
  psx_skip_gnu_attributes();
}

int psx_consume_pointer_prefix_counted(int *is_ptr) {
  int count = 0;
  while (tk_consume('*')) {
    if (is_ptr) *is_ptr = 1;
    /* トップレベル宣言子のポインタ段数を積算する (他経路の呼び出しでも増えるが、
     * g_toplevel_decl_ptr_levels は parse_toplevel_declarator_head でのリセット後に
     * register_toplevel_global_decl が読むだけなので影響しない)。 */
    g_toplevel_decl_ptr_levels++;
    count++;
    skip_ptr_qualifiers();
  }
  return count;
}

void psx_consume_pointer_prefix(int *is_ptr) {
  (void)psx_consume_pointer_prefix_counted(is_ptr);
}

static void parse_static_assert_toplevel(void) {
  if (curtok()->kind != TK_STATIC_ASSERT) {
    diag_emit_tokf(DIAG_ERR_PARSER_STATIC_ASSERT_EXPECTED, curtok(), "%s",
                   diag_message_for(DIAG_ERR_PARSER_STATIC_ASSERT_EXPECTED));
  }
  set_curtok(curtok()->next);
  tk_expect('(');
  long long cond_val = psx_parse_enum_const_expr();
  tk_expect(',');
  if (curtok()->kind != TK_STRING) {
    diag_emit_tokf(DIAG_ERR_PARSER_STATIC_ASSERT_MSG_NOT_STRING, curtok(), "%s",
                   diag_message_for(DIAG_ERR_PARSER_STATIC_ASSERT_MSG_NOT_STRING));
  }
  set_curtok(curtok()->next);
  tk_expect(')');
  tk_expect(';');
  if (cond_val == 0) {
    diag_emit_tokf(DIAG_ERR_PARSER_STATIC_ASSERT_FAILED, curtok(), "%s",
                   diag_message_for(DIAG_ERR_PARSER_STATIC_ASSERT_FAILED));
  }
}

static token_t *skip_decl_prefix_lookahead(token_t *t) {
  while (t && psx_is_decl_prefix_token(t->kind)) {
    if (t->kind == TK_ALIGNAS) {
      t = t->next;
      if (!t || t->kind != TK_LPAREN) return t;
      int depth = 1;
      t = t->next;
      while (t && depth > 0) {
        if (t->kind == TK_LPAREN) depth++;
        else if (t->kind == TK_RPAREN) depth--;
        t = t->next;
      }
      continue;
    }
    if (t->kind == TK_ATOMIC && t->next && t->next->kind == TK_LPAREN) {
      int depth = 0;
      t = t->next;
      while (t) {
        if (t->kind == TK_LPAREN) depth++;
        else if (t->kind == TK_RPAREN) {
          depth--;
          if (depth == 0) {
            t = t->next;
            break;
          }
        }
        t = t->next;
      }
      continue;
    }
    t = t->next;
  }
  return t;
}

static token_kind_t parse_atomic_type_specifier(void) {
  if (curtok()->kind != TK_ATOMIC) return TK_EOF;
  set_curtok(curtok()->next);
  if (!tk_consume('(')) {
    // qualifier-form: "_Atomic int" は前置指定子として扱う
    return TK_EOF;
  }
  token_kind_t inner = psx_consume_type_kind();
  if (inner == TK_EOF) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_ATOMIC_TYPE_NAME_REQUIRED));
  }
  // Minimal support for derived declarators in _Atomic(type), e.g. _Atomic(int*).
  while (tk_consume('*')) {
    while (curtok()->kind == TK_CONST || curtok()->kind == TK_VOLATILE || curtok()->kind == TK_RESTRICT) {
      set_curtok(curtok()->next);
    }
  }
  tk_expect(')');
  return inner;
}

/* 先頭の storage-class / cv 修飾子を読み飛ばした先に tag キーワード
 * (struct/union/enum) が来るか先読みする。`static struct S g` のように
 * 修飾子が tag の前にある形を判定する。builtin 型 (`static int`) は
 * psx_consume_type_kind が内部で skip するためここでは対象にしない。 */
static int toplevel_prefix_precedes_tag(void) {
  if (!psx_is_decl_prefix_token(curtok()->kind)) return 0;
  token_t *t = curtok();
  while (t && psx_is_decl_prefix_token(t->kind)) t = t->next;
  return t && psx_ctx_is_tag_keyword(t->kind);
}

/* tag と同様、storage class / cv 修飾が typedef 名の前にあるか。`static Point p;` で
 * skip しないと Point が型と認識されず E2006 (`;` 期待) になっていた。 */
static int toplevel_prefix_precedes_typedef_name(void) {
  if (!psx_is_decl_prefix_token(curtok()->kind)) return 0;
  token_t *t = curtok();
  while (t && psx_is_decl_prefix_token(t->kind)) t = t->next;
  return t && t != curtok() && psx_ctx_is_typedef_name_token(t);
}

static void parse_toplevel_decl_spec(void) {
  reset_toplevel_decl_spec_state();
  consume_toplevel_typedef_storage_class();

  /* `static struct S g;` 等、storage class が tag の前にある場合は先に修飾子を
   * 消費する。これをしないと parse_toplevel_tag_decl_spec が tag キーワードを
   * 見つけられず E3016 になっていた (static int 等の builtin 経路は別処理)。 */
  if (toplevel_prefix_precedes_tag() || toplevel_prefix_precedes_typedef_name()) {
    skip_cv_qualifiers();
  }

  if (parse_toplevel_tag_decl_spec()) return;

  if (parse_toplevel_typedef_name_spec()) return;

  token_kind_t tl_kind = psx_consume_type_kind();
  apply_toplevel_builtin_decl_spec(tl_kind);
  apply_toplevel_decl_prefix_flags();
}

static void consume_toplevel_typedef_storage_class(void) {
  if (curtok()->kind != TK_TYPEDEF) return;
  g_toplevel_decl_is_typedef = 1;
  set_curtok(curtok()->next);
}

static void apply_toplevel_builtin_decl_spec(token_kind_t type_kind) {
  g_toplevel_decl_base_kind = type_kind;
  /* unsigned 修飾を保持する。`unsigned int` は base_kind=TK_UNSIGNED にするが、
   * `unsigned long/char/short` は base_kind=TK_LONG 等のままなので別フラグで覚える。 */
  g_toplevel_decl_is_unsigned = (type_kind == TK_UNSIGNED) || psx_last_type_is_unsigned();
  if (type_kind == TK_INT && psx_last_type_is_unsigned()) {
    g_toplevel_decl_base_kind = TK_UNSIGNED;
  }
  g_toplevel_decl_fp_kind = fp_kind_for_type_kind_toplevel(type_kind);
  g_toplevel_decl_elem_size = 8;
  if (type_kind != TK_EOF) psx_ctx_get_type_info(type_kind, NULL, &g_toplevel_decl_elem_size);
}

// 現在のトークンが #pragma pack マーカーなら対応する関数を呼んで消費し true を返す。
// プリプロセッサはマーカーを出現位置に挿入するだけなので、トップレベルだけでなく
// 関数本体のブロック内でも遭遇しうる。透過的に処理する。
bool psx_try_consume_pragma_pack_marker(void) {
  token_kind_t k = curtok()->kind;
  if (k == TK_PRAGMA_PACK_PUSH) {
    pragma_pack_push((int)((token_num_int_t *)curtok())->val);
    set_curtok(curtok()->next);
    return true;
  }
  if (k == TK_PRAGMA_PACK_POP) {
    pragma_pack_pop();
    set_curtok(curtok()->next);
    return true;
  }
  if (k == TK_PRAGMA_PACK_SET) {
    pragma_pack_set((int)((token_num_int_t *)curtok())->val);
    set_curtok(curtok()->next);
    return true;
  }
  if (k == TK_PRAGMA_PACK_RESET) {
    pragma_pack_reset();
    set_curtok(curtok()->next);
    return true;
  }
  return false;
}

// program = funcdef*
/* ストリーミングパースの状態 (現在の tokenizer ctx)。トークン位置自体はグローバルな
 * current token が保持するので、ここでは ctx 同期用に保持するだけ。 */
static tokenizer_context_t *g_stream_tk_ctx = NULL;

void ps_stream_begin(tokenizer_context_t *tk_ctx, token_t *start) {
  g_stream_tk_ctx = tk_ctx;
  if (tk_ctx) {
    tk_set_current_token_ctx(tk_ctx, start);
  }
  tk_set_current_token(start);
  /* 翻訳単位境界で関数名テーブルを初期化。
   * テストが同プロセスで複数プログラムを処理しても前回の登録が漏れないようにする。 */
  psx_ctx_reset_function_names();
}

node_t *ps_next_function(void) {
  while (!tk_at_eof()) {
    if (psx_try_consume_pragma_pack_marker()) continue;
    if (psx_ctx_is_tag_keyword(curtok()->kind)) {
      if (!is_tag_return_function_signature(curtok())) {
        parse_toplevel_tag_decl();
        continue;
      }
      // struct/union Tag func(...) — 戻り値型がタグ型の関数定義: funcdef() へ fall through
    }
    if (parse_toplevel_declaration_like()) {
      continue;
    }
    node_t *fn = funcdef();
    if (!fn) continue; // 関数プロトタイプ宣言はASTへ載せない
    if (g_stream_tk_ctx) {
      tk_set_current_token_ctx(g_stream_tk_ctx, tk_get_current_token());
    }
    return fn;
  }
  if (g_stream_tk_ctx) {
    tk_set_current_token_ctx(g_stream_tk_ctx, tk_get_current_token());
  }
  return NULL;
}

void ps_free_processed_ast(void) {
  /* 直前に処理した関数 (および直前の非関数トップレベル宣言) の AST を解放する。
   * AST ノードは全て parser arena 上にあり、関数間で参照されない (永続データ —
   * 文字列ラベル・グローバル名・mangled static-local 名等 — は arena 外)。
   * codegen が IR 経由で AST の funcname を alias するため、必ず 1 関数の codegen を
   * 終えてから呼ぶこと。 */
  arena_free_all();
}

node_t **ps_program_ctx(tokenizer_context_t *tk_ctx, token_t *start) {
  ps_stream_begin(tk_ctx, start);
  int cap = 16;
  node_t **codes = calloc(cap, sizeof(node_t*));
  int i = 0;
  node_t *fn;
  while ((fn = ps_next_function()) != NULL) {
    if (i >= cap - 1) { // NULL終端用
      cap = pda_next_cap(cap, i + 2);
      codes = pda_xreallocarray(codes, (size_t)cap, sizeof(node_t *));
    }
    codes[i++] = fn;
  }
  codes[i] = NULL;
  return codes;
}

node_t **ps_program_from(token_t *start) {
  /* 新しいコンパイル開始時に、前回のパースが残した診断フラグをクリアする。
   * これは「同一プロセス内で複数回 ps_program_from を呼ぶ」ユニットテスト用のリセット
   * (実コンパイルは 1 ファイル 1 プロセスなので影響なし)。これがないと前回パースの
   * `int g=1;` の has_init=1 や前回 funcdef の is_defined=1 が次回パースに漏れて、
   * 重複定義チェック等が誤って発火する。 */
  for (global_var_t *gv = global_vars; gv; gv = gv->next) {
    gv->has_init = 0;
  }
  psx_ctx_reset_function_diag_state();
  psx_ctx_reset_tag_diag_state();
  return ps_program_ctx(NULL, start);
}

node_t **ps_program(void) {
  return ps_program_ctx(NULL, tk_get_current_token());
}

/* 型 spec (builtin / typedef 名 / タグ) の直後 t から、関数宣言子のシグネチャかを判定する。
 * `*name(` / `(*f())(...)` (関数ポインタ・配列へのポインタ戻り) / `(name)(...)` を扱う。
 * builtin/typedef/tag のどの戻り型でも同一なので共有する (tag 版に `(*...)` が無かったため
 * `struct S (*f())[3]` が変数と誤判定され E2006 になっていた)。 */
static int is_function_declarator_sig(token_t *t) {
  while (t && (t->kind == TK_MUL || t->kind == TK_CONST || t->kind == TK_VOLATILE)) t = t->next;
  if (!t) return 0;
  if (t->kind == TK_IDENT) {
    return t->next && t->next->kind == TK_LPAREN;
  }
  // function declarator returning function pointer / pointer-to-array:
  //   int (*f(void))(int)  /  int (*f(void))[3]  /  int (*(*f(void))(int))[3]
  if (t->kind == TK_LPAREN && t->next && t->next->kind == TK_MUL) {
    int depth = 0;
    int saw_name = 0;
    int saw_param = 0;
    token_t *u = t;
    while (u) {
      if (u->kind == TK_LPAREN) {
        if (depth >= 1 && saw_name && !saw_param) saw_param = 1;
        depth++;
      } else if (u->kind == TK_RPAREN) {
        depth--;
        if (depth == 0) {
          u = u->next;
          break;
        }
      } else if (depth >= 1 && !saw_name && u->kind == TK_IDENT) {
        // name must be followed by a parameter list: f(...)
        if (u->next && u->next->kind == TK_LPAREN) {
          saw_name = 1;
        }
      }
      u = u->next;
    }
    if (!saw_name || !saw_param || !u) return 0;
    return u->kind == TK_LPAREN || u->kind == TK_LBRACKET;
  }
  // parenthesized function declarator name: int (f)(...)
  if (t->kind == TK_LPAREN) {
    int depth = 0;
    while (t && t->kind == TK_LPAREN) {
      depth++;
      t = t->next;
    }
    if (!t || t->kind != TK_IDENT) return 0;
    t = t->next;
    while (depth-- > 0) {
      if (!t || t->kind != TK_RPAREN) return 0;
      t = t->next;
    }
    return t && t->kind == TK_LPAREN;
  }
  return 0;
}

/* 型指定子の後、宣言子列にトップレベル `,` があるか (`int f(int), g(int), a;` 等)。
 * 関数定義 `int main() {` は `)` の次が `{` なので偽。単一プロトタイプ `int f(int);` も偽。 */
static int toplevel_decl_has_comma_separated_declarators(token_t *tok) {
  token_t *t = skip_decl_prefix_lookahead(tok);
  if (!t) return 0;
  if (psx_ctx_is_tag_keyword(t->kind)) {
    t = t->next;
    if (t && t->kind == TK_IDENT) t = t->next;
  } else if (psx_ctx_is_type_token(t->kind)) {
    while (t && psx_ctx_is_type_token(t->kind)) t = t->next;
  } else if (psx_ctx_is_typedef_name_token(t)) {
    t = t->next;
  } else {
    return 0;
  }
  if (!t) return 0;
  int depth = 0;
  for (; t && t->kind != TK_EOF; t = t->next) {
    if (depth == 0 && t->kind == TK_SEMI) return 0;
    if (depth == 0 && t->kind == TK_LBRACE) return 0;
    if (depth == 0 && t->kind == TK_COMMA) return 1;
    if (t->kind == TK_LPAREN || t->kind == TK_LBRACKET) depth++;
    else if (t->kind == TK_RPAREN || t->kind == TK_RBRACKET) depth--;
  }
  return 0;
}

static int is_toplevel_function_signature(token_t *tok) {
  if (!tok) return 0;
  token_t *t = skip_decl_prefix_lookahead(tok);
  if (!t) return 0;
  /* タグ戻り型 (`static struct S *g(void){...}`): storage class を飛ばした後がタグ
   * キーワードなら専用判定へ委譲する。これがないと struct/union/enum はここで弾かれ、
   * `static struct S *g()` がオブジェクト宣言と誤判定され `;` 期待で E2006 になっていた。 */
  if (psx_ctx_is_tag_keyword(t->kind)) {
    return is_tag_return_function_signature(t);
  }
  if (psx_ctx_is_type_token(t->kind)) {
    // 複合型キーワード（unsigned long 等）を全てスキップ
    while (t && psx_ctx_is_type_token(t->kind)) t = t->next;
  } else if (psx_ctx_is_typedef_name_token(t)) {
    t = t->next; // typedef 名は1トークン
  } else {
    return 0;
  }
  return is_function_declarator_sig(t);
}

// struct/union Tag [*] ident ( のパターンを検出（戻り値型がタグ型の関数定義）
static int is_tag_return_function_signature(token_t *tok) {
  if (!tok || !psx_ctx_is_tag_keyword(tok->kind)) return 0;
  token_t *t = tok->next; // skip struct/union keyword
  if (!t) return 0;
  if (t->kind == TK_IDENT) t = t->next; // optional tag name
  if (!t) return 0;
  if (t->kind == TK_LBRACE) {
    int depth = 1;
    t = t->next;
    while (t && depth > 0) {
      if (t->kind == TK_LBRACE) depth++;
      else if (t->kind == TK_RBRACE) depth--;
      t = t->next;
    }
    if (!t) return 0;
  }
  /* タグ名/本体の後は builtin/typedef と同じ宣言子判定。これで `struct S (*f())[3]`
   * (配列へのポインタ戻り) や `struct S (*f())(int)` (関数ポインタ戻り) も検出できる。 */
  return is_function_declarator_sig(t);
}

static global_var_t *find_global_var_by_name(char *name, int len) {
  return psx_find_global_var(name, len);
}

static global_var_t *register_toplevel_global_decl(char *name, int len, int is_ptr,
                                                   int is_array, int arr_total, int is_extern_decl,
                                                   int has_incomplete_array) {
  /* 同名関数とのコンフリクト検出 (C11 6.7p4: 関数と変数は同じ名前空間)。
   * `int foo(int){...} int foo;` のように関数として登録済みなら違法。 */
  if (psx_ctx_has_function_name(name, len)) {
    psx_diag_ctx(curtok(), "decl",
                 "'%.*s' は関数として既に宣言されています (C11 6.7p4)",
                 len, name);
  }
  /* 同名 typedef とのコンフリクト検出 (C11 6.7p4)。
   * `typedef int T; int T = 5;` — typedef name と通常の identifier は同じ名前空間。 */
  {
    psx_typedef_info_t _ti;
    if (psx_ctx_find_typedef_name(name, len, &_ti)) {
      psx_diag_ctx(curtok(), "decl",
                   "'%.*s' は typedef 名として既に宣言されています (C11 6.7p4)",
                   len, name);
    }
  }
  /* 同名 enum 定数とのコンフリクト検出 (C11 6.7p4)。
   * `enum E { A = 5 }; int A = 10;` — enum 定数は通常の identifier と同じ名前空間。 */
  {
    long long ev;
    if (psx_ctx_find_enum_const(name, len, &ev)) {
      psx_diag_ctx(curtok(), "decl",
                   "'%.*s' は enum 定数として既に宣言されています (C11 6.7p4)",
                   len, name);
    }
  }
  /* C11 6.9.2 / 6.7p4: 同名グローバル変数の重複宣言。型互換性 (type_size + fp_kind +
   * tag_kind + is_array) を確認し、不一致なら E3064。一致なら既存を返して merge する。
   * extern → 非 extern (定義) の場合は is_extern_decl をクリアして通常 data 出力に切り替える。
   * 両方に初期化があれば apply_toplevel_object_initializer の `=` 検出時に重複定義エラー。 */
  int new_type_size = has_incomplete_array ? 0
                       : (is_array ? ((is_ptr ? 8 : g_toplevel_decl_elem_size) * arr_total)
                                   : (is_ptr ? 8 : g_toplevel_decl_elem_size));
  global_var_t *existing = find_global_var_by_name(name, len);
  if (existing) {
    int type_compatible =
        (existing->type_size == 0 || new_type_size == 0 ||
         existing->type_size == new_type_size) &&
        existing->fp_kind == (unsigned char)g_toplevel_decl_fp_kind &&
        existing->tag_kind == g_toplevel_decl_tag_kind &&
        (unsigned)existing->is_array == (unsigned)is_array;
    if (!type_compatible) {
      psx_diag_ctx(curtok(), "decl",
                   "グローバル変数 '%.*s' の型が以前の宣言と異なります (C11 6.7p4)",
                   len, name);
    }
    if (existing->is_extern_decl && !is_extern_decl) {
      existing->is_extern_decl = 0;
      if (existing->type_size == 0 && new_type_size > 0) {
        existing->type_size = new_type_size;
      }
    }
    return existing;
  }
  global_var_t *gv = calloc(1, sizeof(global_var_t));
  gv->name = name;
  gv->name_len = len;
  int elem_store_size = is_ptr ? 8 : g_toplevel_decl_elem_size;
  gv->type_size = has_incomplete_array ? 0 : (is_array ? (elem_store_size * arr_total) : elem_store_size);
  /* deref_size はスカラ単体 (is_array=0) のポインタ変数では pointee サイズ。
   * `char *p` なら 1、`int *p` なら 4。subscript / `p[i]` のステップに使う。
   * 配列 (`int arr[N]`) の場合は要素サイズ (elem_store_size) を保持する。 */
  gv->deref_size = (is_ptr && !is_array) ? g_toplevel_decl_elem_size : elem_store_size;
  /* `char *names[N]` のような「ポインタ配列」では、各要素 (ポインタ値) の deref_size
   * は要素サイズ (8) になり、pointee の素のサイズ (char なら 1) が失われる。
   * 2 段 subscript (names[i][j]) が正しく動くよう pointee 要素サイズを保存する。 */
  gv->pointee_elem_size = (is_ptr && is_array) ? g_toplevel_decl_elem_size : 0;
  gv->is_array = is_array;
  gv->is_extern_decl = is_extern_decl;
  /* static (内部リンケージ): codegen で .global を抑制し、暫定定義を .comm でなく
   * .zerofill (ローカル) に出す。extern 宣言のみのときは linkage 対象外。 */
  gv->is_static = is_extern_decl ? 0 : g_toplevel_decl_is_static;
  /* tag (struct / union) 情報を decl spec から引き継ぐ。
   * is_ptr のときは is_tag_pointer=1 を立て、`pp->x` のメンバアクセスで
   * build_member_access が tag を引けるようにする。 */
  psx_decl_set_gvar_tag(gv, g_toplevel_decl_tag_kind, g_toplevel_decl_tag_name,
                         g_toplevel_decl_tag_len, is_ptr);
  /* 浮動小数スカラのとき fp_kind を引き継ぐ。ポインタは整数として扱う。 */
  gv->fp_kind = is_ptr ? (unsigned char)TK_FLOAT_KIND_NONE
                       : (unsigned char)g_toplevel_decl_fp_kind;
  /* 関数ポインタグローバル `double (*gops)(double)`: 戻り型の fp_kind を pointee_fp_kind
   * に保存する (fp_kind は codegen がビットパターン出力に使うので流用不可)。単段ポインタ
   * (ptr_levels==1) かつ has_func_suffix に限定。データポインタ `double *dp` の pointee
   * fp_kind は apply_toplevel_object_from_head が設定する (pointer-to-array `double (*pa)[N]`
   * を除外するため head.paren_array_mul を見る必要がある)。 */
  gv->pointee_fp_kind = (is_ptr && g_toplevel_decl_has_func_suffix &&
                         g_toplevel_decl_ptr_levels == 1)
                            ? (unsigned char)g_toplevel_decl_fp_kind
                            : (unsigned char)TK_FLOAT_KIND_NONE;
  /* _Bool スカラ: 代入/初期化を 0/1 に正規化するため記録する。 */
  gv->is_bool = (!is_ptr && !is_array && g_toplevel_decl_base_kind == TK_BOOL) ? 1 : 0;
  gv->elem_is_bool = (!is_ptr && is_array && g_toplevel_decl_base_kind == TK_BOOL) ? 1 : 0;
  /* unsigned スカラ/配列要素: load を zero-extend / 比較を unsigned にするため記録。
   * スカラは node の is_unsigned、配列は pointee_is_unsigned に使う (ポインタ値
   * 自体は unsigned ではないので is_ptr は除外)。 */
  gv->is_unsigned = (!is_ptr && g_toplevel_decl_is_unsigned) ? 1 : 0;
  gv->is_const_qualified = g_toplevel_decl_pointee_const ? 1 : 0;
  gv->is_volatile_qualified = g_toplevel_decl_pointee_volatile ? 1 : 0;
  /* 多段ポインタグローバル (`int **gp` / pointer typedef `PP gp`) の段数 = 宣言子の
   * `*` 数 + 基底ポインタ typedef の段数。`*gp` が int* を返すよう、参照ノード構築時に
   * deref_size=8 等を立てるために記録する (単段以下は意味なし)。 */
  if (is_ptr && !is_array) {
    int lvls = g_toplevel_decl_ptr_levels + g_toplevel_decl_base_pointer_levels;
    gv->pointer_qual_levels = (lvls > 0 && lvls < 256) ? (unsigned char)lvls : 0;
  }
  psx_register_global_var(gv);
  return gv;
}

void psx_skip_func_suffix_groups(int *out_has_func_suffix) {
  psx_reset_funcptr_signature_state();
  while (curtok()->kind == TK_LPAREN) {
    if (out_has_func_suffix) *out_has_func_suffix = 1;
    psx_skip_func_param_list();
  }
}

static toplevel_array_suffix_t parse_toplevel_array_suffixes(int base_mul) {
  toplevel_array_suffix_t out = {0};
  out.arr_total = (base_mul > 0) ? base_mul : 1;
  out.is_array = (base_mul > 1);
  out.has_incomplete_array = 0;
  out.first_dim = 0;
  int dim_count = 0;
  while (tk_consume('[')) {
    int has_size = 0;
    int n = psx_parse_array_size_optional_constexpr(&has_size);
    if (!has_size) {
      out.has_incomplete_array = 1;
    } else {
      out.arr_total *= n;
      if (dim_count == 0) out.first_dim = n;
    }
    if (dim_count < 8) {
      out.dims[dim_count] = has_size ? n : 0;
    }
    dim_count++;
    out.is_array = 1;
  }
  // 使用側 typedef 配列 (`typedef int M[3][4]; M g;`) では typedef dims を後ろに
  // 連結する。`M g[2];` のときは [2] が外側、typedef dims が内側で合計 [2][3][4]。
  if (!g_toplevel_decl_is_typedef && g_toplevel_decl_td_array_dim_count > 0) {
    for (int di = 0; di < g_toplevel_decl_td_array_dim_count && dim_count < 8; di++) {
      int dim = g_toplevel_decl_td_array_dims[di];
      if (dim > 0) {
        out.arr_total *= dim;
        if (dim_count == 0) out.first_dim = dim;
        out.dims[dim_count] = dim;
        dim_count++;
      }
    }
    out.is_array = 1;
  }
  if (dim_count > 8) dim_count = 8;
  out.dim_count = dim_count;
  return out;
}

static void parse_toplevel_declarator_list(void) {
  parse_toplevel_declarator_list_with_apply(0, apply_toplevel_object_from_head);
}

static void parse_toplevel_declarator_list_with_apply(int base_is_ptr,
                                                      void (*apply)(toplevel_declarator_head_t)) {
  int declarator_count = 0;
  for (;;) {
    declarator_count++;
    guard_toplevel_declarator_count(declarator_count);
    toplevel_declarator_head_t head = parse_toplevel_declarator_head(base_is_ptr, 1);
    apply(head);
    if (!has_next_toplevel_declarator()) break;
  }
}

static void guard_toplevel_declarator_count(int declarator_count) {
  if (declarator_count <= PS_MAX_DECLARATOR_COUNT) return;
  psx_diag_ctx(curtok(), "decl",
               diag_message_for(DIAG_ERR_PARSER_DECLARATOR_LIST_TOO_LONG),
               PS_MAX_DECLARATOR_COUNT);
}

// グローバル変数の `{...}` 初期化子を再帰的に flatten して gv->init_values に
// 行優先で詰める。ネストした brace は単に下りる: `{{1,2},{3,4}}` も `{1,2,3,4}` と
// 同じ列になる (多次元配列のメモリレイアウトは行優先)。
// 各要素は ND_NUM のみ受け付け、定数式評価は未対応 (ND_NUM 以外は 0 をプレースする)。
/* グローバル double/float 初期化用の定数式畳み込み。
 * ND_NUM (fval) / ND_ADD / ND_SUB / ND_MUL / ND_DIV / 単項マイナスを再帰評価する。
 * 整数リテラル (ND_NUM with fp_kind=NONE) も double に昇格して評価。
 * 評価不可なら *ok=0。 */
static double psx_eval_const_fp(node_t *n, int *ok) {
  if (!n) { *ok = 0; return 0.0; }
  switch (n->kind) {
    case ND_NUM: {
      node_num_t *num = (node_num_t *)n;
      if (num->base.fp_kind != TK_FLOAT_KIND_NONE) return num->fval;
      return (double)num->val;
    }
    case ND_ADD: {
      double l = psx_eval_const_fp(n->lhs, ok);
      if (!*ok) return 0.0;
      double r = psx_eval_const_fp(n->rhs, ok);
      return *ok ? l + r : 0.0;
    }
    case ND_SUB: {
      double l = psx_eval_const_fp(n->lhs, ok);
      if (!*ok) return 0.0;
      double r = psx_eval_const_fp(n->rhs, ok);
      return *ok ? l - r : 0.0;
    }
    case ND_MUL: {
      double l = psx_eval_const_fp(n->lhs, ok);
      if (!*ok) return 0.0;
      double r = psx_eval_const_fp(n->rhs, ok);
      return *ok ? l * r : 0.0;
    }
    case ND_DIV: {
      double l = psx_eval_const_fp(n->lhs, ok);
      if (!*ok) return 0.0;
      double r = psx_eval_const_fp(n->rhs, ok);
      if (!*ok || r == 0.0) { *ok = 0; return 0.0; }
      return l / r;
    }
    case ND_FNEG: {
      /* 浮動小数の単項マイナス (`-1.0f` は ND_FNEG(1.0f))。これを扱わないと
       * 負の fp グローバル初期化子が定数畳み込みに失敗し has_init が立たず BSS(0) に
       * 化けていた (`float g=-1.0f;` が 0 になる)。 */
      double v = psx_eval_const_fp(n->lhs, ok);
      return *ok ? -v : 0.0;
    }
    default:
      *ok = 0;
      return 0.0;
  }
}

/* struct/union 型のフラット初期化スロット数 (スカラ要素の総数) を再帰的に数える。
 * 入れ子 struct メンバは内側スカラ数だけスロットを占める (`struct In{int p,q;}` は 2)。
 * グローバル designator の slot 計算で先行メンバの正しいスロット数を得るのに使う。 */
static int global_flat_slot_count(token_kind_t tk, char *tn, int tl);

static int global_member_flat_slots(const tag_member_info_t *mi) {
  int per = 1;
  if (mi->tag_kind == TK_STRUCT && !mi->is_tag_pointer) {
    per = global_flat_slot_count(mi->tag_kind, mi->tag_name, mi->tag_len);
  }
  return (mi->array_len > 0) ? mi->array_len * per : per;
}

static int global_flat_slot_count(token_kind_t tk, char *tn, int tl) {
  int n = psx_ctx_get_tag_member_count(tk, tn, tl);
  int slots = 0;
  for (int i = 0; i < n; i++) {
    tag_member_info_t mi = {0};
    if (!psx_ctx_get_tag_member_info(tk, tn, tl, i, &mi)) break;
    slots += global_member_flat_slots(&mi);
  }
  return slots;
}

/* グローバル struct/union の `.member` designator 解決は resolve_member_designator_tag
 * (tag 直接指定版) に統合した。本体 psx_gbrace_flat はネスト時の型コンテキスト ctx に対して
 * 解決する必要があるため gv 固定版は使わない。 */

static int resolve_global_addr_init(node_t *e, char **sym, int *sym_len, long long *off);

/* ネスト brace 内の designator (`.member` / `[N]`) を解決するための「現在の brace level が
 * 初期化している集約型」コンテキスト。これがないと `.s={.a=7}` や `.items={[2]={.a=7}}` の
 * 内側 `.a` を最外 gv の型に対して探してしまい E3064 になっていた。 */
typedef struct {
  token_kind_t tag_kind;  /* struct/union タグ (要素が struct/union なら) / TK_EOF */
  char *tag_name;
  int tag_len;
  int is_array;           /* この level が配列か (要素型は tag_kind) */
  int is_tag_pointer;     /* この level の要素がタグへのポインタ (`struct P *arr[3]`)。
                           * 設定時、is_array=1 で要素は「タグポインタ scalar 8B」になり
                           * gbrace_child_at が各要素を 1 slot (= 8B) として返す。
                           * 通常のタグ値配列 (`struct P arr[3]`) は 0 で従来どおり struct
                           * 単位 (= 内側メンバ数 slot) で展開する。 */
  int elem_size;          /* 非タグ配列メンバ (`char name[8]`) の要素サイズ。char 配列の文字列展開判定に使う */
  int array_len;          /* 同上の要素数 (0=非配列) */
  /* 多次元 char 配列メンバ (`char c[2][2][3]`) の「残り」次元チェーン。is_array=1 のとき、
   * 最外側次元はこの level が表現し、sub_dims[0..sub_ndim) が内側に残る次元を最外側から並べる。
   * gbrace_child_at で 1 段下がるたびに sub_dims を 1 つ消費し、内側 ctx を生成する。
   * sub_ndim==0 は単純 1 次元 (従来挙動)。sub_ndim==1 は要素が「char[sub_dims[0]] 行」(文字列展開)。
   * sub_ndim>=2 はさらに内側がネスト配列。 */
  int sub_dims[8];
  int sub_ndim;
  /* ネスト union の active メンバ fp_kind (TK_FLOAT_KIND_NONE = fp ではない or 不明)。
   * `.member = expr` designator で fp メンバを解決した時に立て、後段の scalar 書き込みで
   * sentinel (init_value_symbol_lens) を立ててネスト union fp 出力に使う。 */
  tk_float_kind_t pending_fp_kind;
  int pending_fp_size;  /* float=4, double=8 (sentinel decode 用) */
} gbrace_ctx_t;

/* tag 直接指定版の `.member` designator 解決 (resolve_global_member_designator の gv 非依存版)。 */
static int resolve_member_designator_tag(token_kind_t tk, char *tn, int tl,
                                         char *mname, int mlen, int *out_ordinal) {
  int n = psx_ctx_get_tag_member_count(tk, tn, tl);
  int slot = 0;
  for (int i = 0; i < n; i++) {
    tag_member_info_t mi = {0};
    if (!psx_ctx_get_tag_member_info(tk, tn, tl, i, &mi)) break;
    if (mi.len == mlen && mi.name && strncmp(mi.name, mname, (size_t)mlen) == 0) {
      if (out_ordinal) *out_ordinal = i;
      return (tk == TK_UNION) ? 0 : slot;
    }
    slot += global_member_flat_slots(&mi);
  }
  return -1;
}

/* tag_member_info_t (designator の葉メンバ型) から子 brace のコンテキストを作る。 */
static gbrace_ctx_t gbrace_ctx_from_member(const tag_member_info_t *mi) {
  /* 非タグ配列メンバの要素サイズ。char 配列 (`char name[8]`) のメンバは deref_size=0 で
   * type_size に要素サイズ (char=1) が入るため、deref_size が無ければ type_size を使う。 */
  int elem = mi->deref_size > 0 ? mi->deref_size : mi->type_size;
  gbrace_ctx_t c = {mi->tag_kind, mi->tag_name, mi->tag_len, (mi->array_len > 0),
                    mi->is_tag_pointer ? 1 : 0,
                    elem, mi->array_len, {0}, 0, TK_FLOAT_KIND_NONE, 0};
  /* 多次元配列メンバ: 各次元サイズが arr_dims に入る。最外側 1 段はこの ctx が
   * is_array=1 として表現するので、残り (sub_dims) には arr_dims[1..arr_ndim) を
   * 最外側から並べてコピー。child_at が 1 段ずつ消費する。
   * - char (`char c[2][2][3]`): 内側 1D を文字列展開する (sub_ndim==1 で is_array=0)。
   * - 非 char (`int x[3][3]`): 内側次元を ndim-1 として再帰し、`[N]=` 経路では
   *   sub_dims から内側 1 要素の slot 数を計算する。
   * - struct タグ配列 (`struct C rows[3][2]`): 最外 `[N]=` の elem_slots を
   *   `struct slot * 内側次元の積` で計算するために sub_dims を保持する。
   * 2 次元以上のみ (1D は sub_dims 不要、従来の array_len で運用)。 */
  if (mi->arr_ndim >= 2) {
    int n = mi->arr_ndim - 1;
    if (n > 8) n = 8;
    for (int i = 0; i < n; i++) c.sub_dims[i] = mi->arr_dims[i + 1];
    c.sub_ndim = n;
  }
  return c;
}

/* aggregate `ctx` の中で level 先頭から slot オフセット `off` にある部分オブジェクトの型。
 * positional 初期化 (`{{.a=1},{.b=2}}`) で次の brace 要素のコンテキストを得るのに使う。 */
static gbrace_ctx_t gbrace_child_at(gbrace_ctx_t ctx, int off) {
  gbrace_ctx_t c = {TK_EOF, NULL, 0, 0, 0, 0, 0, {0}, 0, TK_FLOAT_KIND_NONE, 0};
  if (ctx.is_array) {
    /* 配列要素はすべて同型 (要素型 = ctx.tag_kind)。
     * タグポインタ配列 (`struct P *arr[3]`): 要素は「struct P へのポインタ scalar (8B)」。
     * tag_kind は伝播せず TK_EOF にして scalar 8B として返す。これがないと psx_gbrace_flat
     * の struct 経路で「struct 値 (= 内側メンバ数 slot)」として展開され、parr[1]/parr[2] の
     * シンボル+offset が誤 slot に書かれていた。1 要素 = 1 slot で済むよう scalar 化する。 */
    if (ctx.is_tag_pointer) {
      c.tag_kind = TK_EOF;
      c.is_array = 0;
      c.elem_size = 8;
      return c;
    }
    c.tag_kind = ctx.tag_kind;
    c.tag_name = ctx.tag_name;
    c.tag_len = ctx.tag_len;
    c.is_array = 0;
    /* 多次元配列メンバ: 残り次元 sub_dims を 1 段消費して内側 ctx を生成する。
     * - char (`char c[2][2][3]`): 最内 1 段 (sub_ndim==1) は文字列展開用に is_array=0 で
     *   返す。中間段 (sub_ndim>=2) は is_array=1 で sub_dims を 1 つ前に詰めて再帰。
     * - 非 char (`int x[3][3]`): 中間段は is_array=1 で内側ndim配列として再帰。最内
     *   1 段 (sub_ndim==1) は scalar 要素 (`int`) を 1 つ書く ctx (is_array=0, elem_size=)
     *   としてそのまま fall-through (sub_dims 機構を抜ける)。
     * - struct タグ多次元配列 (`struct C rows[3][2]`): 中間段 (sub_ndim>=2) と最内 1 段
     *   (sub_ndim==1) のいずれも is_array=1 で「内側次元数の struct タグ配列」として
     *   返す。これがないと内側 brace `{{.val=99}}` で designator が「単一 struct」コンテキストに
     *   解釈され `.val=` が E3064 で弾かれる。 */
    if (ctx.sub_ndim >= 1) {
      if (ctx.tag_kind == TK_EOF && ctx.elem_size == 1 && ctx.sub_ndim == 1) {
        /* char 最内 1D: 行 (sub_dims[0] バイト) として文字列展開分岐に乗せる。 */
        c.elem_size = 1;
        c.array_len = ctx.sub_dims[0];
      } else if (ctx.sub_ndim >= 2 || ctx.elem_size > 1 || ctx.tag_kind != TK_EOF) {
        /* 中間段 / 非 char 多次元 / struct タグ多次元: 内側 (sub_ndim-1) 次元の配列。 */
        int inner_total = 1;
        for (int i = 0; i < ctx.sub_ndim; i++) inner_total *= ctx.sub_dims[i];
        c.is_array = 1;
        c.elem_size = ctx.elem_size;
        c.array_len = inner_total;
        int n = ctx.sub_ndim - 1;
        for (int i = 0; i < n; i++) c.sub_dims[i] = ctx.sub_dims[i + 1];
        c.sub_ndim = n;
      }
    }
    return c;
  }
  if (ctx.tag_kind == TK_STRUCT || ctx.tag_kind == TK_UNION) {
    int n = psx_ctx_get_tag_member_count(ctx.tag_kind, ctx.tag_name, ctx.tag_len);
    int slot = 0;
    for (int i = 0; i < n; i++) {
      tag_member_info_t mi = {0};
      if (!psx_ctx_get_tag_member_info(ctx.tag_kind, ctx.tag_name, ctx.tag_len, i, &mi)) break;
      int ms = global_member_flat_slots(&mi);
      if (off < slot + ms) return gbrace_ctx_from_member(&mi);
      slot += ms;
    }
  }
  return c;
}

static void psx_gbrace_flat(global_var_t *gv, int *cap, int start_idx, gbrace_ctx_t ctx);

/* static local 配列の lowering (decl.c) からも使えるよう非 static 化。 */
void psx_parse_global_brace_init_flat(global_var_t *gv, int *cap, int start_idx) {
  gbrace_ctx_t ctx = {gv->tag_kind, gv->tag_name, gv->tag_len, gv->is_array,
                      gv->is_tag_pointer ? 1 : 0, 0, 0, {0}, 0, TK_FLOAT_KIND_NONE, 0};
  /* グローバル多次元配列 (`int g[3][2]` 等) のトップレベル ctx に sub_dims を埋める。
   * 内側 designator (`{[2] = {[1] = 99}}`) の elem_slots 計算で「外側 `[N]=` は内側次元の総
   * スカラ数 * N 進める」必要がある (続き13 で struct メンバ多次元配列に対応済みだが、その
   * 経路は gbrace_ctx_from_member 経由。トップレベル global 多次元配列は本関数が ctx を
   * 初期化するためここでも sub_dims を埋める必要がある)。
   * gv の outer_stride / mid_stride / extra_strides の隣接ペアを割って各 dim を算出。
   * 1D 配列 (outer_stride==deref_size) は sub_ndim=0 (従来挙動)。 */
  if (gv->is_array && gv->tag_kind == TK_EOF && gv->deref_size > 0
      && gv->outer_stride > gv->deref_size) {
    int strides[10];
    int n_strides = 0;
    strides[n_strides++] = gv->outer_stride;
    if (gv->mid_stride > 0) strides[n_strides++] = gv->mid_stride;
    for (int i = 0; i < gv->extra_strides_count && i < 5; i++) {
      if (gv->extra_strides[i] > 0) strides[n_strides++] = gv->extra_strides[i];
    }
    strides[n_strides++] = gv->deref_size;
    int n_sub = n_strides - 1;
    if (n_sub > 8) n_sub = 8;
    for (int i = 0; i < n_sub; i++) {
      ctx.sub_dims[i] = strides[i] / strides[i + 1];
    }
    ctx.sub_ndim = n_sub;
    ctx.elem_size = gv->deref_size;
  }
  psx_gbrace_flat(gv, cap, start_idx, ctx);
}

static void psx_gbrace_flat(global_var_t *gv, int *cap, int start_idx, gbrace_ctx_t ctx) {
  tk_expect('{');
  if (tk_consume('}')) return;
  /* 書き込み位置はフラットな絶対 index。ネスト brace の再帰でも連続して
   * 追記できるよう、現在の充填位置 (init_count) から開始する。
   * designator [N]=/.member= で外側が cur_idx をジャンプ済みのときは、その slot
   * (start_idx) から書き始める (`{.z=14, .i={12,13}}` で .i の brace を slot 0 へ)。
   * start_idx < 0 は「init_count から」を意味する (トップレベル呼出)。 */
  int cur_idx = (start_idx >= 0) ? start_idx : gv->init_count;
  int level_start = cur_idx;  /* この brace level の先頭 slot ([N]= の絶対位置計算に使う) */
  int align_next_array_positional = 0;
  for (;;) {
    /* 配列レベルの positional 要素は要素境界へ揃える。直前の要素が部分初期化
     * (`{.a=1}` で b を埋めない) でも init_count が要素途中で止まり、次要素が
     * ずれるのを防ぐ。designator (`[N]=`) はこの後 cur_idx を再設定するので除外。 */
    if (align_next_array_positional && ctx.is_array &&
        curtok()->kind != TK_LBRACKET && curtok()->kind != TK_DOT) {
      /* タグポインタ配列 (`struct P *arr[3]`) は要素 = 1 slot (scalar pointer) なので
       * es=1 で従来どおり境界揃え不要。タグ値配列 (`struct P arr[3]`) は struct 内側メンバ数。 */
      int es = (ctx.tag_kind == TK_STRUCT && !ctx.is_tag_pointer)
                   ? global_flat_slot_count(ctx.tag_kind, ctx.tag_name, ctx.tag_len) : 1;
      if (es > 1) {
        int r = (cur_idx - level_start) % es;
        if (r != 0) cur_idx += es - r;  /* 次の要素境界へ切り上げ (隙間は後段で 0 埋め) */
      }
      /* plain 多次元配列の positional 要素境界。部分初期化した行の直後は次行の先頭 slot へ
       * 進める (`int a[2][3][5]` で `{0,0,3,5}` の 4 スカラの次は slot 5 から)。
       * sub_dims の積が 1 要素の flat slot 数 (`[3][5]`→15、行 `[5]`→5)。 */
      if (es <= 1 && ctx.sub_ndim >= 1) {
        int elem_slots = 1;
        for (int i = 0; i < ctx.sub_ndim; i++) elem_slots *= ctx.sub_dims[i];
        if (elem_slots > 1) {
          int off = cur_idx - level_start;
          int r = off % elem_slots;
          if (r != 0) cur_idx += elem_slots - r;
        }
      }
    }
    align_next_array_positional = 0;
    /* この反復で初期化する部分オブジェクトの型 (ネスト brace の子コンテキスト)。
     * 既定は positional 位置の型。designator のときは下で上書きする。 */
    gbrace_ctx_t child = gbrace_child_at(ctx, cur_idx - level_start);
    /* `[N] = expr` 形式の designated initializer (C11 6.7.9p6) を許可する。
     * cur_idx を N に飛ばし、その位置から書き込む。間の要素は 0 のまま。 */
    if (curtok()->kind == TK_LBRACKET) {
      set_curtok(curtok()->next);
      node_t *idx_node = psx_expr_assign();
      int const_ok = 1;
      long long idx_val = psx_decl_eval_const_int(idx_node, &const_ok);
      if (!const_ok || idx_val < 0) {
        psx_diag_ctx(curtok(), "decl",
                     "配列指定初期化子の添字は非負の定数式である必要があります");
      }
      consume_gnu_range_designator_tail_if_any();
      tk_expect(']');
      tk_expect('=');
      /* struct 要素配列の `[N]=` は要素 1 つが内側スカラ数だけ slot を占めるので
       * N にその数を掛ける (`struct P g[3]={[2]={5,6}}` の [2] は flat slot 4)。
       * 多次元配列 (`int x[3][3]`) も同様: 1 要素 = 内側次元の総スカラ数 (sub_dims の積)。
       * scalar 要素配列は 1 slot なので従来どおり N。 */
      int elem_slots = 1;
      if (ctx.tag_kind == TK_STRUCT && !ctx.is_tag_pointer) {
        /* タグ値配列 (`struct P arr[3]={[1]={...}}`) は要素 = 内側メンバ数 slot。
         * タグポインタ配列 (`struct P *arr[3]={[1]=&p}`) は要素 = 1 slot (scalar pointer)
         * なので elem_slots=1 のまま (`is_tag_pointer` で除外)。 */
        elem_slots = global_flat_slot_count(ctx.tag_kind, ctx.tag_name, ctx.tag_len);
        if (elem_slots < 1) elem_slots = 1;
        /* 多次元 struct タグ配列メンバ (`struct C rows[3][2]`): 1 要素 (rows[i]) は
         * 内側次元 (sub_dims[*]) の積 ぶんだけ struct slot が並ぶ。`[N]=` ジャンプは
         * `struct slot * 内側次元の積` で進める必要がある (これがないと外側 designator が
         * 内側次元を無視し誤ジャンプ。99 が rows[2][0] でなく rows[1][0] に書かれていた)。 */
        for (int i = 0; i < ctx.sub_ndim; i++) elem_slots *= ctx.sub_dims[i];
      } else if (ctx.sub_ndim >= 1) {
        /* 多次元配列メンバ (非タグ): 1 要素 = 内側 sub_ndim 次元の総スカラ数。
         * `int x[3][3]` で sub_dims={3} なら elem_slots=3、`[2]=` は slot 6 へ。
         * これがないと elem_slots=1 のまま `[2]=` が slot 2 へジャンプし他要素を
         * 上書きしていた (designator nested バグ)。 */
        for (int i = 0; i < ctx.sub_ndim; i++) elem_slots *= ctx.sub_dims[i];
        if (elem_slots < 1) elem_slots = 1;
      }
      /* level 先頭からの絶対 slot。ネスト配列 (`.items={[2]={...}}`) では level_start を
       * 足さないと外側メンバの offset を無視して先頭から書いてしまう。 */
      cur_idx = level_start + (int)idx_val * elem_slots;
      /* `[N]=` の要素型 = 配列要素 (ctx の要素型)。多次元配列なら 1 段降りた内側ndim配列。 */
      child = gbrace_child_at(ctx, cur_idx - level_start);
    }
    /* `.member = expr` 形式の struct/union メンバ designator (C11 6.7.9p6)。
     * メンバの flat slot へ cur_idx を飛ばす。union は活性メンバ序数を記録。 */
    else if (curtok()->kind == TK_DOT) {
      set_curtok(curtok()->next);
      token_ident_t *m = tk_consume_ident();
      if (!m || ctx.tag_kind == TK_EOF) {
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_MEMBER_DESIGNATOR_INVALID));
      }
      int ordinal = 0;
      /* designator は最外 gv ではなく「現在の brace level の型」ctx に対して解決する。
       * これがないと `.s={.a=7}` / `.items={[2]={.a=7}}` の内側 `.a` を gv の型に
       * 探して E3064 になっていた。level_start を足して絶対 slot にする。 */
      int slot = resolve_member_designator_tag(ctx.tag_kind, ctx.tag_name, ctx.tag_len,
                                               m->str, m->len, &ordinal);
      if (slot < 0) {
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_MEMBER_DESIGNATOR_NOT_FOUND));
      }
      cur_idx = level_start + slot;
      if (ctx.tag_kind == TK_UNION) gv->union_init_ordinal = ordinal;
      /* `.member[idx]` / `.member.sub` の designator チェーンを辿る (C11 6.7.9p6)。
       * 現メンバの型情報 cmi を持ち、[idx] は要素 slot 数だけ、.sub は内側メンバの
       * slot offset だけ cur_idx を進める。これがないと `struct W w={.arr[1]=7}`
       * (グローバル) が E2006 で拒否されていた。 */
      tag_member_info_t cmi = {0};
      psx_ctx_get_tag_member_info(ctx.tag_kind, ctx.tag_name, ctx.tag_len, ordinal, &cmi);
      /* ネスト union の fp メンバ designator (`.f = 2.5f`): 次の scalar 書き込みで sentinel
       * を立てて emit にネスト union active メンバが fp であることを伝える。0.0f と .n=0 を
       * 判別可能にするため、ヒューリスティック (fv!=0) ではなく明示的に通知する。 */
      if (ctx.tag_kind == TK_UNION && cmi.fp_kind != TK_FLOAT_KIND_NONE) {
        ctx.pending_fp_kind = cmi.fp_kind;
        ctx.pending_fp_size = cmi.type_size;
      }
      for (;;) {
        if (curtok()->kind == TK_LBRACKET) {
          set_curtok(curtok()->next);
          int iok = 1;
          long long iv = psx_decl_eval_const_int(psx_expr_assign(), &iok);
          consume_gnu_range_designator_tail_if_any();
          tk_expect(']');
          if (!iok || iv < 0) {
            psx_diag_ctx(curtok(), "decl", "%s",
                         diag_message_for(DIAG_ERR_PARSER_ARRAY_DESIGNATOR_INDEX_INVALID));
          }
          int per = (cmi.tag_kind == TK_STRUCT && !cmi.is_tag_pointer)
                        ? global_flat_slot_count(cmi.tag_kind, cmi.tag_name, cmi.tag_len) : 1;
          cur_idx += (int)iv * per;
          cmi.array_len = 0; /* 添字を 1 段消費 */
        } else if (curtok()->kind == TK_DOT) {
          set_curtok(curtok()->next);
          token_ident_t *sm = tk_consume_ident();
          if (!sm || cmi.tag_kind == TK_EOF)
            psx_diag_ctx(curtok(), "decl", "%s",
                         diag_message_for(DIAG_ERR_PARSER_MEMBER_DESIGNATOR_INVALID));
          int sub_n = psx_ctx_get_tag_member_count(cmi.tag_kind, cmi.tag_name, cmi.tag_len);
          int sub_slot = 0, found = 0;
          for (int si = 0; si < sub_n; si++) {
            tag_member_info_t smi = {0};
            if (!psx_ctx_get_tag_member_info(cmi.tag_kind, cmi.tag_name, cmi.tag_len, si, &smi)) break;
            if (smi.len == sm->len && smi.name &&
                strncmp(smi.name, sm->str, (size_t)sm->len) == 0) {
              cur_idx += (cmi.tag_kind == TK_UNION) ? 0 : sub_slot;
              cmi = smi; found = 1; break;
            }
            sub_slot += global_member_flat_slots(&smi);
          }
          if (!found) {
            psx_diag_ctx(curtok(), "decl", "%s",
                         diag_message_for(DIAG_ERR_PARSER_MEMBER_DESIGNATOR_NOT_FOUND));
          }
        } else break;
      }
      tk_expect('=');
      /* designator の葉メンバ型を子 brace コンテキストにする (`.s={.a=7}` の `{...}` は struct I)。 */
      child = gbrace_ctx_from_member(&cmi);
    }
    /* 書き込み位置 cur_idx の slot を確保する (designator の後方ジャンプにも対応)。 */
    while (*cap <= cur_idx) {
      int new_cap = *cap * 2;
      gv->init_values = realloc(gv->init_values, (size_t)new_cap * sizeof(long long));
      gv->init_value_symbols = realloc(gv->init_value_symbols, (size_t)new_cap * sizeof(char *));
      gv->init_value_symbol_lens = realloc(gv->init_value_symbol_lens, (size_t)new_cap * sizeof(int));
      if (gv->init_fvalues) {
        gv->init_fvalues = realloc(gv->init_fvalues, (size_t)new_cap * sizeof(double));
        for (int i = *cap; i < new_cap; i++) gv->init_fvalues[i] = 0.0;
      }
      *cap = new_cap;
    }
    /* cur_idx より前の未使用要素を 0 で埋める (前方ジャンプ時のギャップ)。
     * 後方ジャンプ (cur_idx < init_count) では既存 slot なので何もしない。 */
    while (gv->init_count < cur_idx) {
      gv->init_values[gv->init_count] = 0;
      gv->init_value_symbols[gv->init_count] = NULL;
      gv->init_value_symbol_lens[gv->init_count] = 0;
      if (gv->init_fvalues) gv->init_fvalues[gv->init_count] = 0.0;
      gv->init_count++;
    }
    if (curtok()->kind == TK_LBRACE) {
      /* 入れ子 brace は外側の現在位置 cur_idx から書き始める (designator で
       * 後方ジャンプ済みのときも正しい slot へ)。child は内側 designator を正しい型で
       * 解決するためのコンテキスト (`.s={.a=7}` の `{...}` は struct I)。 */
      psx_gbrace_flat(gv, cap, cur_idx, child);
      cur_idx = gv->init_count;
      align_next_array_positional = 1;
    } else if (curtok()->kind == TK_STRING && gv->deref_size == 1 && gv->outer_stride > 0) {
      /* 多次元 char 配列の行を文字列で初期化: `char g[2][6]={"hello","world"}`。
       * 文字列を行 (outer_stride バイト) のバイト列へ展開する (char* 配列ではないので
       * .LC ポインタにしない)。残りは 0 埋め。 */
      node_t *e = psx_expr_assign();
      int row_w = gv->outer_stride;
      while (*cap < cur_idx + row_w) {
        int new_cap = *cap * 2;
        if (new_cap < cur_idx + row_w) new_cap = cur_idx + row_w;
        gv->init_values = realloc(gv->init_values, (size_t)new_cap * sizeof(long long));
        gv->init_value_symbols = realloc(gv->init_value_symbols, (size_t)new_cap * sizeof(char *));
        gv->init_value_symbol_lens = realloc(gv->init_value_symbol_lens, (size_t)new_cap * sizeof(int));
        if (gv->init_fvalues) {
          gv->init_fvalues = realloc(gv->init_fvalues, (size_t)new_cap * sizeof(double));
          for (int i = *cap; i < new_cap; i++) gv->init_fvalues[i] = 0.0;
        }
        *cap = new_cap;
      }
      string_lit_t *lit = NULL;
      if (e && e->kind == ND_STRING) {
        for (string_lit_t *l = string_literals; l; l = l->next) {
          if (strcmp(l->label, ((node_string_t *)e)->string_label) == 0) { lit = l; break; }
        }
      }
      int j = 0, sp = 0;
      if (lit) {
        while (sp < lit->len && j < row_w) {
          uint32_t cp = 0;
          if (lit->str[sp] == '\\') {
            if (!tk_parse_escape_value(lit->str, lit->len, &sp, &cp)) { cp = (unsigned char)lit->str[sp]; sp++; }
          } else { cp = (unsigned char)lit->str[sp]; sp++; }
          gv->init_values[cur_idx + j] = (unsigned char)cp;
          gv->init_value_symbols[cur_idx + j] = NULL;
          gv->init_value_symbol_lens[cur_idx + j] = 0;
          if (gv->init_fvalues) gv->init_fvalues[cur_idx + j] = 0.0;
          j++;
        }
      }
      while (j < row_w) {  /* 行の残りを 0 埋め */
        gv->init_values[cur_idx + j] = 0;
        gv->init_value_symbols[cur_idx + j] = NULL;
        gv->init_value_symbol_lens[cur_idx + j] = 0;
        if (gv->init_fvalues) gv->init_fvalues[cur_idx + j] = 0.0;
        j++;
      }
      cur_idx += row_w;
      if (cur_idx > gv->init_count) gv->init_count = cur_idx;
      if (!tk_consume(',')) break;
      if (curtok()->kind == TK_RBRACE) break;
      continue;
    } else if (curtok()->kind == TK_STRING && child.tag_kind == TK_EOF &&
               child.array_len > 0 && child.elem_size == 1) {
      /* struct の char 配列メンバを文字列で初期化: `struct S{char name[8];} g={"main"}`。
       * 文字列を array_len バイトへ展開する (char* メンバではないので .LC ポインタにしない。
       * 旧挙動は scalar 経路で .quad <ラベル> を 1 slot に書き、name 全体がポインタ値に
       * 化けていた)。残りは 0 埋め。
       *
       * 多次元 char メンバ (`char rows[2][4]`) への brace elision `{"ab","cd"}`: 1 文字列を
       * 「行」(sub_dims 最後の次元 = 行幅) に展開する。残りメンバ要素の埋めは外側ループの
       * 次反復が gbrace_child_at で同メンバを返すので自動で続く (cur_idx は行幅ぶん進めるだけ)。
       * これがないと array_len 全体 (=メンバ全要素数) を 1 文字列で埋めてしまい後続文字列が
       * 次メンバとして扱われていた (struct に他メンバが無いと 0 埋めだけになる)。 */
      node_t *e = psx_expr_assign();
      int row_w = child.sub_ndim > 0 ? child.sub_dims[child.sub_ndim - 1] : child.array_len;
      if (row_w <= 0) row_w = child.array_len;
      while (*cap < cur_idx + row_w) {
        int new_cap = *cap * 2;
        if (new_cap < cur_idx + row_w) new_cap = cur_idx + row_w;
        gv->init_values = realloc(gv->init_values, (size_t)new_cap * sizeof(long long));
        gv->init_value_symbols = realloc(gv->init_value_symbols, (size_t)new_cap * sizeof(char *));
        gv->init_value_symbol_lens = realloc(gv->init_value_symbol_lens, (size_t)new_cap * sizeof(int));
        if (gv->init_fvalues) {
          gv->init_fvalues = realloc(gv->init_fvalues, (size_t)new_cap * sizeof(double));
          for (int i = *cap; i < new_cap; i++) gv->init_fvalues[i] = 0.0;
        }
        *cap = new_cap;
      }
      string_lit_t *lit = NULL;
      if (e && e->kind == ND_STRING) {
        for (string_lit_t *l = string_literals; l; l = l->next) {
          if (strcmp(l->label, ((node_string_t *)e)->string_label) == 0) { lit = l; break; }
        }
      }
      int j = 0, sp = 0;
      if (lit) {
        while (sp < lit->len && j < row_w) {
          uint32_t cp = 0;
          if (lit->str[sp] == '\\') {
            if (!tk_parse_escape_value(lit->str, lit->len, &sp, &cp)) { cp = (unsigned char)lit->str[sp]; sp++; }
          } else { cp = (unsigned char)lit->str[sp]; sp++; }
          gv->init_values[cur_idx + j] = (unsigned char)cp;
          gv->init_value_symbols[cur_idx + j] = NULL;
          gv->init_value_symbol_lens[cur_idx + j] = 0;
          if (gv->init_fvalues) gv->init_fvalues[cur_idx + j] = 0.0;
          j++;
        }
      }
      while (j < row_w) {
        gv->init_values[cur_idx + j] = 0;
        gv->init_value_symbols[cur_idx + j] = NULL;
        gv->init_value_symbol_lens[cur_idx + j] = 0;
        if (gv->init_fvalues) gv->init_fvalues[cur_idx + j] = 0.0;
        j++;
      }
      cur_idx += row_w;
      if (cur_idx > gv->init_count) gv->init_count = cur_idx;
      if (!tk_consume(',')) break;
      if (curtok()->kind == TK_RBRACE) break;
      continue;
    } else {
      node_t *e = psx_expr_assign();
      long long v = 0;
      double fv = 0.0;
      char *sym = NULL;
      int sym_len = 0;
      int ok = 1;
      if (e && e->kind == ND_NUM) {
        node_num_t *n = (node_num_t *)e;
        v = n->val;
        /* float/double 要素のグローバル配列では fval を保存。整数リテラルが
         * 混ざっていても (`double a[] = {1, 2.5}`) 宣言型 fp_kind を優先する。 */
        fv = (n->base.fp_kind != TK_FLOAT_KIND_NONE) ? n->fval : (double)n->val;
      }
      else if (e && gv->fp_kind != TK_FLOAT_KIND_NONE && gv->init_fvalues) {
        /* fp 配列の非 ND_NUM 要素 (負値 `-2.5` は ND_FNEG、定数式 `1.0/2` 等)。
         * psx_eval_const_fp で畳み込む。これがないと負の配列要素が 0 に化けていた。
         * fp_kind ゲート必須: init_fvalues はポインタ/整数配列でも確保されうるので、
         * fp 配列に限定しないと `&data[n]`/文字列要素を乗っ取り symbol を失う。 */
        int fok = 1;
        double folded = psx_eval_const_fp(e, &fok);
        if (fok) fv = folded;
      }
      else if (e && e->kind == ND_FUNCREF) {
        /* `struct Op gop = {sq};` 等の関数ポインタメンバ初期化。 */
        node_funcref_t *fr = (node_funcref_t *)e;
        sym = fr->funcname;
        sym_len = fr->funcname_len;
      } else if (e && (e->kind == ND_ADDR || e->kind == ND_ADD || e->kind == ND_SUB)) {
        /* `&g` / `&data[n]` / `data + n` 形式: グローバル変数 (配列要素) のアドレスを
         * 要素に置く。resolve_global_addr_init が (シンボル, バイトオフセット) へ
         * 解決する。オフセットは init_values に格納し、codegen が `_sym+off` を出力する。
         * これがないと `int *arr[]={&data[0],&data[2]}` が const int 評価で 0 になり
         * NULL ポインタ配列になっていた (deref で SIGSEGV)。 */
        long long off = 0;
        if (resolve_global_addr_init(e, &sym, &sym_len, &off)) {
          v = off;
        } else {
          int ok2 = 1;
          v = psx_decl_eval_const_int(e, &ok2);
        }
      } else if (e && e->kind == ND_STRING) {
        /* `const char *arr[] = {"abc", ...};` の文字列リテラル要素。
         * 文字列の .LC<n> ラベルをそのまま symbol として保持し、
         * codegen 側で `_` プレフィックスなしで `.quad <label>` を出力する。
         * sym_len=0 でも sym!=NULL の状態を表現する苦しいフォーマットなので、
         * 別途識別するため init_value_symbol_lens を -1 にしておく。 */
        node_string_t *s = (node_string_t *)e;
        sym = s->string_label;
        sym_len = -1; /* sentinel: emit raw label (no `_` prefix) */
      } else if (e) v = psx_decl_eval_const_int(e, &ok);
      /* 書き込み位置は cur_idx (designator でジャンプ済み)。init_count は
       * 充填済みの最大要素数として追跡する。 */
      gv->init_values[cur_idx] = v;
      gv->init_value_symbols[cur_idx] = sym;
      gv->init_value_symbol_lens[cur_idx] = sym_len;
      if (gv->init_fvalues) gv->init_fvalues[cur_idx] = fv;
      /* ネスト union の fp active メンバ sentinel: DOT 経路で pending_fp_kind がセットされて
       * いれば、init_value_symbols=NULL かつ init_value_symbol_lens に sentinel (-2: float,
       * -3: double/long double) を立てる。emit TK_UNION 分岐がこれを読んで fp として出力。
       * sentinel -1 は既存の「文字列リテラル要素」用なので使わない。
       * scalar 書き込み 1 回で消費して clear (`{.f=2.5f,.n=99}` などの後勝ち designator にも対応)。 */
      if (sym == NULL && ctx.pending_fp_kind != TK_FLOAT_KIND_NONE) {
        gv->init_value_symbols[cur_idx] = NULL;
        gv->init_value_symbol_lens[cur_idx] = (ctx.pending_fp_size >= 8) ? -3 : -2;
        ctx.pending_fp_kind = TK_FLOAT_KIND_NONE;
        ctx.pending_fp_size = 0;
      }
      cur_idx++;
      if (cur_idx > gv->init_count) gv->init_count = cur_idx;
    }
    if (!tk_consume(',')) break;
    if (curtok()->kind == TK_RBRACE) break;  // 末尾カンマ許容
  }
  tk_expect('}');
}

/* グローバルポインタ初期化子のアドレス式を (シンボル, バイトオフセット) へ解決する。
 *   &x / x(配列decay)          → (x, 0)
 *   a + n / &a[n]              → (a, n*sizeof(elem))
 *   &a[n] (= &*(a+n))          → DEREF を剥がして再帰
 * 解決できれば 1 を返し sym・sym_len・off を設定する。 */
int psx_resolve_global_addr_init(node_t *e, char **sym, int *sym_len, long long *off);

int psx_resolve_global_addr_init(node_t *e, char **sym, int *sym_len, long long *off) {
  return resolve_global_addr_init(e, sym, sym_len, off);
}

static int resolve_global_addr_init(node_t *e, char **sym, int *sym_len, long long *off) {
  if (!e) return 0;
  switch (e->kind) {
    case ND_ADDR:
      if (e->lhs && e->lhs->kind == ND_GVAR) {
        node_gvar_t *g = (node_gvar_t *)e->lhs;
        *sym = g->name; *sym_len = g->name_len;
        return 1;
      }
      if (e->lhs && e->lhs->kind == ND_DEREF) {
        return resolve_global_addr_init(e->lhs->lhs, sym, sym_len, off);
      }
      return 0;
    /* `(char*)&g_arr[N]` のような明示キャストは ND_PTR_CAST にラップされる。
     * シンボル+offset 解決には型変換の有無は無関係なので、operand に再帰する。 */
    case ND_PTR_CAST:
      return resolve_global_addr_init(e->lhs, sym, sym_len, off);
    case ND_GVAR: {
      node_gvar_t *g = (node_gvar_t *)e;
      *sym = g->name; *sym_len = g->name_len;
      return 1;
    }
    /* 文字列リテラルを「ベース + 0」のシンボル参照として扱う。
     * `const char *p = "abc" + 2;` のような形を resolve できるようにし、後段の
     * ND_ADD 経路で +2 を加算した init_symbol_offset として登録する。
     * init_symbol_len=-1 を sentinel に立てて codegen が `.LCn` を `_` プレフィックス
     * なしで出すようにする (通常 gvar 名と同じ仕組み)。 */
    case ND_STRING: {
      node_string_t *s = (node_string_t *)e;
      *sym = s->string_label; *sym_len = -1;
      return 1;
    }
    case ND_ADD: {
      int ok = 1;
      if (resolve_global_addr_init(e->lhs, sym, sym_len, off)) {
        long long c = psx_decl_eval_const_int(e->rhs, &ok);
        if (!ok) return 0;
        *off += c; return 1;
      }
      if (resolve_global_addr_init(e->rhs, sym, sym_len, off)) {
        long long c = psx_decl_eval_const_int(e->lhs, &ok);
        if (!ok) return 0;
        *off += c; return 1;
      }
      return 0;
    }
    case ND_SUB: {
      int ok = 1;
      if (resolve_global_addr_init(e->lhs, sym, sym_len, off)) {
        long long c = psx_decl_eval_const_int(e->rhs, &ok);
        if (!ok) return 0;
        *off -= c; return 1;
      }
      return 0;
    }
    default:
      return 0;
  }
}

static void ensure_global_init_capacity(global_var_t *gv, int *cap, int min_cap) {
  while (*cap < min_cap) {
    int old_cap = *cap;
    int new_cap = old_cap * 2;
    if (new_cap < min_cap) new_cap = min_cap;
    gv->init_values = realloc(gv->init_values, (size_t)new_cap * sizeof(long long));
    gv->init_value_symbols = realloc(gv->init_value_symbols, (size_t)new_cap * sizeof(char *));
    gv->init_value_symbol_lens = realloc(gv->init_value_symbol_lens, (size_t)new_cap * sizeof(int));
    if (gv->init_fvalues) {
      gv->init_fvalues = realloc(gv->init_fvalues, (size_t)new_cap * sizeof(double));
    }
    for (int i = old_cap; i < new_cap; i++) {
      gv->init_values[i] = 0;
      gv->init_value_symbols[i] = NULL;
      gv->init_value_symbol_lens[i] = 0;
      if (gv->init_fvalues) gv->init_fvalues[i] = 0.0;
    }
    *cap = new_cap;
  }
}

static void pad_global_init_zeros(global_var_t *gv, int *cap, int total_slots) {
  ensure_global_init_capacity(gv, cap, total_slots);
  while (gv->init_count < total_slots) {
    gv->init_values[gv->init_count] = 0;
    gv->init_value_symbols[gv->init_count] = NULL;
    gv->init_value_symbol_lens[gv->init_count] = 0;
    if (gv->init_fvalues) gv->init_fvalues[gv->init_count] = 0.0;
    gv->init_count++;
  }
}

static void apply_toplevel_object_initializer(global_var_t *gv) {
  if (!tk_consume('=')) return;
  /* C11 6.9.2: 同名グローバル変数の二重定義検出。register_toplevel_global_decl が merge して
   * 既存 gvar を返すため、`int g = 1; int g = 2;` の 2 度目の `=` 時には gv->has_init が
   * すでに 1 になっている。 */
  if (gv->has_init) {
    psx_diag_ctx(curtok(), "decl",
                 "グローバル変数 '%.*s' は重複定義されています (C11 6.9.2)",
                 gv->name_len, gv->name);
  }
  /* ファイルスコープの複合リテラル初期化子 `T g = (T){...};` は `T g = {...};` と
   * 等価 (C11 6.5.2.5)。先頭の `(型)` を読み飛ばして既存の brace 初期化経路に渡す。
   * `)` の直後が `{` であることを先読みして複合リテラルだけを対象にする。
   * ただし変数がポインタの場合 (`int *p = (int[]){...}`)、cast 型と変数型が違うため strip
   * してしまうと「ポインタを brace 初期化子で初期化」と解釈され先頭要素値がポインタスロット
   * に書き込まれて SIGBUS になる。集約 (配列 / struct 値 / union 値) のときだけ strip し、
   * ポインタ・スカラ変数では式経路 (psx_expr_assign) で compound literal 経路に乗せて hidden
   * gvar を作る。スカラ変数 `int g = (int){5}` は式経路の compound literal 短絡
   * (expr.c の `!is_arr && !want_addr && ND_NUM` 分岐) が ND_NUM を直接返すので動作する。 */
  if (curtok()->kind == TK_LPAREN) {
    token_t *t = curtok()->next;
    int depth = 1;
    while (t && depth > 0) {
      if (t->kind == TK_LPAREN) depth++;
      else if (t->kind == TK_RPAREN) { depth--; if (depth == 0) break; }
      t = t->next;
    }
    if (t && t->kind == TK_RPAREN && t->next && t->next->kind == TK_LBRACE) {
      /* strip 判定: 集約 (配列 / struct 値 / union 値) なら常に OK。ポインタ・スカラ var では、
       * brace が単一文字列 (`char *p = (char[6]){"hi"}` の "hi" のような形) ならポインタ初期化
       * として等価なので strip OK。複数値の `int *p = (int[]){10,20,30}` 形は strip すると先頭
       * 要素値がポインタスロットに書き込まれて SIGBUS なので skip し、式経路で compound literal
       * を hidden gvar に materialize させる。 */
      int gv_is_aggregate = gv->is_array || (gv->tag_kind != TK_EOF && !gv->is_tag_pointer);
      int may_strip = gv_is_aggregate;
      if (!may_strip) {
        token_t *brace_open = t->next;            /* '{' */
        token_t *first = brace_open->next;        /* 中身先頭 */
        if (first && first->kind == TK_STRING && first->next &&
            first->next->kind == TK_RBRACE) {
          may_strip = 1;  /* {"str"} 単一文字列 → ポインタ初期化と等価 */
        }
      }
      if (may_strip) {
        set_curtok(t->next);  /* `(型)` を捨てて `{` から始める */
      }
    }
  }
  // `T arr[N] = {a,b,c,...}` 形式のグローバル配列初期化子。
  // 1D と多次元 (ネスト brace) の両方を flat 化して保持する。
  if (curtok()->kind == TK_LBRACE) {
    gv->has_init = 1;
    int cap = 16;
    gv->init_values = calloc((size_t)cap, sizeof(long long));
    gv->init_value_symbols = calloc((size_t)cap, sizeof(char *));
    gv->init_value_symbol_lens = calloc((size_t)cap, sizeof(int));
    /* 浮動小数要素の配列 (`double a[5] = {...}`) や、float/double メンバを持ち得る
     * struct/union では fvalues も並行確保する。要素ごとに fval を保存し、codegen が
     * 浮動小数メンバをビットパターンで出力する。 */
    if (gv->fp_kind != TK_FLOAT_KIND_NONE || gv->tag_kind != TK_EOF) {
      gv->init_fvalues = calloc((size_t)cap, sizeof(double));
    }
    gv->init_count = 0;
    psx_parse_global_brace_init_flat(gv, &cap, -1);
    /* C11 6.7.6.2p1: `T a[] = {...}` 形式は要素数を初期化子から推論する。
     * register 時には has_incomplete_array で type_size=0 にされているので
     * ここで埋め直す。 */
    if (gv->type_size == 0 && gv->is_array && gv->deref_size > 0 && gv->init_count > 0) {
      if (gv->tag_kind == TK_STRUCT && !gv->is_tag_pointer) {
        /* `struct P a[] = {1,2,3,4}`: init_count is flat scalar slots, while
         * type_size must be inferred in struct elements. */
        int elem_slots = global_flat_slot_count(gv->tag_kind, gv->tag_name, gv->tag_len);
        if (elem_slots < 1) elem_slots = 1;
        int outer_dim = (gv->init_count + elem_slots - 1) / elem_slots;
        int total_slots = outer_dim * elem_slots;
        pad_global_init_zeros(gv, &cap, total_slots);
        gv->type_size = outer_dim * gv->deref_size;
      } else if (gv->outer_stride > gv->deref_size) {
        /* `int a[][3][5]={{...},{...}}`: 外側次元を内側 slab (outer_stride) から推論。 */
        int inner_slots = gv->outer_stride / gv->deref_size;
        if (inner_slots > 0) {
          int outer_dim = (gv->init_count + inner_slots - 1) / inner_slots;
          int total_slots = outer_dim * inner_slots;
          pad_global_init_zeros(gv, &cap, total_slots);
          gv->type_size = total_slots * gv->deref_size;
        } else {
          gv->type_size = gv->init_count * gv->deref_size;
        }
      } else {
        gv->type_size = gv->init_count * gv->deref_size;
      }
    }
    /* C11 6.3.1.2: `_Bool a[N]={...}` の各要素初期化子を 0/1 に正規化する。
     * (配列ブランチはここで早期 return するため末尾のスカラ正規化には到達しない。) */
    if (gv->elem_is_bool && gv->init_values) {
      for (int i = 0; i < gv->init_count; i++) {
        gv->init_values[i] = (gv->init_values[i] != 0) ? 1 : 0;
      }
    }
    return;
  }
  node_t *init_expr = psx_expr_assign();
  /* `int g = -42;` のように unary minus を含む式は ND_NUM ではなく
   * ND_SUB(0, 42) になる。const 畳み込みできる式は折りたたんで init_val に格納する。 */
  int const_ok = 1;
  long long folded = init_expr ? psx_decl_eval_const_int(init_expr, &const_ok) : 0;
  /* グローバル double/float 用の定数式畳み込み (`double v = 1.5 + 2.5;`)。
   * 各 ND_NUM の fval を取り、ND_ADD/SUB/MUL/DIV/単項マイナスを再帰評価する。 */
  int fp_const_ok = (gv->fp_kind != TK_FLOAT_KIND_NONE);
  double fp_folded = 0.0;
  if (fp_const_ok && init_expr) {
    fp_folded = psx_eval_const_fp(init_expr, &fp_const_ok);
  }
  if (init_expr && init_expr->kind == ND_NUM) {
    gv->has_init = 1;
    node_num_t *n = (node_num_t *)init_expr;
    gv->init_val = n->val;
    /* グローバル変数が浮動小数スカラなら fval をビット出力用に保存する。
     * `double v = 3;` のように整数リテラルでも、宣言型 fp_kind を優先する。 */
    if (gv->fp_kind != TK_FLOAT_KIND_NONE) {
      gv->fval = (n->base.fp_kind != TK_FLOAT_KIND_NONE) ? n->fval : (double)n->val;
    }
  } else if (init_expr && gv->fp_kind != TK_FLOAT_KIND_NONE && fp_const_ok) {
    /* 浮動小数の定数式 (`1.5 + 2.5`): fp_folded を fval に保存。 */
    gv->has_init = 1;
    gv->fval = fp_folded;
  } else if (init_expr && const_ok) {
    gv->has_init = 1;
    gv->init_val = folded;
  } else if (init_expr &&
             (init_expr->kind == ND_ADDR || init_expr->kind == ND_GVAR ||
              init_expr->kind == ND_ADD || init_expr->kind == ND_SUB)) {
    /* `int *p = &x;` / `int *p = a + 1;` / `int *p = &a[1];` 等の
     * グローバル/配列アドレス + オフセット初期化。 */
    char *asym = NULL; int asym_len = 0; long long aoff = 0;
    if (resolve_global_addr_init(init_expr, &asym, &asym_len, &aoff)) {
      gv->has_init = 1;
      gv->init_symbol = asym;
      gv->init_symbol_len = asym_len;
      gv->init_symbol_offset = aoff;
    }
  } else if (init_expr && init_expr->kind == ND_FUNCREF) {
    /* `int (*gp)(int,int) = add;` グローバル関数ポインタ初期化。
     * codegen は init_symbol を `.quad _<funcname>` として出力する。 */
    node_funcref_t *fr = (node_funcref_t *)init_expr;
    gv->has_init = 1;
    gv->init_symbol = fr->funcname;
    gv->init_symbol_len = fr->funcname_len;
  } else if (init_expr && init_expr->kind == ND_STRING) {
    node_string_t *s = (node_string_t *)init_expr;
    /* `char *p = "...";` のようなポインタ変数 (配列ではない) では、
     * 文字列ラベル `.LCn` のアドレスを `.quad` で書き出す。 */
    if (!gv->is_array && gv->type_size == 8) {
      gv->has_init = 1;
      gv->init_symbol = s->string_label;
      gv->init_symbol_len = -1;  /* sentinel: emit raw label (no `_` prefix) */
    } else {
      /* C11 6.7.6.2p1 + 6.7.9p14: `char a[] = "...";` / `unsigned short a[] = u"..";` /
       * `T a[] = U".."`/`L".."` 形式。文字列の各コード単位と null 終端を init_values へ展開し
       * type_size を確定する。要素幅 (elem) が文字列の char_width (char/u8=1, u=2, U/L=4) と
       * 一致するときのみ (ASCII 内容のみ。非 ASCII の UTF-8→UTF-16/32 デコードは未対応)。
       * emit (emit_one_global_var) は deref_size 幅で .byte/.short/.long 出力する。 */
      int elem = gv->deref_size > 0 ? gv->deref_size : 1;
      int cw = s->char_width > 0 ? (int)s->char_width : 1;
      if (elem == cw) {
        int total = s->byte_len + 1; /* null 終端を含む (要素数) */
        gv->has_init = 1;
        gv->init_values = calloc((size_t)total, sizeof(long long));
        string_lit_t *lit = NULL;
        for (string_lit_t *l = string_literals; l; l = l->next) {
          if (strcmp(l->label, s->string_label) == 0) { lit = l; break; }
        }
        if (lit) {
          /* lit->str はソースのまま (raw)。エスケープシーケンスをデコードして
           * 各コード単位を格納する (ローカル `a[]=".."` と同じ処理)。これがないと
           * グローバル `char g[]="a\tb"` が `\` と `t` をそのまま書いて壊れていた。 */
          int idx = 0, sp = 0;
          while (sp < lit->len && idx < s->byte_len) {
            uint32_t units[2];
            int nu = tk_next_string_code_units(lit->str, lit->len, &sp, elem, units);
            for (int k = 0; k < nu && idx < s->byte_len; k++)
              gv->init_values[idx++] = units[k];
          }
        }
        gv->init_values[s->byte_len] = 0;
        gv->init_count = total;
        if (gv->type_size == 0 && gv->is_array) gv->type_size = total * elem;
      }
    }
  }
  /* C11 6.3.1.2: _Bool スカラの初期化子は 0/1 に正規化する (`_Bool b = 5;` → 1)。 */
  if (gv->is_bool && gv->has_init) {
    gv->init_val = (gv->init_val != 0) ? 1 : 0;
  }
}

// 多次元配列の各次元 dims[0..count-1] から outer/mid/extra strides を計算して
// global_var_t に書き込む (lvar の多次元配列と同じ計算)。
static void apply_global_multidim_strides(global_var_t *gv, const int *dims, int dim_count,
                                          int elem_size) {
  if (dim_count < 2 || elem_size <= 0) return;
  int outer_mul = 1;
  for (int i = 1; i < dim_count; i++) {
    if (dims[i] > 0) outer_mul *= dims[i];
  }
  gv->outer_stride = outer_mul * elem_size;
  if (dim_count >= 3) {
    int mid_mul = 1;
    for (int i = 2; i < dim_count; i++) {
      if (dims[i] > 0) mid_mul *= dims[i];
    }
    gv->mid_stride = mid_mul * elem_size;
  }
  if (dim_count >= 4) {
    int idx_in_extras = 0;
    for (int start = 3; start < dim_count && idx_in_extras < 5; start++) {
      int rest_mul = 1;
      for (int j = start; j < dim_count; j++) {
        if (dims[j] > 0) rest_mul *= dims[j];
      }
      gv->extra_strides[idx_in_extras++] = rest_mul * elem_size;
    }
    gv->extra_strides_count = (unsigned char)idx_in_extras;
  }
}

static void apply_toplevel_object_from_head(toplevel_declarator_head_t head) {
  if (head.name && curtok()->kind == TK_LPAREN) {
    register_toplevel_function_prototype(head.name, head.is_ptr);
    if (curtok()->kind == TK_ASSIGN) {
      set_curtok(curtok()->next);
      psx_expr_assign();
    }
    return;
  }
  if (g_toplevel_decl_tag_kind != TK_EOF && !head.is_ptr &&
      psx_ctx_get_tag_member_count(g_toplevel_decl_tag_kind, g_toplevel_decl_tag_name,
                                   g_toplevel_decl_tag_len) <= 0) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_INCOMPLETE_OBJECT_FORBIDDEN));
  }
  toplevel_array_suffix_t arr = parse_toplevel_array_suffixes(head.paren_array_mul);
  psx_ret_pointee_array_t direct_funcptr_ret_pointee_array = {0};
  if (head.is_ptr && g_toplevel_decl_has_func_suffix && arr.is_array &&
      arr.dim_count >= 1 && !arr.has_incomplete_array) {
    /* 関数ポインタグローバルが配列へのポインタを返す直書き宣言子:
     * `int (*(*g)(void))[N]` / `int (*(*g)(void))[N][M]`。
     * trailing `[N][M]` はグローバル自身の配列ではなく、戻り値ポインタの pointee 配列次元。 */
    psx_ret_pointee_array_absorb_suffix(&arr.is_array, &arr.arr_total,
                                        &arr.dim_count, &arr.first_dim,
                                        arr.dims, 8,
                                        g_toplevel_decl_elem_size,
                                        &direct_funcptr_ret_pointee_array);
  }
  /* `T (*pa)[N]` (配列へのポインタ): `*` が括弧内 (ptr_in_paren_group) で、外側に `[N]`
   * 配列サフィックス (arr.is_array) が付く形。pa は 8B のスカラポインタで、`[N]` は
   * **pointee 配列**の次元。parse_toplevel_array_suffixes は `[N]` を arr.is_array=1 /
   * arr_total=N にするため、`int *pa[N]` (ポインタの配列) と同一に登録され、pa が
   * 「N 要素配列」と誤って扱われて subscript が pa のポインタ値をロードせず &pa を base に
   * して隣接メモリを読んでいた (SIGSEGV / 誤値)。配列扱いを解除し、pointee の各次元を
   * subscript ストライドに記録する (ローカル decl.c の `(*p)[N]...` 分岐と同じ表現)。関数
   * ポインタ (`(*f)(args)`) は has_func_suffix で除外。 */
  int is_ptr_to_array = (head.is_ptr && g_toplevel_decl_ptr_in_paren_group &&
                         !g_toplevel_decl_has_func_suffix && arr.is_array &&
                         arr.dim_count >= 1 && !arr.has_incomplete_array);
  int pointee_total = arr.arr_total;        /* pointee 全要素数 (= 全次元の積) */
  int pointee_dims[8];
  int pointee_dim_count = arr.dim_count;
  for (int i = 0; i < arr.dim_count && i < 8; i++) pointee_dims[i] = arr.dims[i];
  /* pointer-to-array typedef 経由 `typedef int (*PA)[3]; PA gp;`: 直書き `int (*gp)[3]`
   * と違い宣言子に括弧も trailing `[N]` も無いので上の is_ptr_to_array では検出できない。
   * typedef に記録したポインティ dims から同じセットアップを行う (base のポインタ性は
   * head.is_ptr で既に立っている)。 */
  if (!is_ptr_to_array && head.is_ptr && !arr.is_array &&
      g_toplevel_decl_td_ptr_pointee_dim_count > 0) {
    is_ptr_to_array = 1;
    pointee_dim_count = g_toplevel_decl_td_ptr_pointee_dim_count;
    pointee_total = 1;
    for (int i = 0; i < pointee_dim_count && i < 8; i++) {
      pointee_dims[i] = g_toplevel_decl_td_array_dims[i];
      if (pointee_dims[i] > 0) pointee_total *= pointee_dims[i];
    }
  }
  if (is_ptr_to_array) {
    arr.is_array = 0;
    arr.arr_total = 1;
    arr.dim_count = 0;
  }
  /* 要素数 1 の括弧内配列 `(*g[1])(...)` / `(*g[1])`: paren_array_mul=1 だと
   * arr.is_array が立たず (is_array = base_mul>1)、スカラ funcptr/ポインタとして誤登録され
   * subscript `g[0]` が crash していた。括弧内に配列サフィックスがあれば要素数によらず
   * 配列として登録する。pointer-to-array (trailing `[N]`) は is_ptr_to_array で別処理済み。 */
  if (!is_ptr_to_array && g_toplevel_decl_paren_array_present && !arr.is_array) {
    arr.is_array = 1;
    arr.arr_total = (head.paren_array_mul > 0) ? head.paren_array_mul : 1;
    arr.dim_count = 1;
    arr.dims[0] = arr.arr_total;
  }
  /* 多次元の括弧内配列 `int (*t[2][2])(void)`: parse_toplevel_array_suffixes は
   * paren_array_mul(積=4) で is_array=1 にするが trailing `[N]` が無いので dim_count=0。
   * 捕捉した個別 dims (g_toplevel_decl_paren_array_dims) で埋め直し、下の
   * apply_global_multidim_strides がストライドを立てられるようにする。 */
  if (!is_ptr_to_array && g_toplevel_decl_paren_array_present &&
      g_toplevel_decl_paren_array_dim_count >= 2 && arr.dim_count < 2) {
    arr.is_array = 1;
    arr.dim_count = g_toplevel_decl_paren_array_dim_count;
    if (arr.dim_count > 8) arr.dim_count = 8;
    arr.arr_total = 1;
    for (int i = 0; i < arr.dim_count; i++) {
      arr.dims[i] = g_toplevel_decl_paren_array_dims[i];
      arr.arr_total *= arr.dims[i];
    }
  }
  validate_toplevel_object_array_suffix(arr);
  global_var_t *gv = register_toplevel_object_from_declarator(head.name, head.is_ptr, arr);
  /* _Generic 用: 先頭宣言子の型を name 抜きでトークン文字列化してグローバル sig 表に記録する。
   * 複雑な派生型 ('(' を含む funcptr / ネスト宣言子) のみ非 NULL。consume-once で先頭のみ。 */
  if (g_toplevel_typespec_start && gv && head.name) {
    char *sig = psx_serialize_decl_type_tokens(g_toplevel_typespec_start, curtok(),
                                               (token_t *)head.name);
    if (sig) psx_record_global_type_sig(head.name->str, head.name->len, sig);
    g_toplevel_typespec_start = NULL;
  }
  if (gv && is_ptr_to_array) {
    /* 第1subscript `pa[i]` は pointee 全体 (pointee_total*elem) をステップ。多次元 pointee
     * `(*pa)[N][M]` では pa[i][j]=行 (M*elem)、pa[i][j][k]=要素 (elem)。これは「先頭に
     * ポインタ index の仮想次元を 1 つ足した多次元配列」のストライドと一致するので、仮想
     * 次元 1 を先頭に付けた dims で apply_global_multidim_strides を再利用する (outer_stride
     * = pointee 全体、mid_stride 以降が内側)。deref_size(=elem) は register 設定済み。
     * try_build_global_var_node のスカラ分岐が outer/mid/extra を node に反映する。 */
    int elem = g_toplevel_decl_elem_size;
    if (pointee_dim_count >= 2) {
      int vdims[9];
      vdims[0] = 1;
      for (int i = 0; i < pointee_dim_count && i + 1 < 9; i++) vdims[i + 1] = pointee_dims[i];
      apply_global_multidim_strides(gv, vdims, pointee_dim_count + 1, elem);
    } else {
      gv->outer_stride = pointee_total * elem;
    }
  }
  if (gv && arr.is_array && arr.dim_count >= 2) {
    /* 多次元配列のストライド設定。ポインタ要素配列 (`int *t[2][2]` / `char *names[2][3]`) は
     * 要素がポインタ値 (8B) なので elem_size=8。以前は `!head.is_ptr` でポインタ配列を除外して
     * いたため stride が立たず、`t[i]` が「ポインタ値として load → [j] で deref」と誤計算され
     * SIGSEGV になっていた (非ポインタ 2D 配列は元から動作)。最終要素 load 幅と
     * pointee_is_scalar_ptr の中間次元伝播は build_subscript_deref 側で扱う。 */
    int ms_elem = head.is_ptr ? 8 : g_toplevel_decl_elem_size;
    apply_global_multidim_strides(gv, arr.dims, arr.dim_count, ms_elem);
  }
  /* スカラデータポインタ `double *dp`: pointee スカラの fp_kind を保存し、`*dp` / `dp[i]`
   * の deref を fp load にする (ローカル `double *a` と同じ)。単段ポインタ (ptr_levels==1)
   * で配列でなく関数ポインタでもない場合に限定する。pointer-to-array `double (*pa)[N]`
   * (head.paren_array_mul>1) は pointee がスカラ double でないため除外。funcptr は
   * register_toplevel_global_decl が既に設定済み。 */
  /* 単段データポインタの pointee fp_kind。直書き `double *dp` (ptr_levels==1) に加え、
   * ポインタ typedef `typedef double *PD; PD pd;` (基底がポインタ = base_is_ptr) も対象に
   * する。実効ポインタ段数 = ptr_levels + base_is_ptr が 1 のときに限定し、`double **` 等の
   * 多段を除外する。 */
  if (gv && head.is_ptr && !arr.is_array && head.paren_array_mul <= 1 &&
      !g_toplevel_decl_has_func_suffix &&
      (g_toplevel_decl_ptr_levels + (g_toplevel_decl_base_is_ptr ? 1 : 0)) == 1 &&
      g_toplevel_decl_fp_kind != TK_FLOAT_KIND_NONE) {
    gv->pointee_fp_kind = (unsigned char)g_toplevel_decl_fp_kind;
  }
  /* 関数ポインタ配列グローバル `double (*gops[N])(double)`: 戻り型 fp_kind を pointee_fp_kind
   * に保存する (funcptr スカラ global と同じ。fp_kind は配列ではビットパターン出力に使えない)。
   * try_build_global_var_node の配列分岐がこれを ND_ADDR の pointee_fp_kind に伝播し、
   * `gops[i](x)` の funcall が戻り値を d0 で読む。 */
  if (gv && head.is_ptr && arr.is_array && g_toplevel_decl_has_func_suffix &&
      g_toplevel_decl_fp_kind != TK_FLOAT_KIND_NONE) {
    gv->pointee_fp_kind = (unsigned char)g_toplevel_decl_fp_kind;
  }
  if (gv && head.is_ptr && g_toplevel_decl_has_func_suffix && psx_last_funcptr_is_variadic()) {
    gv->is_variadic_funcptr = 1;
    gv->funcptr_nargs_fixed = (short)psx_last_funcptr_nargs_fixed();
  }
  if (gv && head.is_ptr) {
    unsigned short fp_mask = g_toplevel_decl_has_func_suffix
                                 ? psx_last_funcptr_param_fp_mask()
                                 : g_toplevel_decl_base_funcptr_param_fp_mask;
    unsigned short int_mask = g_toplevel_decl_has_func_suffix
                                  ? psx_last_funcptr_param_int_mask()
                                  : g_toplevel_decl_base_funcptr_param_int_mask;
    gv->funcptr_param_fp_mask = fp_mask;
    gv->funcptr_param_int_mask = int_mask;
    psx_ret_pointee_array_t ret_pointee_array = psx_ret_pointee_array_select(
        direct_funcptr_ret_pointee_array,
        g_toplevel_decl_base_funcptr_ret_pointee_array);
    psx_ret_pointee_array_store_shorts_if_present(
        ret_pointee_array,
        &gv->funcptr_ret_pointee_array_first_dim,
        &gv->funcptr_ret_pointee_array_second_dim,
        &gv->funcptr_ret_pointee_array_elem_size);
  }
  finalize_toplevel_object_declarator(gv);
}

static toplevel_declarator_head_t parse_toplevel_declarator_head(int base_is_ptr, int require_name) {
  toplevel_declarator_head_t out = new_toplevel_declarator_head(base_is_ptr);
  g_toplevel_decl_has_func_suffix = 0;
  g_toplevel_decl_funcptr_ret_is_pointer = 0;
  g_toplevel_decl_ptr_levels = 0;
  g_toplevel_decl_ptr_in_paren_group = 0;
  g_toplevel_decl_paren_array_present = 0;
  g_toplevel_decl_paren_array_dim_count = 0;
  for (int i = 0; i < 8; i++) g_toplevel_decl_paren_array_dims[i] = 0;
  psx_consume_pointer_prefix(&out.is_ptr);
  if (g_toplevel_decl_is_typedef && out.is_ptr &&
      (g_toplevel_decl_tag_kind == TK_STRUCT || g_toplevel_decl_tag_kind == TK_UNION)) {
    g_toplevel_decl_funcptr_ret_is_pointer = 1;
  }
  out.name = parse_toplevel_decl_name(&out.is_ptr, &out.paren_array_mul);
  if (!out.name && require_name) emit_decl_name_required_diag();
  return out;
}

static toplevel_declarator_head_t new_toplevel_declarator_head(int base_is_ptr) {
  toplevel_declarator_head_t out = {0};
  out.is_ptr = base_is_ptr;
  out.paren_array_mul = 1;
  return out;
}

static void validate_toplevel_object_array_suffix(toplevel_array_suffix_t arr) {
  if (!arr.has_incomplete_array || g_toplevel_decl_is_extern) return;
  /* C11 6.7.6.2p1: 初期化子がある場合、配列のサイズは初期化子から推論できる。
   * 後段の apply_toplevel_object_initializer で type_size を再計算する。 */
  if (curtok() && curtok()->kind == TK_ASSIGN) return;
  psx_diag_ctx(curtok(), "decl", "%s",
               diag_message_for(DIAG_ERR_PARSER_INCOMPLETE_OBJECT_FORBIDDEN));
}

static void finalize_toplevel_object_declarator(global_var_t *gv) {
  if (g_toplevel_decl_is_extern) {
    consume_toplevel_extern_initializer_if_any();
    return;
  }
  gv->is_thread_local = g_toplevel_decl_is_thread_local;
  apply_toplevel_object_initializer(gv);
}

static global_var_t *register_toplevel_object_from_declarator(token_ident_t *name, int is_ptr,
                                                               toplevel_array_suffix_t arr) {
  return register_toplevel_global_decl(name->str, name->len, is_ptr, arr.is_array, arr.arr_total,
                                       current_toplevel_extern_flag(), arr.has_incomplete_array);
}

static int current_toplevel_extern_flag(void) {
  return g_toplevel_decl_is_extern ? 1 : 0;
}

static void consume_toplevel_extern_initializer_if_any(void) {
  if (tk_consume('=')) {
    psx_expr_assign(); // extern宣言では通常ないが消費する
  }
}

static void define_toplevel_typedef_from_declarator(token_ident_t *name, int is_ptr,
                                                    int paren_array_mul) {
  toplevel_array_suffix_t arr = parse_toplevel_array_suffixes(paren_array_mul);
  int typedef_sizeof = compute_toplevel_typedef_sizeof(is_ptr, arr);
  token_kind_t stored_base_kind = resolve_toplevel_typedef_base_kind_for_store();
  /* pointer-element 配列 typedef (`typedef BinOp OpArr3[3]`): base が pointer typedef + 配列
   * suffix のとき、is_array=1 として登録する (sizeof_size 経路と同じく ptr_in_paren_group=0
   * かつ declarator に `*` 追加なしを条件にして pointer-to-array typedef とは排他)。 */
  int base_ptr_elem_array = (is_ptr && !g_toplevel_decl_ptr_in_paren_group &&
                             g_toplevel_decl_ptr_levels == 0 &&
                             arr.is_array && arr.arr_total > 0) ? 1 : 0;
  /* `typedef int *IP[5]`: declarator の `*` (ptr_levels>=1) と `[N]` 配列 suffix の組合せは
   * 「N 個のポインタ配列」。is_array=1 として記録する。 */
  int decl_ptr_array = (is_ptr && !g_toplevel_decl_ptr_in_paren_group &&
                        g_toplevel_decl_ptr_levels >= 1 &&
                        arr.is_array && arr.arr_total > 0) ? 1 : 0;
  int td_is_array = ((!is_ptr || base_ptr_elem_array || decl_ptr_array) &&
                     (arr.is_array || arr.has_incomplete_array)) ? 1 : 0;
  int td_first_dim = td_is_array ? arr.first_dim : 0;
  int td_dim_count = td_is_array ? arr.dim_count : 0;
  const int *td_dims = arr.dims;
  /* 多次元 typedef chain: 基底 typedef が自身配列の場合 (`typedef int Row[3]; typedef Row Matrix[2]`)、
   * declarator の dims (= [2]) と base typedef の dims (= [3]) を [declarator..., base...] の順で
   * 結合し、新しい typedef の dims を [2, 3] にする。これがないと Matrix は int[2] として登録され、
   * sizeof(Matrix)=24 のはずが 8 になり、`Matrix m; m[i][j]` も誤計算する。
   * 条件は base が配列 (g_toplevel_decl_td_array_dim_count>0) かつ declarator も配列 (td_is_array)、
   * かつ pointer-to-array typedef でない (!is_ptr, !ptr_in_paren_group)。
   * pointer-element 配列 typedef (`typedef IP IPA[3]`、base_ptr_elem_array) は base が array でなく
   * ポインタなので td_array_dim_count=0 で自然に除外される。 */
  static int s_merged_dims[8];
  if (td_is_array && !is_ptr && !g_toplevel_decl_ptr_in_paren_group &&
      g_toplevel_decl_td_array_dim_count > 0) {
    int n = 0;
    for (int i = 0; i < arr.dim_count && n < 8; i++) {
      s_merged_dims[n++] = arr.dims[i];
    }
    for (int i = 0; i < g_toplevel_decl_td_array_dim_count && n < 8; i++) {
      s_merged_dims[n++] = g_toplevel_decl_td_array_dims[i];
    }
    td_dims = s_merged_dims;
    td_dim_count = n;
    td_first_dim = (n > 0) ? s_merged_dims[0] : td_first_dim;
    int prod = 1;
    for (int i = 0; i < n; i++) prod *= s_merged_dims[i];
    typedef_sizeof = g_toplevel_decl_elem_size * prod;
  }
  /* pointer-to-array typedef `typedef int (*PA)[3]`: is_ptr=1 で `*` が括弧内
   * (ptr_in_paren_group) のとき、括弧の後ろの `[3]` (arr に入っている) はポインティ
   * 配列の extent。is_array=0 のままその dims を typedef に記録する。これがないと
   * `PA p; p+1 / p[i]` が要素 1 個 (4B) しか進まず、直書き `int(*p)[3]` と食い違う。
   * 宣言側 (decl.c の `is_pointer && td_array_dim_count>0` 分岐) が outer_stride /
   * mid_stride をこの dims から設定する。ポインタの配列 `int *PB[3]` は `*` が括弧外
   * (ptr_in_paren_group=0) なので除外される。 */
  if (is_ptr && g_toplevel_decl_ptr_in_paren_group && arr.is_array && arr.dim_count > 0) {
    td_first_dim = arr.first_dim;
    td_dim_count = arr.dim_count;
    td_dims = arr.dims;
  }
  psx_ret_pointee_array_t funcptr_ret_pointee_array = {0};
  if (is_ptr && g_toplevel_decl_has_func_suffix &&
      !g_toplevel_decl_paren_array_present && arr.first_dim > 0) {
    funcptr_ret_pointee_array =
        psx_ret_pointee_array_make(arr.first_dim,
                                   (arr.dim_count >= 2) ? arr.dims[1] : 0,
                                   g_toplevel_decl_elem_size);
  }
  register_toplevel_typedef_name(name, stored_base_kind, is_ptr, typedef_sizeof, td_is_array,
                                 td_first_dim, td_dims, td_dim_count,
                                 funcptr_ret_pointee_array);
  /* 多段ポインタ typedef (`typedef int **PP`) の段数を記録する。単段や pointer-to-array
   * は getter のデフォルト (is_pointer→1) に任せ、2 段以上だけ明示保存。段数 = 基底ポインタ
   * typedef の段数 + 宣言子の prefix `*` 数 (g_toplevel_decl_ptr_levels)。 */
  int td_ptr_levels = g_toplevel_decl_base_pointer_levels + g_toplevel_decl_ptr_levels;
  if (is_ptr && td_ptr_levels >= 2) {
    psx_ctx_set_typedef_pointer_levels(name->str, name->len, td_ptr_levels);
  }
}

static void register_toplevel_typedef_name(token_ident_t *name, token_kind_t stored_base_kind,
                                           int is_ptr, int typedef_sizeof, int td_is_array,
                                           int td_first_dim,
                                           const int *td_dims, int td_dim_count,
                                           psx_ret_pointee_array_t funcptr_ret_pointee_array) {
  psx_typedef_info_t _ti = {0};
  _ti.base_kind = stored_base_kind;
  _ti.elem_size = g_toplevel_decl_elem_size;
  _ti.fp_kind = g_toplevel_decl_fp_kind;
  _ti.tag_kind = g_toplevel_decl_tag_kind;
  _ti.tag_name = g_toplevel_decl_tag_name;
  _ti.tag_len = g_toplevel_decl_tag_len;
  _ti.is_pointer = is_ptr;
  _ti.sizeof_size = typedef_sizeof;
  _ti.pointee_const_qualified = g_toplevel_decl_pointee_const;
  _ti.pointee_volatile_qualified = g_toplevel_decl_pointee_volatile;
  _ti.is_unsigned = is_toplevel_typedef_unsigned(stored_base_kind);
  _ti.is_array = td_is_array;
  _ti.array_first_dim = td_first_dim;
  _ti.array_dim_count = td_dim_count;
  if (td_dims) for (int i = 0; i < td_dim_count && i < 8; i++) _ti.array_dims[i] = td_dims[i];
  if (is_ptr && g_toplevel_decl_has_func_suffix) {
    _ti.is_funcptr = 1;
    _ti.funcptr_ret_is_pointer = g_toplevel_decl_funcptr_ret_is_pointer ? 1 : 0;
    _ti.funcptr_param_fp_mask = psx_last_funcptr_param_fp_mask();
    _ti.funcptr_param_int_mask = psx_last_funcptr_param_int_mask();
    psx_ret_pointee_array_store_ints_if_present(
        funcptr_ret_pointee_array,
        &_ti.funcptr_ret_pointee_array_first_dim,
        &_ti.funcptr_ret_pointee_array_second_dim,
        &_ti.funcptr_ret_pointee_array_elem_size);
  }
  if (!psx_ctx_define_typedef_name(name->str, name->len, &_ti)) {
    psx_diag_duplicate_with_name(curtok(), "typedef", name->str, name->len);
  }
}

static int is_toplevel_typedef_unsigned(token_kind_t stored_base_kind) {
  return (stored_base_kind == TK_UNSIGNED) || psx_last_type_is_unsigned();
}

static int compute_toplevel_typedef_sizeof(int is_ptr, toplevel_array_suffix_t arr) {
  int typedef_sizeof = is_ptr ? 8 : g_toplevel_decl_elem_size;
  if (!is_ptr && arr.has_incomplete_array) return 0;
  if (!is_ptr && arr.is_array && arr.arr_total > 0) typedef_sizeof *= arr.arr_total;
  /* pointer-element 配列 typedef (`typedef BinOp OpArr3[3]`): base が pointer typedef
   * かつ declarator は `*` を追加していない (= ptr_in_paren_group=0 で base のみ由来) +
   * 配列 suffix が立つケース。sizeof = 8 (pointer) * arr_total。これがないと typedef の
   * sizeof_size が 8 (base pointer サイズのまま) になり、宣言側で OpArr3 *pa の要素サイズ
   * が誤判定される。pointer-to-array typedef (`typedef int (*PA)[3]`、ptr_in_paren_group=1)
   * とは排他。 */
  if (is_ptr && !g_toplevel_decl_ptr_in_paren_group &&
      g_toplevel_decl_ptr_levels == 0 &&
      arr.is_array && arr.arr_total > 0) {
    typedef_sizeof = 8 * arr.arr_total;
  }
  /* 直書きの「array of pointer typedef」(`typedef int *IP[5]`): declarator が `*` を追加
   * (ptr_levels>=1) かつ `[N]` 配列 suffix がある形は「N 個のポインタ配列」なので
   * sizeof = 8 (pointer) * arr_total。修正前は単一ポインタ扱いで 8 のまま、`IP a; a[0]=&g;
   * *a[0]` が SIGSEGV していた。pointer-to-array typedef (`int (*PA)[3]`) は
   * ptr_in_paren_group=1 で除外。 */
  if (is_ptr && !g_toplevel_decl_ptr_in_paren_group &&
      g_toplevel_decl_ptr_levels >= 1 &&
      arr.is_array && arr.arr_total > 0) {
    typedef_sizeof = 8 * arr.arr_total;
  }
  return typedef_sizeof;
}

static token_kind_t resolve_toplevel_typedef_base_kind_for_store(void) {
  token_kind_t stored_base_kind = g_toplevel_decl_base_kind;
  if (stored_base_kind == TK_INT && psx_last_type_is_unsigned()) return TK_UNSIGNED;
  return stored_base_kind;
}

static void apply_toplevel_typedef_from_head(toplevel_declarator_head_t head) {
  define_toplevel_typedef_from_declarator(head.name, head.is_ptr, head.paren_array_mul);
}

static int has_next_toplevel_declarator(void) {
  return tk_consume(',');
}

static token_ident_t *parse_toplevel_decl_name(int *is_ptr, int *out_paren_array_mul) {
  return parse_decl_name_recursive(is_ptr, 1, out_paren_array_mul);
}

/* curtok が `(` のとき仮引数リストだけを消費する (関数名は既に読んだ後)。 */
static void parse_func_param_list_only(int *out_is_variadic, int *out_has_unnamed_param,
                                       node_t ***out_args, int *out_nargs) {
  int arg_cap = 16;
  node_t **args = calloc(arg_cap, sizeof(node_t *));
  int nargs = 0;
  int is_variadic = 0;
  int has_unnamed_param = 0;
  tk_expect('(');
  if (!tk_consume(')')) {
    bool done = false;
    node_func_t node_tmp = {0};
    node_tmp.args = args;
    while (!done) {
      if (curtok()->kind == TK_ELLIPSIS) {
        set_curtok(curtok()->next);
        if (curtok()->kind == ',') {
          diag_emit_tokf(DIAG_ERR_PARSER_INVALID_CONTEXT, curtok(), "%s",
                         diag_message_for(DIAG_ERR_PARSER_VARIADIC_NOT_LAST));
        }
        is_variadic = 1;
        done = true;
        continue;
      }
      if (parse_param_decl(&node_tmp, &nargs, &arg_cap, 1)) has_unnamed_param = 1;
      args = node_tmp.args;
      if (!tk_consume(',')) break;
      if (curtok()->kind == TK_RPAREN) {
        psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_PARAMETER));
      }
    }
    tk_expect(')');
  }
  *out_is_variadic = is_variadic;
  *out_has_unnamed_param = has_unnamed_param;
  *out_args = args;
  *out_nargs = nargs;
}

/* `int f(int), g(int), a;` の f/g 等: funcdef と同様に関数テーブルへプロトタイプを登録する。 */
static void register_toplevel_function_prototype(token_ident_t *tok, int declarator_is_ptr) {
  if (!tok || curtok()->kind != TK_LPAREN) return;
  psx_decl_reset_locals();
  token_kind_t ret_kind = g_toplevel_decl_base_kind;
  tk_float_kind_t ret_fp_kind = g_toplevel_decl_fp_kind;
  token_kind_t ret_token_kind = (ret_kind == TK_EOF) ? TK_INT : ret_kind;
  int ret_is_ptr = g_toplevel_decl_base_is_ptr || declarator_is_ptr;
  int ret_base_unsigned = g_toplevel_decl_is_unsigned;
  int ret_is_unsigned = !ret_is_ptr && ret_base_unsigned;
  int ret_is_complex = !ret_is_ptr && psx_last_type_is_complex();
  int ret_struct_size = 0;
  if ((g_toplevel_decl_tag_kind == TK_STRUCT || g_toplevel_decl_tag_kind == TK_UNION) &&
      !ret_is_ptr && g_toplevel_decl_tag_name &&
      psx_ctx_has_tag_type(g_toplevel_decl_tag_kind, g_toplevel_decl_tag_name,
                           g_toplevel_decl_tag_len)) {
    ret_struct_size = psx_ctx_get_tag_size(g_toplevel_decl_tag_kind, g_toplevel_decl_tag_name,
                                           g_toplevel_decl_tag_len);
  }
  int is_variadic = 0;
  int has_unnamed_param = 0;
  node_t **args = NULL;
  int nargs = 0;
  parse_func_param_list_only(&is_variadic, &has_unnamed_param, &args, &nargs);
  psx_skip_gnu_attributes();
  (void)has_unnamed_param;
  if (find_global_var_by_name(tok->str, tok->len)) {
    psx_diag_ctx(curtok(), "decl",
                 "'%.*s' はグローバル変数として既に宣言されています (C11 6.7p4)",
                 tok->len, tok->str);
  }
  psx_ctx_define_function_name_with_ret(tok->str, tok->len, ret_struct_size);
  if (ret_fp_kind != TK_FLOAT_KIND_NONE && !ret_is_ptr) {
    psx_ctx_set_function_ret_fp_kind(tok->str, tok->len, ret_fp_kind);
  }
  if (ret_is_complex) {
    psx_ctx_set_function_ret_is_complex(tok->str, tok->len, 1);
  }
  if (ret_kind == TK_VOID && !ret_is_ptr) {
    psx_ctx_set_function_ret_void(tok->str, tok->len, 1);
  }
  if (!psx_ctx_track_function_ret_type(tok->str, tok->len, ret_token_kind, ret_is_ptr)) {
    psx_diag_ctx(curtok(), "decl",
                 "関数 '%.*s' の戻り値型が以前の宣言と異なります (C11 6.7p3)",
                 tok->len, tok->str);
  }
  if (ret_base_unsigned) psx_ctx_set_function_ret_unsigned(tok->str, tok->len, 1);
  if (ret_is_ptr && (g_toplevel_decl_pointee_const || g_toplevel_decl_pointee_volatile)) {
    psx_ctx_set_function_ret_pointee_qualifiers(tok->str, tok->len,
                                                g_toplevel_decl_pointee_const,
                                                g_toplevel_decl_pointee_volatile);
  }
  (void)ret_is_unsigned;
  if (!psx_ctx_track_function_nargs(tok->str, tok->len, nargs, is_variadic)) {
    psx_diag_ctx(curtok(), "decl",
                 "関数 '%.*s' の引数数が以前の宣言と異なります (C11 6.7p4)",
                 tok->len, tok->str);
  }
  psx_ctx_set_function_variadic(tok->str, tok->len, is_variadic ? 1 : 0, nargs);
  for (int i = 0; i < nargs && i < 16; i++) {
    tk_float_kind_t pfk = (tk_float_kind_t)(args[i] ? args[i]->fp_kind : 0);
    int param_cat = PSX_PCAT_UNSET;
    if (pfk == TK_FLOAT_KIND_FLOAT) param_cat = PSX_PCAT_FLOAT;
    else if (pfk >= TK_FLOAT_KIND_DOUBLE) param_cat = PSX_PCAT_DOUBLE;
    else if (args[i] && ps_node_is_pointer(args[i])) param_cat = PSX_PCAT_PTR;
    else if (args[i]) {
      int sz = ps_node_type_size(args[i]);
      if (sz >= 1 && sz <= 8) param_cat = PSX_PCAT_INT4;
      else if (sz > 0) param_cat = PSX_PCAT_STRUCT;
    }
    if (!psx_ctx_track_function_param_category(tok->str, tok->len, i, param_cat)) {
      psx_diag_ctx(curtok(), "decl",
                   "関数 '%.*s' の引数 %d の型が以前の宣言と異なります (C11 6.7p4)",
                   tok->len, tok->str, i + 1);
    }
    if (pfk != TK_FLOAT_KIND_NONE) {
      psx_ctx_set_function_param_fp_kind(tok->str, tok->len, i, pfk);
    } else if (args[i] && !ps_node_is_pointer(args[i])) {
      int sz = ps_node_type_size(args[i]);
      if (sz >= 1 && sz <= 4) {
        psx_ctx_set_function_param_int_size(tok->str, tok->len, i, 4);
      } else if (sz == 8) {
        psx_ctx_set_function_param_int_size(tok->str, tok->len, i, 8);
      }
    }
  }
  if ((g_toplevel_decl_tag_kind == TK_STRUCT || g_toplevel_decl_tag_kind == TK_UNION) &&
      g_toplevel_decl_tag_name) {
    psx_ctx_set_function_ret_tag(tok->str, tok->len, g_toplevel_decl_tag_kind,
                                 g_toplevel_decl_tag_name, g_toplevel_decl_tag_len);
  }
}

static token_ident_t *parse_decl_name_recursive(int *is_ptr, int require_name, int *out_paren_array_mul) {
  int ptr_before_prefix = *is_ptr;
  psx_consume_pointer_prefix(is_ptr);
  if (require_name && !ptr_before_prefix && *is_ptr && g_toplevel_decl_is_typedef &&
      (g_toplevel_decl_tag_kind == TK_STRUCT || g_toplevel_decl_tag_kind == TK_UNION)) {
    g_toplevel_decl_funcptr_ret_is_pointer = 1;
  }
  psx_skip_gnu_attributes();
  token_ident_t *name = NULL;
  int had_parens = 0;
  int paren_array_mul = 1;
  if (tk_consume('(')) {
    had_parens = 1;
    psx_skip_gnu_attributes();
    int ptr_before_inner = *is_ptr;
    name = parse_decl_name_recursive(is_ptr, require_name, &paren_array_mul);
    /* 括弧内で初めて `*` が立った (`(*pa)`): 配列へのポインタ / 関数ポインタの指標。
     * 後続の `[N]` サフィックスと合わせて pointer-to-array を識別する。 */
    if (*is_ptr && !ptr_before_inner) g_toplevel_decl_ptr_in_paren_group = 1;
    /* 括弧内に `[N]` があるか (要素数 1 でも配列扱いにするため、積ではなく有無を記録)。 */
    if (curtok()->kind == TK_LBRACKET) g_toplevel_decl_paren_array_present = 1;
    paren_array_mul = psx_parse_array_suffixes_capture_dims(
        paren_array_mul, g_toplevel_decl_paren_array_dims, 8,
        &g_toplevel_decl_paren_array_dim_count);
    tk_expect(')');
  } else {
    name = consume_decl_ident_or_error(require_name);
  }
  consume_toplevel_paren_decl_func_suffixes_if_any(had_parens);
  if (out_paren_array_mul) *out_paren_array_mul = paren_array_mul;
  return name;
}

static token_ident_t *consume_decl_ident_or_error(int require_name) {
  token_ident_t *name = tk_consume_ident();
  if (!name && require_name) {
    diag_emit_tokf(DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED, curtok(), "%s",
                   diag_message_for(DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED));
  }
  return name;
}

static void emit_decl_name_required_diag(void) {
  diag_emit_tokf(DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED, curtok(), "%s",
                 diag_message_for(DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED));
}

static void consume_toplevel_paren_decl_func_suffixes_if_any(int had_parens) {
  if (!had_parens) return;
  psx_reset_funcptr_signature_state();
  while (curtok()->kind == TK_LPAREN) {
    /* `(*gops)(double)` / `(*fprintfptr)(FILE*, const char*, ...)` の関数サフィックス。
     * skip_func_params で `...` を検出し、グローバル funcptr 登録と variadic 経由呼び出しに伝播。 */
    g_toplevel_decl_has_func_suffix = 1;
    psx_skip_func_param_list();
  }
}

static void parse_toplevel_decl_after_type(void) {
  if (g_toplevel_decl_is_typedef) {
    parse_toplevel_declarator_stmt(g_toplevel_decl_base_is_ptr, apply_toplevel_typedef_from_head);
    return;
  }
  /* ポインタ typedef を基底にしたグローバル変数 `typedef int *PI; PI gp;` では、
   * 基底のポインタ性 (g_toplevel_decl_base_is_ptr) を宣言子へ渡す必要がある。0 固定だと
   * gp が int スカラとして登録され sizeof=4 / subscript で E3064 になっていた。直書き
   * `int *gp` は base_is_ptr=0 + 宣言子の `*` で is_ptr が立つため影響しない。 */
  parse_toplevel_declarator_stmt(g_toplevel_decl_base_is_ptr, apply_toplevel_object_from_head);
}

static void parse_toplevel_declarator_stmt(int base_is_ptr,
                                           void (*apply)(toplevel_declarator_head_t)) {
  parse_toplevel_declarator_list_with_apply(base_is_ptr, apply);
  tk_expect(';');
}

static int parse_toplevel_declaration_like(void) {
  if (curtok()->kind == TK_STATIC_ASSERT) {
    parse_static_assert_toplevel();
    return 1;
  }
  if (psx_ctx_is_tag_keyword(curtok()->kind)) {
    // struct/union/enum 開始は ps_program() 側の専用経路で処理する。
    return 0;
  }
  if (is_toplevel_decl_like_start(curtok()) &&
      (!is_toplevel_function_signature(curtok()) ||
       toplevel_decl_has_comma_separated_declarators(curtok()))) {
    /* _Generic 用: 型シグネチャ文字列化のため型開始トークンを記録 (オブジェクト宣言のみ)。 */
    g_toplevel_typespec_start = (curtok()->kind == TK_TYPEDEF) ? NULL : curtok();
    parse_toplevel_decl_spec();
    parse_toplevel_decl_after_type();
    return 1;
  }
  return 0;
}

static int is_toplevel_decl_like_start(token_t *tok) {
  if (!tok) return 0;
  return tok->kind == TK_TYPEDEF ||
         psx_ctx_is_type_token(tok->kind) ||
         psx_is_decl_prefix_token(tok->kind) ||
         psx_ctx_is_typedef_name_token(tok);
}


static void install_toplevel_tag_decl_globals(token_kind_t tag_kind, char *tag_name, int tag_len) {
  g_toplevel_decl_tag_kind = tag_kind;
  g_toplevel_decl_tag_name = tag_name;
  g_toplevel_decl_tag_len = tag_len;
  g_toplevel_decl_base_kind = tag_kind;
  g_toplevel_decl_elem_size = psx_ctx_get_tag_size(tag_kind, tag_name, tag_len);
}

static void parse_toplevel_tag_decl(void) {
  /* この経路は宣言が tag キーワード (`struct`/`union`/`enum`) で始まる場合のみ (storage
   * class 前置があれば dispatcher が parse_toplevel_declaration_like へ回す)。dispatcher
   * (ps_next_function) はこの経路の前に reset_toplevel_decl_spec_state を呼ばないので、
   * 前の宣言の decl-spec 状態が g_toplevel_decl_* に残る。例えば直前が `typedef double T;`
   * だと g_toplevel_decl_fp_kind=DOUBLE が漏れ、ここで宣言する struct object の fp_kind が
   * DOUBLE になり、グローバル brace init の fp-fold 経路が文字列/関数参照/アドレス初期化子を
   * fp 定数(0)として食べてしまう (`struct{char b[4];char*p;} g={"x","y"}` の p が NULL 化)。
   * extern/static 漏れ (`extern struct S es; struct S es={7};` の 2 行目が extern 扱いされ
   * brace を取りこぼす) も同根。宣言ごとに全状態をクリアする。tag 情報は後段の
   * install_toplevel_tag_decl_globals が再設定する。 */
  reset_toplevel_decl_spec_state();
  token_kind_t tag_kind = TK_EOF;
  char *tag_name = NULL;
  int tag_len = 0;
  parse_toplevel_tag_head(&tag_kind, &tag_name, &tag_len);

  if (tk_consume('{')) {
    int member_count = 0;
    int tag_size = 0;
    member_count = psx_parse_tag_definition_body(tag_kind, tag_name, tag_len, &tag_size);
    psx_ctx_define_tag_type_with_layout(tag_kind, tag_name, tag_len, member_count, tag_size);
    if (tk_consume(';')) return;
    install_toplevel_tag_decl_globals(tag_kind, tag_name, tag_len);
    parse_toplevel_declarator_list();
    tk_expect(';');
    return;
  }
  if (tk_consume(';')) {
    psx_ctx_define_tag_type(tag_kind, tag_name, tag_len);
    return;
  }
  if (!psx_ctx_has_tag_type(tag_kind, tag_name, tag_len)) {
    psx_ctx_define_tag_type(tag_kind, tag_name, tag_len);
  }
  skip_post_type_cv_qualifiers();
  install_toplevel_tag_decl_globals(tag_kind, tag_name, tag_len);
  parse_toplevel_declarator_list();
  tk_expect(';');
}

static int g_last_type_unsigned = 0;
static int g_last_type_complex = 0;
/* 直近に解釈した型が `long long` か / plain `char` (signed/unsigned 無し) か。
 * _Generic は long と long long、char と signed/unsigned char を別型として扱う
 * (C11 6.2.5/6.7.2.1) ため、サイズだけでは区別できないこれらを side-channel で渡す。 */
static int g_last_type_long_long = 0;
static int g_last_type_plain_char = 0;
/* long double。ag_c は long double を double に lowering するが、_Generic は double と
 * long double を別型として扱う (C11 6.2.5) ため side-channel で区別する。 */
static int g_last_type_long_double = 0;
// g_last_type_atomic is defined above (before skip_cv_qualifiers)

int psx_last_type_is_unsigned(void) {
  return g_last_type_unsigned;
}

int psx_last_type_is_long_double(void) {
  return g_last_type_long_double;
}

int psx_last_type_is_complex(void) {
  return g_last_type_complex;
}

int psx_last_type_is_long_long(void) {
  return g_last_type_long_long;
}

int psx_last_type_is_plain_char(void) {
  return g_last_type_plain_char;
}

int psx_last_type_is_atomic(void) {
  return g_last_type_atomic;
}

static void emit_invalid_type_spec_diag(void) {
  diag_emit_tokf(DIAG_ERR_PARSER_INVALID_TYPE_SPEC, curtok(), "%s",
                 diag_message_for(DIAG_ERR_PARSER_INVALID_TYPE_SPEC));
}

// consume_type: 型キーワードがあれば読み進め、そのトークン種別を返す（0=型なし）
/* 後置 cv/atomic 修飾子トークンを 1 つ消費する。const/volatile/restrict/atomic
 * いずれも同じ「対応 flag を立てて trailing トークンを進める」パターンなので
 * 集約する。消費したら 1、該当しなければ 0 (呼出側で loop を抜ける)。 */
static int try_consume_post_cv_qualifier(token_kind_t k) {
  switch (k) {
    case TK_CONST:    g_last_type_const_qualified = 1; break;
    case TK_VOLATILE: g_last_type_volatile_qualified = 1; break;
    case TK_RESTRICT: break;
    case TK_ATOMIC:   g_last_type_atomic = 1; break;
    default: return 0;
  }
  set_curtok(curtok()->next);
  return 1;
}

/* saw_* flag 群から最終的な型 token_kind_t を決定する。
 * 優先度: void > float > double > bool > char > short > long > int。 */
static token_kind_t resolve_type_kind_from_flags(int saw_void, int saw_float, int saw_double,
                                                  int saw_bool, int saw_char, int saw_short,
                                                  int long_count) {
  if (saw_void) return TK_VOID;
  if (saw_float) return TK_FLOAT;
  if (saw_double) return TK_DOUBLE;
  if (saw_bool) return TK_BOOL;
  if (saw_char) return TK_CHAR;
  if (saw_short) return TK_SHORT;
  if (long_count > 0) return TK_LONG;
  return TK_INT;
}

token_kind_t psx_consume_type_kind(void) {
  g_last_type_unsigned = 0;
  g_last_type_complex = 0;
  g_last_type_long_long = 0;
  g_last_type_long_double = 0;
  g_last_type_plain_char = 0;
  g_last_type_atomic = 0;
  g_last_type_thread_local = 0;
  skip_cv_qualifiers();
  if (curtok()->kind == TK_ATOMIC && curtok()->next && curtok()->next->kind == TK_LPAREN) {
    g_last_type_atomic = 1;
    token_kind_t inner = parse_atomic_type_specifier();
    if (inner != TK_EOF) return inner;
  }
  // qualifier-form: _Atomic int x;
  if (curtok()->kind == TK_ATOMIC) {
    g_last_type_atomic = 1;
    set_curtok(curtok()->next);
  }
  token_t *start = curtok();
  int saw_signed = 0;
  int saw_unsigned = 0;
  int long_count = 0;
  int saw_short = 0;
  int saw_int = 0;
  int saw_char = 0;
  int saw_void = 0;
  int saw_float = 0;
  int saw_double = 0;
  int saw_bool = 0;
  int saw_complex = 0;
  int saw_imaginary = 0;

  while (true) {
    token_kind_t k = curtok()->kind;
    if (k == TK_COMPLEX) {
      if (saw_complex || saw_imaginary || saw_void || saw_char || saw_short || saw_int || saw_bool) {
        emit_invalid_type_spec_diag();
      }
      saw_complex = 1;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_IMAGINARY) {
      if (saw_complex || saw_imaginary || saw_void || saw_char || saw_short || saw_int || saw_bool) {
        emit_invalid_type_spec_diag();
      }
      saw_imaginary = 1;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_SIGNED) {
      if (saw_signed || saw_unsigned || saw_char || saw_short || long_count || saw_int || saw_void || saw_float || saw_double || saw_bool) {
        emit_invalid_type_spec_diag();
      }
      saw_signed = 1;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_UNSIGNED) {
      if (saw_signed || saw_unsigned || saw_char || saw_short || long_count || saw_int || saw_void || saw_float || saw_double || saw_bool) {
        emit_invalid_type_spec_diag();
      }
      saw_unsigned = 1;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_LONG) {
      if (saw_char || saw_short || saw_void || saw_float || saw_bool || long_count >= 2) {
        emit_invalid_type_spec_diag();
      }
      long_count++;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_SHORT) {
      if (saw_char || saw_short || long_count || saw_void || saw_float || saw_double || saw_bool) {
        emit_invalid_type_spec_diag();
      }
      saw_short = 1;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_INT) {
      if (saw_int || saw_char || saw_void || saw_float || saw_double || saw_bool) {
        emit_invalid_type_spec_diag();
      }
      saw_int = 1;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_CHAR) {
      if (saw_char || saw_short || long_count || saw_int || saw_void || saw_float || saw_double || saw_bool) {
        emit_invalid_type_spec_diag();
      }
      saw_char = 1;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_VOID) {
      if (saw_signed || saw_unsigned || saw_char || saw_short || long_count || saw_int || saw_float || saw_double || saw_bool) {
        emit_invalid_type_spec_diag();
      }
      saw_void = 1;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_FLOAT) {
      if (saw_signed || saw_unsigned || saw_char || saw_short || long_count || saw_int || saw_void || saw_double || saw_bool) {
        emit_invalid_type_spec_diag();
      }
      saw_float = 1;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_DOUBLE) {
      if (saw_signed || saw_unsigned || saw_char || saw_short || saw_int || saw_void || saw_float || saw_bool) {
        emit_invalid_type_spec_diag();
      }
      saw_double = 1;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_BOOL) {
      if (saw_signed || saw_unsigned || saw_char || saw_short || long_count || saw_int || saw_void || saw_float || saw_double) {
        emit_invalid_type_spec_diag();
      }
      saw_bool = 1;
      set_curtok(curtok()->next);
      continue;
    }
    // 後置 cv 修飾子（int const, volatile int const など）は同じ形なので集約。
    if (try_consume_post_cv_qualifier(k)) continue;
    /* C11 6.7p1: declaration-specifiers の順序は任意。型指定子の後ろに storage class
     * (static / extern / auto / register / inline / _Noreturn / _Thread_local / _Alignas) が
     * 来てもよい (`int static x = 5;` 等)。ここで遭遇したら skip_cv_qualifiers と同じ要領で
     * 1 つ消費して flag を立てループ継続。skip_cv_qualifiers を直接呼ぶと先頭で reset され
     * 既に立っている qualifier 情報 (const/volatile/atomic) を失うため、ここでは OR 的に
     * 1 トークンずつ処理する。 */
    if (psx_is_decl_prefix_token(k)) {
      /* storage class の重複・併用検出 (C11 6.7.1p2): static / extern / auto / register は
       * 高々 1 個。型指定子の前 (skip_cv_qualifiers) ですでに 1 つ立っていたら 2 つ目で error。 */
      int is_new_storage = (k == TK_STATIC || k == TK_EXTERN ||
                            k == TK_AUTO || k == TK_REGISTER);
      if (is_new_storage && (g_last_decl_is_static || g_last_decl_is_extern)) {
        psx_diag_ctx(curtok(), "decl",
                     "storage class 指定子は1つまでです (C11 6.7.1p2)");
      }
      if (k == TK_CONST)        g_last_type_const_qualified = 1;
      else if (k == TK_VOLATILE) g_last_type_volatile_qualified = 1;
      else if (k == TK_STATIC)   g_last_decl_is_static = 1;
      else if (k == TK_EXTERN)   g_last_decl_is_extern = 1;
      else if (k == TK_THREAD_LOCAL) g_last_type_thread_local = 1;
      else if (k == TK_ATOMIC) {
        /* `int _Atomic(int) x` 形式は ATOMIC 後に `(` が来る (型指定子)。型指定子の後の
         * 単独 `_Atomic` は qualifier 形 (`int _Atomic x`)。 */
        if (curtok()->next && curtok()->next->kind == TK_LPAREN) break;
        g_last_type_atomic = 1;
      }
      /* TK_AUTO / TK_REGISTER / TK_INLINE / TK_NORETURN / TK_ALIGNAS(...) は flag を立てずに
       * 単純消費。TK_ALIGNAS は `(value)` 形のため複雑だが、型指定子の後の出現は稀 (実例は
       * `int _Alignas(8) x` で C11 では基本的に typespec の前)。ここでは省略 — 必要ならば
       * 既存の skip_cv_qualifiers の TK_ALIGNAS 分岐を引用する。 */
      set_curtok(curtok()->next);
      continue;
    }
    break;
  }

  if (curtok() == start) return TK_EOF;
  g_last_type_unsigned = saw_unsigned;
  g_last_type_complex = saw_complex;
  g_last_type_long_long = (long_count >= 2) ? 1 : 0;
  g_last_type_plain_char = (saw_char && !saw_signed && !saw_unsigned) ? 1 : 0;
  g_last_type_long_double = (saw_double && long_count >= 1) ? 1 : 0;
  if ((saw_complex || saw_imaginary) && !(saw_float || saw_double)) {
    diag_emit_tokf(DIAG_ERR_PARSER_INVALID_CONTEXT, start,
                   "%s",
                   diag_message_for(DIAG_ERR_PARSER_COMPLEX_IMAGINARY_TYPE_REQUIRES_FLOAT));
  }
  return resolve_type_kind_from_flags(saw_void, saw_float, saw_double, saw_bool,
                                      saw_char, saw_short, long_count);
}


// _Alignas( constant-expression | type-name )
static void skip_balanced_group(token_kind_t lkind, token_kind_t rkind) {
  if (curtok()->kind != lkind) return;
  int depth = 0;
  while (curtok() && curtok()->kind != TK_EOF) {
    if (curtok()->kind == lkind) depth++;
    else if (curtok()->kind == rkind) {
      depth--;
      if (depth == 0) {
        set_curtok(curtok()->next);
        return;
      }
    }
    set_curtok(curtok()->next);
  }
  psx_diag_ctx(curtok(), "param", "%s",
               diag_message_for(DIAG_ERR_PARSER_MISSING_CLOSING_PAREN));
}

/* N-D VLA 仮引数 (`int t[n][m][k][l]` 等) 対応のため、内側 dim 全リストを保持する。
 * parse_param_declarator_name_recursive がセット、register_vla_array_param が読む。
 * 1 仮引数ごとに parse_param_declarator_name で初期化される。idx 0 が外側 dim。
 * 内側 dim は `inner_first_dim`/`inner_second_dim` と意味的に同じ (既存 API は 2D まで)。 */
static int g_param_inner_dim_consts[7] = {0};
static token_ident_t *g_param_inner_dim_idents[7] = {0};
static int g_param_inner_dim_count = 0;

static token_ident_t *parse_param_declarator_name(int *out_is_array_declarator, int *out_is_pointer_declarator,
                                                  int *out_pointer_levels,
                                                  int *out_inner_first_dim, int *out_inner_second_dim,
                                                  token_ident_t **out_inner_first_dim_ident,
                                                  token_ident_t **out_inner_second_dim_ident,
                                                  int *out_has_func_suffix) {
  if (out_is_array_declarator) *out_is_array_declarator = 0;
  if (out_is_pointer_declarator) *out_is_pointer_declarator = 0;
  if (out_pointer_levels) *out_pointer_levels = 0;
  if (out_inner_first_dim) *out_inner_first_dim = 0;
  if (out_inner_second_dim) *out_inner_second_dim = 0;
  if (out_inner_first_dim_ident) *out_inner_first_dim_ident = NULL;
  if (out_inner_second_dim_ident) *out_inner_second_dim_ident = NULL;
  if (out_has_func_suffix) *out_has_func_suffix = 0;
  /* N-D 用の inner dim 配列もクリア。 */
  for (int i = 0; i < 7; i++) {
    g_param_inner_dim_consts[i] = 0;
    g_param_inner_dim_idents[i] = NULL;
  }
  g_param_inner_dim_count = 0;
  token_ident_t *param = parse_param_declarator_name_recursive(out_is_array_declarator,
                                                               out_is_pointer_declarator,
                                                               out_pointer_levels,
                                                               out_inner_first_dim,
                                                               out_inner_second_dim,
                                                               out_inner_first_dim_ident,
                                                               out_inner_second_dim_ident,
                                                               out_has_func_suffix);
  return param;
}

/* `int (int x)` のように declarator の `(` 直後が型指定子なら、関数型の仮引数リスト。 */
static int is_param_decl_spec_start(void) {
  token_t *t = curtok();
  if (!t) return 0;
  if (psx_ctx_is_tag_keyword(t->kind)) return 1;
  if (psx_ctx_is_type_token(t->kind)) return 1;
  if (psx_ctx_is_typedef_name_token(t)) return 1;
  if (t->kind == TK_CONST || t->kind == TK_VOLATILE) return 1;
  return 0;
}

static token_ident_t *parse_param_declarator_name_recursive(int *out_is_array_declarator,
                                                            int *out_is_pointer_declarator,
                                                            int *out_pointer_levels,
                                                            int *out_inner_first_dim,
                                                            int *out_inner_second_dim,
                                                            token_ident_t **out_inner_first_dim_ident,
                                                            token_ident_t **out_inner_second_dim_ident,
                                                            int *out_has_func_suffix) {
  while (tk_consume('*')) {
    if (out_is_pointer_declarator) *out_is_pointer_declarator = 1;
    if (out_pointer_levels) (*out_pointer_levels)++;
    skip_ptr_qualifiers();
  }
  token_ident_t *name = NULL;
  // 括弧内に *p があるか (= 「ポインタを括弧で覆って配列にする」`(*p)[N]` 形式) を
  // 判定する。recursive 呼び出し前後で pointer level の変化を見れば判別できる。
  int levels_before_paren = out_pointer_levels ? *out_pointer_levels : 0;
  bool paren_made_pointer = false;
  if (tk_consume('(')) {
    if (is_param_decl_spec_start() || curtok()->kind == TK_VOID) {
      /* 関数型 declarator: `int (int x)` / `int (int())` / `int (void)` 等。
       * 仮引数位置では関数型は関数ポインタへ decay する。 */
      if (out_has_func_suffix) *out_has_func_suffix = 1;
      if (out_is_pointer_declarator) *out_is_pointer_declarator = 1;
      if (out_pointer_levels && *out_pointer_levels == 0) (*out_pointer_levels)++;
      node_func_t discard = {0};
      int discard_nargs = 0;
      int discard_cap = 0;
      while (curtok()->kind != TK_RPAREN) {
        parse_param_decl(&discard, &discard_nargs, &discard_cap, 0);
        if (!tk_consume(',')) break;
      }
      tk_expect(')');
    } else {
    name = parse_param_declarator_name_recursive(out_is_array_declarator, out_is_pointer_declarator,
                                                 out_pointer_levels,
                                                 out_inner_first_dim, out_inner_second_dim,
                                                 out_inner_first_dim_ident,
                                                 out_inner_second_dim_ident,
                                                 out_has_func_suffix);
    tk_expect(')');
    if (out_pointer_levels && *out_pointer_levels > levels_before_paren) {
      paren_made_pointer = true;
    }
    }
  } else {
    name = tk_consume_ident();
  }
  int bracket_count = 0;
  while (curtok()->kind == TK_LPAREN || curtok()->kind == TK_LBRACKET) {
    if (curtok()->kind == TK_LPAREN) {
      /* 関数 suffix `(...)`: `int (*ops[])(int)` の最後の `(int)` 等を skip。
       * 仮引数登録経路で「関数ポインタ配列」を識別するためフラグを立てる。 */
      if (out_has_func_suffix) *out_has_func_suffix = 1;
      skip_balanced_group(TK_LPAREN, TK_RPAREN);
    } else {
      if (out_is_array_declarator) *out_is_array_declarator = 1;
      // C11 6.7.6.3p7: 通常の仮引数 `int a[N][M]` では最も外側の `[N]` が
      // pointer 調整によりサイズが無関係になる。一方 `int (*a)[N][M]` は
      // ポインタが既に括弧内で適用されており、続く `[N][M]` は pointee の
      // dim を表すため最初の bracket も捕捉する。
      bool skip_first = (bracket_count == 0) && !paren_made_pointer;
      if (skip_first) {
        skip_balanced_group(TK_LBRACKET, TK_RBRACKET);
      } else {
        tk_consume('[');
        int dim = 0;
        token_ident_t *dim_ident = NULL;
        if (curtok() && curtok()->kind != TK_RBRACKET) {
          /* C99 6.7.6.3p7 VLA-as-param: `int g[n][m]` の内側 dim が単純な
           * パラメータ識別子のとき、constexpr 評価を試みずに識別子トークンを
           * 捕捉する。それ以外は従来の定数式評価へ。 */
          if (curtok()->kind == TK_IDENT &&
              curtok()->next && curtok()->next->kind == TK_RBRACKET) {
            dim_ident = (token_ident_t *)curtok();
            set_curtok(curtok()->next);
          } else {
            dim = psx_parse_array_size_constexpr();
          }
        }
        tk_expect(']');
        // paren_made_pointer 時は bracket 0/1/... が全て pointee dim。
        // 通常時は bracket 1/2/... が pointee dim。
        int dim_pos = paren_made_pointer ? bracket_count : (bracket_count - 1);
        if (dim_pos == 0) {
          if (out_inner_first_dim) *out_inner_first_dim = dim;
          if (out_inner_first_dim_ident) *out_inner_first_dim_ident = dim_ident;
        } else if (dim_pos == 1) {
          if (out_inner_second_dim) *out_inner_second_dim = dim;
          if (out_inner_second_dim_ident) *out_inner_second_dim_ident = dim_ident;
        }
        /* N-D 用 inner dim 配列にも記録 (最大 7 個まで)。 */
        if (dim_pos >= 0 && dim_pos < 7) {
          g_param_inner_dim_consts[dim_pos] = dim;
          g_param_inner_dim_idents[dim_pos] = dim_ident;
          if (dim_pos + 1 > g_param_inner_dim_count) g_param_inner_dim_count = dim_pos + 1;
        }
      }
      bracket_count++;
    }
  }
  return name;
}

static lvar_t *register_param_stride_slot(token_ident_t *param, int byte_size) {
  char name_buf[64];
  int name_len = snprintf(name_buf, sizeof(name_buf), "__rs_%.*s", param->len, param->str);
  char *name = arena_alloc((size_t)name_len + 1);
  memcpy(name, name_buf, (size_t)name_len);
  name[name_len] = '\0';
  return psx_decl_register_lvar_sized(name, name_len, byte_size, 8, 0);
}

static void apply_param_pointer_array_strides(lvar_t *var, int first_dim,
                                              int second_dim, int elem_size) {
  var->outer_stride = first_dim * elem_size;
  if (second_dim > 0) {
    var->outer_stride = first_dim * second_dim * elem_size;
    var->mid_stride = second_dim * elem_size;
  }
}

static int attach_param_runtime_stride(lvar_t *var, token_ident_t *param,
                                       token_ident_t *dim_ident, int elem_size) {
  lvar_t *src = psx_decl_find_lvar(dim_ident->str, dim_ident->len);
  if (!src || !src->is_param) {
    psx_diag_ctx(curtok(), "param",
                 diag_message_for(DIAG_ERR_PARSER_VLA_PARAM_DIM_NOT_PRECEDING_PARAM),
                 dim_ident->len, dim_ident->str);
    return 0;
  }
  lvar_t *rs = register_param_stride_slot(param, 8);
  var->is_vla = 1;
  var->vla_row_stride_frame_off = rs->offset;
  var->vla_row_stride_src_offset = src->offset;
  var->vla_row_stride_elem_size = (short)elem_size;
  return 1;
}

/* 仮引数 VLA / 多次元配列宣言子の lvar 登録 (`int a[n]` / `int a[][N]` /
 * `int a[][N][M]` / VLA dim that's another param 等)。
 * C11 6.7.6.3p7 により int *a として扱われるが、pointee が配列の場合は
 * outer_stride / mid_stride 等を立てて `a[i]` の steping を pointee 全体に揃える。 */
static lvar_t *register_vla_array_param(token_ident_t *param, param_decl_spec_t *ds,
                                         int inner_first_dim,
                                         token_ident_t *inner_first_dim_ident) {
  // size=8 (pointer), elem_size=実際の要素サイズ
  lvar_t *var = psx_decl_register_lvar_sized(param->str, param->len, 8, ds->elem_size, 0);
  /* 1D の fp 要素配列引数 (`double a[n]`): pointee の fp 種別を伝播。size==elem_size
   * (=8) で lvar_is_pointer の size>elem_size 判定に漏れる double 要素でも、これで
   * ポインタ認識され subscript が fp load になる (int a[n] は size>elem_size で OK)。 */
  if (inner_first_dim == 0 && ds->fp_kind != TK_FLOAT_KIND_NONE) {
    var->pointee_fp_kind = ds->fp_kind;
  }
  /* 全 inner dim が定数 (N-D const inner、`int t[][2][3][4]` 等): outer/mid/extra strides を立てる。
   * 3D 以下は既存挙動を維持。4D 以上は N-D 一般経路 (下記) で extra_strides まで設定する。 */
  int all_inner_const = (g_param_inner_dim_count >= 1);
  for (int i = 0; i < g_param_inner_dim_count; i++) {
    if (g_param_inner_dim_consts[i] <= 0) { all_inner_const = 0; break; }
  }
  if (inner_first_dim > 0 && all_inner_const) {
    var->base_deref_size = (short)ds->elem_size;
    /* level k の stride = dim[k+1]*...*dim[N-1]*elem。outer_stride = level 0、
     * mid_stride = level 1、extra_strides[i] = level (i+2)。 */
    int prod_all = 1;
    for (int i = 0; i < g_param_inner_dim_count; i++) prod_all *= g_param_inner_dim_consts[i];
    var->outer_stride = prod_all * ds->elem_size;
    if (g_param_inner_dim_count >= 2) {
      int prod_mid = 1;
      for (int i = 1; i < g_param_inner_dim_count; i++) prod_mid *= g_param_inner_dim_consts[i];
      var->mid_stride = prod_mid * ds->elem_size;
    }
    if (g_param_inner_dim_count >= 3) {
      var->extra_strides = calloc(5, sizeof(int));
      int idx_ex = 0;
      for (int start = 2; start < g_param_inner_dim_count && idx_ex < 5; start++) {
        int rest = 1;
        for (int j = start; j < g_param_inner_dim_count; j++) rest *= g_param_inner_dim_consts[j];
        var->extra_strides[idx_ex++] = rest * ds->elem_size;
      }
      var->extra_strides_count = (unsigned char)idx_ex;
    }
    return var;
  }
  /* N-D VLA 仮引数 (内側 dim に runtime / 混在 const-VLA 含む)。stride スロットを (N-1)*8 バイト
   * 確保し、関数 entry で emit_vla_row_stride_for_params が各 level の stride を計算・store する。
   * 内側 dim 情報を lvar に保存して emit が読めるようにする。 */
  if (g_param_inner_dim_count >= 1) {
    int n_inner = g_param_inner_dim_count;
    /* stride スロット (__rs_<param>) を 8*n_inner バイト確保。先頭 8 バイトが level 0 (vla_row)、
     * 次が level 1、...、最後が level n_inner-1。subscript chain は vla_row を +=8 シフト。 */
    lvar_t *rs = register_param_stride_slot(param, 8 * n_inner);
    var->is_vla = 1;
    var->vla_row_stride_frame_off = rs->offset;
    var->vla_strides_remaining = n_inner - 1;
    var->base_deref_size = (short)ds->elem_size;
    var->vla_row_stride_elem_size = (short)ds->elem_size;
    /* 内側 dim 情報を保存。emit_vla_row_stride_for_params がこれを読んで stride を計算する。 */
    var->vla_param_inner_dim_count = (unsigned char)n_inner;
    for (int i = 0; i < n_inner; i++) {
      var->vla_param_inner_dim_consts[i] = (short)g_param_inner_dim_consts[i];
      if (g_param_inner_dim_consts[i] <= 0 && g_param_inner_dim_idents[i]) {
        lvar_t *src = psx_decl_find_lvar(g_param_inner_dim_idents[i]->str,
                                         g_param_inner_dim_idents[i]->len);
        if (!src || !src->is_param) {
          psx_diag_ctx(curtok(), "param",
                       diag_message_for(DIAG_ERR_PARSER_VLA_PARAM_DIM_NOT_PRECEDING_PARAM),
                       g_param_inner_dim_idents[i]->len, g_param_inner_dim_idents[i]->str);
          return var;
        }
        var->vla_param_inner_dim_src_offsets[i] = src->offset;
      } else {
        var->vla_param_inner_dim_src_offsets[i] = 0;  /* const dim */
      }
    }
    /* 2D 後方互換: 旧 vla_row_stride_src_offset も先頭 runtime dim から立てる
     * (既存 emit が 2D ではこれを参照する。N-D 経路では使わない)。 */
    if (n_inner == 1 && g_param_inner_dim_consts[0] == 0) {
      var->vla_row_stride_src_offset = var->vla_param_inner_dim_src_offsets[0];
    }
    return var;
  }
  if (!inner_first_dim_ident) return var;
  /* C99 6.7.6.3p7 VLA-as-param: `int g[n][m]` の内側 dim が他のパラメータ。
   * row stride スロットを確保し、関数 entry で
   *   *[rs_slot] = *[src_param] * elem_size
   * を計算する。これにより subscript の vla_rsf 経路 (expr.c) が
   * runtime stride を読んで `g[i]` を正しく steping できる。 */
  attach_param_runtime_stride(var, param, inner_first_dim_ident, ds->elem_size);
  var->base_deref_size = (short)ds->elem_size;
  return var;
}

/* `typedef int M[2][3][4]; M *p` のように pointee が typedef した配列型の
 * 仮引数で、(*p)[i][j][k] の各サブスクリプト stride を設定する:
 *   var->outer_stride       = sizeof(M) = D0*D1*..*elem  (p[i] のステップ)
 *   var->mid_stride         = D1*..*elem                 ((*p)[j] のステップ)
 *   var->extra_strides[0..] = D2*..*elem, D3*..*elem, ...
 * build_unary_deref_node が *p で 1 段スライドして復元する。 */
static void apply_typedef_array_pointee_strides(lvar_t *var, param_decl_spec_t *ds) {
  var->outer_stride = ds->typedef_sizeof_size;
  if (ds->typedef_array_first_dim > 0) {
    int second_dim_bytes = ds->typedef_sizeof_size / ds->typedef_array_first_dim;
    if (second_dim_bytes > 0 && second_dim_bytes != ds->elem_size) {
      var->mid_stride = second_dim_bytes;
    }
  }
  if (ds->typedef_array_dim_count < 3) return;
  // mid = D1 以降の積 * elem
  int mid_mul = 1;
  for (int i = 1; i < ds->typedef_array_dim_count; i++) {
    if (ds->typedef_array_dims[i] > 0) mid_mul *= ds->typedef_array_dims[i];
  }
  if (mid_mul > 0) var->mid_stride = mid_mul * ds->elem_size;
  // extra_strides[k] = D(k+2) 以降の積 * elem
  if (ds->typedef_array_dim_count >= 3) var->extra_strides = calloc(5, sizeof(int));
  int idx_in_extras = 0;
  for (int start = 2; start < ds->typedef_array_dim_count && idx_in_extras < 5; start++) {
    int rest_mul = 1;
    for (int j = start; j < ds->typedef_array_dim_count; j++) {
      if (ds->typedef_array_dims[j] > 0) rest_mul *= ds->typedef_array_dims[j];
    }
    var->extra_strides[idx_in_extras++] = rest_mul * ds->elem_size;
  }
  var->extra_strides_count = (unsigned char)idx_in_extras;
}

/* 仮引数宣言子の形式 (funcptr-array / VLA / struct array / >16B byref struct /
 * ≤16B struct value / struct pointer / scalar pointer / typedef-array decay /
 * plain scalar) に応じて lvar_t を登録する。
 * 各分岐は parse_param_declarator_name の出力 (is_ptr / is_array_declarator /
 * inner dims / func_suffix etc) と param_decl_spec_t を使って判別する。 */
static lvar_t *register_param_lvar(token_ident_t *param, const param_decl_spec_t *ds_in,
                                    int param_is_ptr, int param_is_array_declarator,
                                    int param_ptr_levels, int param_has_func_suffix,
                                    int param_inner_first_dim, int param_inner_second_dim,
                                    token_ident_t *param_inner_first_dim_ident) {
  /* register_vla_array_param / apply_typedef_array_pointee_strides は ds を非const
   * の param_decl_spec_t * で受け取るため、内部的にキャストする。値は変更しない。 */
  param_decl_spec_t *ds = (param_decl_spec_t *)ds_in;
  if (param_is_array_declarator && param_is_ptr && param_has_func_suffix &&
      ds->tag_kind == TK_EOF) {
    /* `int (*ops[])(int)` 形式の関数ポインタ配列パラメータ。
     * C11 6.7.6.3p7 で配列 → ポインタへ adjust される (= `int (**ops)(int)` 相当)。
     * 各要素は関数ポインタ (8 byte) なので elem_size=8 で登録。
     * pointer_qual_levels=1 で lvar_is_pointer (expr.c) を発火させ、subscript 経路を動かす。 */
    lvar_t *var = psx_decl_register_lvar_sized_align(param->str, param->len, 8, 8, 0, 0);
    var->is_tag_pointer = 0;
    var->base_deref_size = 8;
    var->pointer_qual_levels = 1;
    return var;
  }
  if (param_is_array_declarator && ds->tag_kind == TK_EOF && !param_is_ptr) {
    /* 仮引数 VLA / 多次元配列宣言子 (`int a[n]` / `int a[][N]` 等)。
     * C11 6.7.6.3p7 により pointer (or pointer-to-array) に adjust される。 */
    return register_vla_array_param(param, ds, param_inner_first_dim,
                                     param_inner_first_dim_ident);
  }
  if (param_is_array_declarator && ds->tag_kind != TK_EOF && !param_is_ptr) {
    /* struct/union 配列パラメータ `struct V arr[]` は C11 6.7.6.3p7 で
     * `struct V *arr` に adjust される。tag_kind を保持しつつ pointer 扱い。 */
    lvar_t *var = psx_decl_register_lvar_sized_align(param->str, param->len, 8, ds->struct_size, 0, 0);
    psx_decl_set_var_tag(var, ds->tag_kind, ds->tag_name, ds->tag_len, 1);
    var->base_deref_size = (short)ds->struct_size;
    return var;
  }
  if (ds->tag_kind != TK_EOF && !param_is_ptr && ds->struct_size > 16) {
    // >16バイト構造体の値渡し → ABI: アドレス渡し（byref）
    lvar_t *var = psx_decl_register_lvar_sized_align(param->str, param->len, 8, ds->struct_size, 0, 0);
    psx_decl_set_var_tag(var, ds->tag_kind, ds->tag_name, ds->tag_len, 0);
    var->is_byref_param = 1;
    return var;
  }
  if (ds->tag_kind != TK_EOF && !param_is_ptr && ds->struct_size > 0) {
    // ≤16バイト構造体の値渡し → ABI: レジスタ渡し（1 or 2レジスタ）
    lvar_t *var = psx_decl_register_lvar_sized_align(param->str, param->len, ds->struct_size, ds->struct_size, 0, 8);
    psx_decl_set_var_tag(var, ds->tag_kind, ds->tag_name, ds->tag_len, 0);
    return var;
  }
  if (ds->tag_kind != TK_EOF && param_is_ptr) {
    /* struct/union へのポインタ仮引数。`a[i]` / `a+i` のスケーリングに pointee
     * (= struct サイズ) が必要なので deref_size に struct_size を入れる。多段
     * ポインタ (`struct N **a`) の pointee はポインタ (8) なので除外する。
     * 修正前は常に 8 で、4 バイト構造体の subscript が誤スケールしていた。 */
    int pointee = (param_ptr_levels <= 1 && ds->struct_size > 0) ? ds->struct_size : 8;
    lvar_t *var = psx_decl_register_lvar_sized_align(param->str, param->len, 8, pointee, 0, 0);
    psx_decl_set_var_tag(var, ds->tag_kind, ds->tag_name, ds->tag_len, 1);
    var->base_deref_size = (short)pointee;
    /* 多段の struct ポインタ仮引数 (`struct N **root`): pointer_qual_levels を
     * declarator の段数で立てる。これがないと build_unary_deref_node の `*root` で
     * is_tag_pointer 伝播が pql>=2 を要求して 0 にクリアしてしまい、続く
     * `(*root)->m` が E3005 になる。単段 struct ポインタは従来どおり pql 未設定。 */
    if (param_ptr_levels >= 2) {
      var->pointer_qual_levels = param_ptr_levels;
    }
    /* `struct V (*p)[N]` 配列へのポインタ仮引数: 上の is_tag_pointer=1 のままだと
     * 1 要素 (struct サイズ) ずつしか進まず行を跨げない。outer_stride を 1 行 (N 要素)
     * に設定し is_tag_pointer をクリアして、ローカルの `struct V (*p)[N]` と同じ
     * 「配列へのポインタ」表現にする (`p[i][j].m` が正しくスケールする)。 */
    if (param_is_array_declarator && param_inner_first_dim > 0 && ds->struct_size > 0) {
      var->is_tag_pointer = 0;
      var->base_deref_size = (short)ds->struct_size;
      apply_param_pointer_array_strides(var, param_inner_first_dim,
                                        param_inner_second_dim,
                                        ds->struct_size);
    }
    return var;
  }
  if (param_is_ptr && ds->tag_kind == TK_EOF) {
    /* スカラー型へのポインタ仮引数（int *p, char *p, int **pp など）。
     * 多段ポインタなら pointee_size=8、それ以外は ds->elem_size。 */
    int pointee_size = (param_ptr_levels >= 2) ? 8 : ds->elem_size;
    lvar_t *var = psx_decl_register_lvar_sized(param->str, param->len, 8, pointee_size, 0);
    var->base_deref_size = (short)ds->elem_size;
    /* `unsigned char *p` / `unsigned short *p` 等: pointee が unsigned なら is_unsigned を
     * 立てる。build_lvar_or_vla_node が pointee_is_unsigned へ伝播し、`*p` / `p[i]` が
     * zero-extend load (ldrb/ldrh) になる。未設定だと符号拡張で値が化けていた。 */
    var->is_unsigned = ds->is_unsigned ? 1 : 0;
    /* `long *a` / `unsigned long *a` / scalar `T **a` のように pointee が 8 バイトの
     * ポインタ仮引数は size==elem_size==8 となり、lvar_is_pointer の size>elem_size
     * 判定に漏れる。pointer_qual_levels を立ててポインタと認識させ subscript `a[i]`
     * を通す。
     * 注意: int* など pointee<8 では size>elem_size 判定が既に効いておりポインタ
     * 認識されている。そこへ pql を立てると subscript の結果型が誤って pointer 化し
     * `p[i]` が壊れる (arr_as_ptr 回帰)。よって pointee_size>=8 のときだけ立てる。
     * fp 単段ポインタ (`double *a`) は pointee_fp_kind 経路で処理済みなので除外。 */
    if (pointee_size >= 8 && !(param_ptr_levels == 1 && ds->fp_kind != TK_FLOAT_KIND_NONE) &&
        /* 配列へのポインタ `T (*p)[N]` (pointee が配列) は除外。pql を立てると
         * subscript が単段ポインタ (T*) 扱いになり outer_stride を無視して 1 要素
         * 分しか進まない。要素 struct が 8B 以上のときだけ pointee_size>=8 に該当し
         * 壊れていた (int(*)[N] は pointee<8 で元から pql 非設定)。 */
        !(param_is_array_declarator && param_inner_first_dim > 0)) {
      var->pointer_qual_levels = param_ptr_levels;
    }
    /* `double *a` / `float *a` の単段ポインタ仮引数: pointee の fp 種別を伝播し、
     * `*a` / `a[i]` が fp load/store になるようにする (未設定だと整数 load + scvtf に
     * なって値が壊れていた)。 */
    var->pointee_fp_kind = (param_ptr_levels == 1) ? ds->fp_kind : TK_FLOAT_KIND_NONE;
    /* `int (*a)[N]` / `int (*a)[N][M]` のように pointee が配列の場合、
     * captured inner dims を使って outer_stride / mid_stride を設定する。 */
    if (param_is_array_declarator && param_inner_first_dim > 0) {
      apply_param_pointer_array_strides(var, param_inner_first_dim,
                                        param_inner_second_dim,
                                        ds->elem_size);
    } else if (param_is_array_declarator && param_inner_first_dim_ident != NULL) {
      /* pointer-to-VLA 仮引数 `int (*a)[n]` (n は先行パラメータ)。行ストライド n*elem は
       * 実行時値なので、row stride スロットを確保し関数 entry で *[rs] = *[n]*elem を計算する
       * (register_vla_array_param と同じ機構を再利用)。subscript は vla_row_stride_frame_off を
       * 実行時参照する。outer_stride は 0 のまま (実行時経路)。 */
      attach_param_runtime_stride(var, param, param_inner_first_dim_ident, ds->elem_size);
    } else if (param_ptr_levels == 1 && ds->typedef_is_array && ds->typedef_sizeof_size > 0) {
      /* `typedef int row_t[N]; row_t *a` / 多次元版 (`typedef int M[2][3][4]; M *p`)。 */
      apply_typedef_array_pointee_strides(var, ds);
    }
    return var;
  }
  if (!param_is_ptr && ds->tag_kind == TK_EOF && ds->typedef_is_array &&
      ds->typedef_sizeof_size > 0) {
    /* `typedef int Vec3[3]; int sum(Vec3 v)` の仮引数:
     * C11 6.7.6.3p7 により配列型は pointer に adjust される (decay 先頭要素ポインタ)。 */
    lvar_t *var = psx_decl_register_lvar_sized(param->str, param->len, 8, ds->elem_size, 0);
    var->base_deref_size = (short)ds->elem_size;
    return var;
  }
  if (!param_is_ptr && ds->tag_kind == TK_EOF && ds->is_complex) {
    /* `double _Complex` / `float _Complex` 仮引数: {re,im} を持つので 2*half バイトで
     * 登録する (8 のままだと double complex で次の仮引数スロットと重なり im が壊れる)。 */
    int half = (ds->fp_kind == TK_FLOAT_KIND_FLOAT) ? 4 : 8;
    lvar_t *var = psx_decl_register_lvar_sized_align(param->str, param->len,
                                                     2 * half, 2 * half, 0, 8);
    var->is_complex = 1;
    var->fp_kind = ds->fp_kind;
    return var;
  }
  // スカラー型仮引数（既存の動作）
  return psx_decl_register_lvar(param->str, param->len);
}

static int parse_param_decl(node_func_t *node, int *nargs, int *arg_cap, int count_unnamed) {
  param_decl_spec_t ds = {0};
  parse_param_decl_spec(&ds);
  // ポインタ修飾子を確認してから parse_param_declarator_name へ
  int param_is_ptr = 0;
  int param_is_array_declarator = 0;
  int param_ptr_levels = 0;
  int param_inner_first_dim = 0;
  int param_inner_second_dim = 0;
  token_ident_t *param_inner_first_dim_ident = NULL;
  token_ident_t *param_inner_second_dim_ident = NULL;
  int param_has_func_suffix = 0;
  token_ident_t *param = parse_param_declarator_name(&param_is_array_declarator, &param_is_ptr,
                                                     &param_ptr_levels,
                                                     &param_inner_first_dim,
                                                     &param_inner_second_dim,
                                                     &param_inner_first_dim_ident,
                                                     &param_inner_second_dim_ident,
                                                     &param_has_func_suffix);
  /* ポインタ typedef 基底 (`typedef char* Str; f(Str s)`) を実効ポインタ性へ合成する。
   * 宣言子に `*` が無く (param_is_ptr=0) 配列宣言子でもないときのみ、基底の段数を採用する
   * (`Str *p` のように宣言子側にも `*` がある場合は param_ptr_levels に基底段数を足す)。
   * 配列宣言子 `Str a[]` は C11 6.7.6.3p7 の adjust が別経路で効くので触らない。 */
  if (ds.base_is_pointer && !param_is_array_declarator) {
    param_is_ptr = 1;
    param_ptr_levels += ds.base_pointer_levels;
  }
  if (!param) {
    // int f(void) の "void" は仮引数0件として扱う（C11 6.7.6.3）。
    if (ds.base_type_kind == TK_VOID && ds.tag_kind == TK_EOF && !ds.saw_typedef_name &&
        !param_is_ptr && !param_is_array_declarator) {
      return 0;
    }
    // decl-specifier はあるが識別子が無い仮引数（例: int f(int);）は
    // プロトタイプでは許容し、関数定義時のみ呼び出し元で診断する。
    if (ds.base_type_kind != TK_EOF || ds.tag_kind != TK_EOF || ds.saw_typedef_name) {
      /* count_unnamed のとき、無名でも固定引数として nargs に数える。これがないと
       * 可変長プロトタイプ (`int printf(const char*, ...)` のように引数名を省く一般的な
       * 書き方) で固定引数数が 0 と誤算され、可変長呼び出し ABI で format 等までスタックに
       * 積まれ x0 が未設定になって crash していた (Apple ARM64)。args[] には fp_kind を持つ
       * プレースホルダを置き、固定 fp 引数の ABI 情報も index 整合させて保つ。プロトタイプ
       * 専用 (定義で無名引数は下流で診断) なので args[] のプレースホルダは codegen に出ない。
       * count_unnamed=0 は入れ子宣言子 (`int (*(*f(void))(int))[3]` の内側 `(int)` 等) で、
       * これらは関数 f 自身の引数ではないため f の nargs に数えてはならない。 */
      if (count_unnamed) {
        if (*nargs >= *arg_cap) {
          *arg_cap = pda_next_cap(*arg_cap, *nargs + 1);
          node->args = pda_xreallocarray(node->args, (size_t)(*arg_cap), sizeof(node_t *));
        }
        node_t *ph = psx_node_new_num(0);
        if (ds.fp_kind != TK_FLOAT_KIND_NONE && !param_is_ptr && !param_is_array_declarator) {
          ph->fp_kind = ds.fp_kind;
        }
        node->args[(*nargs)++] = ph;
      }
      return 1;
    }
    psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_PARAMETER));
  }

  if (*nargs >= *arg_cap) {
    *arg_cap = pda_next_cap(*arg_cap, *nargs + 1);
    node->args = pda_xreallocarray(node->args, (size_t)(*arg_cap), sizeof(node_t *));
  }
  lvar_t *var = register_param_lvar(param, &ds,
                                     param_is_ptr, param_is_array_declarator,
                                     param_ptr_levels, param_has_func_suffix,
                                     param_inner_first_dim, param_inner_second_dim,
                                     param_inner_first_dim_ident);
  var->is_param = 1;
  var->is_initialized = 1;
  // float/double 仮引数は ABI に従い d0..d7 で受け取るため fp_kind を保持。
  // ただし配列宣言子 (`double a[n]`) はポインタへ adjust され整数レジスタ渡しに
  // なるので fp_kind は付けない (付けると d レジスタ受けになり ABI が壊れる)。
  if (ds.fp_kind != TK_FLOAT_KIND_NONE && !param_is_ptr && !param_is_array_declarator) {
    var->fp_kind = ds.fp_kind;
  }
  // args[] には「ABIサイズ」を type_size に持つ ND_LVAR を格納
  // codegen がレジスタ数（1 or 2）を判断するため
  // 配列宣言子の struct パラメータ (`struct V arr[]`) はポインタに adjust される
  // ので、ABI サイズは 8 (pointer) であり struct_size ではない。
  int abi_type_size = (ds.tag_kind != TK_EOF && !param_is_ptr && ds.struct_size > 0 &&
                       !param_is_array_declarator)
                      ? ds.struct_size : 8;
  node_t *param_node = psx_node_new_lvar_typed(var->offset, abi_type_size);
  // codegen 側で `str d_reg` (FP) と `str x_reg` (integer) を切り替えるために
  // args[i] ノードにも fp_kind を残す。配列宣言子はポインタ (整数レジスタ) なので除外。
  if (ds.fp_kind != TK_FLOAT_KIND_NONE && !param_is_ptr && !param_is_array_declarator) {
    param_node->fp_kind = ds.fp_kind;
  }
  /* 複素数仮引数: args[] ノードにも is_complex を残す (setup_function_params は
   * owner->is_complex を見るが、念のため両方に伝播)。 */
  if (ds.is_complex && !param_is_ptr && !param_is_array_declarator) {
    param_node->is_complex = 1;
    ((node_lvar_t *)param_node)->mem.is_complex = 1;
  }
  node->args[(*nargs)++] = param_node;
  return 0;
}

static void parse_param_decl_spec(param_decl_spec_t *out) {
  out->base_type_kind = TK_EOF;
  out->saw_typedef_name = 0;
  out->tag_kind = TK_EOF;
  out->tag_name = NULL;
  out->tag_len = 0;
  out->struct_size = 0;
  out->elem_size = 8;

  // 仮引数の型解析（struct/union の値渡し/ポインタ渡しを含む）
  skip_cv_qualifiers();
  if (parse_param_tag_decl_spec(out)) {
    return;
  }

  // スカラー型: 仮引数配列宣言子のelemサイズ取得のため型を明示消費
  parse_param_scalar_decl_spec(out);
}

static int parse_param_tag_decl_spec(param_decl_spec_t *out) {
  if (!psx_ctx_is_tag_keyword(curtok()->kind)) return 0;
  out->tag_kind = curtok()->kind;
  set_curtok(curtok()->next);
  token_ident_t *tag_ident = tk_consume_ident();
  if (tag_ident) {
    out->tag_name = tag_ident->str;
    out->tag_len = tag_ident->len;
    if (psx_ctx_has_tag_type(out->tag_kind, out->tag_name, out->tag_len)) {
      out->struct_size = psx_ctx_get_tag_size(out->tag_kind, out->tag_name, out->tag_len);
    }
  }
  return 1;
}

static void parse_param_scalar_decl_spec(param_decl_spec_t *out) {
  skip_cv_qualifiers();
  token_kind_t param_type_kind = psx_consume_type_kind();
  if (param_type_kind != TK_EOF) {
    out->base_type_kind = param_type_kind;
    out->is_unsigned = (param_type_kind == TK_UNSIGNED) || psx_last_type_is_unsigned();
    psx_ctx_get_type_info(param_type_kind, NULL, &out->elem_size);
    if (param_type_kind == TK_FLOAT) out->fp_kind = TK_FLOAT_KIND_FLOAT;
    else if (param_type_kind == TK_DOUBLE) out->fp_kind = TK_FLOAT_KIND_DOUBLE;
    /* `double _Complex z` 等: psx_consume_type_kind が _Complex も消費し
     * g_last_type_complex を立てる。HFA 受け取りのため記録する。 */
    out->is_complex = psx_last_type_is_complex();
  } else if (psx_ctx_is_typedef_name_token(curtok())) {
    out->saw_typedef_name = 1;
    // typedef 名の情報を仮引数解析に伝える。特に「配列型 typedef」が
    // `typedef_name *a` の形でポインタ仮引数になるとき、配列の総バイト数を
    // outer_stride として使うため。
    token_ident_t *id = (token_ident_t *)curtok();
    int td_elem_size = 0;
    int td_is_array = 0;
    int td_sizeof_size = 0;
    int td_first_dim = 0;
    tk_float_kind_t td_fp_kind = TK_FLOAT_KIND_NONE;
    int td_dim_count = 0;
    token_kind_t td_tag_kind = TK_EOF;
    char *td_tag_name = NULL;
    int td_tag_len = 0;
    psx_typedef_info_t _ti;
    if (psx_ctx_find_typedef_name(id->str, id->len, &_ti)) {
      td_elem_size = _ti.elem_size;
      td_fp_kind = _ti.fp_kind;
      td_tag_kind = _ti.tag_kind;
      td_tag_name = _ti.tag_name;
      td_tag_len = _ti.tag_len;
      td_is_array = _ti.is_array;
      td_sizeof_size = _ti.sizeof_size;
      td_first_dim = _ti.array_first_dim;
      td_dim_count = _ti.array_dim_count;
      for (int i = 0; i < td_dim_count && i < 8; i++) out->typedef_array_dims[i] = _ti.array_dims[i];
      if (td_elem_size > 0) out->elem_size = td_elem_size;
      out->typedef_is_array = td_is_array;
      out->typedef_sizeof_size = td_sizeof_size;
      out->typedef_array_first_dim = td_first_dim;
      out->typedef_array_dim_count = td_dim_count;
      if (td_fp_kind != TK_FLOAT_KIND_NONE) out->fp_kind = td_fp_kind;
      /* struct/union typedef (`typedef struct {...} T; T *t`) のタグを伝播し、
       * `t->m` のメンバアクセスと subscript スケーリングを解決できるようにする。 */
      if (td_tag_kind == TK_STRUCT || td_tag_kind == TK_UNION) {
        out->tag_kind = td_tag_kind;
        out->tag_name = td_tag_name;
        out->tag_len = td_tag_len;
        int ts = psx_ctx_get_tag_size(td_tag_kind, td_tag_name, td_tag_len);
        if (ts > 0) out->struct_size = ts;
      }
      /* ポインタ typedef (`typedef char* Str; f(Str s)`): 基底のポインタ性を捕捉し、
       * 仮引数を非配列・宣言子に `*` が無くてもポインタとして登録できるようにする。
       * 未捕捉だと `s` がスカラ登録され `s[i]` が E3064 (subscript 不可) になっていた。
       * elem_size は typedef 解決で pointee サイズ (char=1 等) に設定済み。 */
      if (_ti.is_pointer) {
        out->base_is_pointer = 1;
        int lv = psx_ctx_get_typedef_pointer_levels(id->str, id->len);
        out->base_pointer_levels = (lv > 0) ? lv : 1;
      }
      if (_ti.is_unsigned) out->is_unsigned = 1;
    }
    set_curtok(curtok()->next);
  }
}

static void parse_func_decl_spec(token_kind_t *ret_kind, tk_float_kind_t *ret_fp_kind,
                                 token_ident_t **ret_tag, int *ret_is_ptr,
                                 int *ret_is_unsigned) {
  *ret_kind = TK_EOF;
  *ret_fp_kind = TK_FLOAT_KIND_NONE;
  *ret_tag = NULL;
  *ret_is_ptr = 0;
  if (ret_is_unsigned) *ret_is_unsigned = 0;
  g_func_ret_is_funcptr = 0;
  g_func_funcptr_ret_is_pointer = 0;
  /* storage class (static/extern) の直後がタグキーワードなら、先に storage class を消費
   * してフラグを立ててからタグ経路へ。psx_consume_type_kind は `static` の後の `struct` を
   * 型と認識できず implicit int に落ちるため (`static struct S *g(){}` が壊れていた)。
   * 後ろがタグでないとき (builtin/typedef) は従来どおり psx_consume_type_kind に任せる。 */
  {
    token_t *t = curtok();
    while (t && (t->kind == TK_STATIC || t->kind == TK_EXTERN ||
                 t->kind == TK_CONST || t->kind == TK_VOLATILE)) t = t->next;
    if (t && t != curtok() && psx_ctx_is_tag_keyword(t->kind)) {
      while (curtok()->kind == TK_STATIC || curtok()->kind == TK_EXTERN ||
             curtok()->kind == TK_CONST || curtok()->kind == TK_VOLATILE) {
        if (curtok()->kind == TK_STATIC) g_last_decl_is_static = 1;
        if (curtok()->kind == TK_EXTERN) g_last_decl_is_extern = 1;
        if (curtok()->kind == TK_CONST) g_last_type_const_qualified = 1;
        if (curtok()->kind == TK_VOLATILE) g_last_type_volatile_qualified = 1;
        set_curtok(curtok()->next);
      }
    }
  }
  if (psx_ctx_is_tag_keyword(curtok()->kind)) {
    resolve_func_ret_tag_spec(ret_kind, ret_tag);
    parse_pointer_suffix_flags(ret_is_ptr); // skip optional pointer(s)
    return;
  }

  *ret_kind = psx_consume_type_kind(); // 通常の戻り値型（省略可）
  if (*ret_kind == TK_EOF && psx_ctx_is_typedef_name_token(curtok())) {
    resolve_func_ret_typedef(ret_kind, ret_fp_kind, ret_tag, ret_is_ptr, ret_is_unsigned);
  }
  *ret_fp_kind = fp_kind_for_type_kind_toplevel(*ret_kind);
  parse_pointer_suffix_flags(ret_is_ptr);
}

static void resolve_func_ret_tag_spec(token_kind_t *ret_kind, token_ident_t **ret_tag) {
  *ret_kind = curtok()->kind;
  set_curtok(curtok()->next);
  psx_skip_gnu_attributes();
  token_ident_t *tag = tk_consume_ident();
  if (!tag && curtok()->kind != TK_LBRACE) {
    psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_TAG_NAME));
  }
  if (!tag) {
    char *anon_name = NULL;
    int anon_len = 0;
    psx_make_anonymous_tag_name(&anon_name, &anon_len);
    tag = calloc(1, sizeof(token_ident_t));
    tag->str = anon_name;
    tag->len = anon_len;
  }
  *ret_tag = tag;
  if (tk_consume('{')) {
    int member_count = 0;
    int tag_size = 0;
    member_count = psx_parse_tag_definition_body(*ret_kind, tag->str, tag->len, &tag_size);
    psx_ctx_define_tag_type_with_layout(*ret_kind, tag->str, tag->len, member_count, tag_size);
  } else if (!psx_ctx_has_tag_type(*ret_kind, tag->str, tag->len)) {
    psx_ctx_define_tag_type(*ret_kind, tag->str, tag->len);
  }
}

static void parse_pointer_suffix_flags(int *out_is_ptr) {
  while (curtok()->kind == TK_MUL) {
    set_curtok(curtok()->next);
    if (out_is_ptr) *out_is_ptr = 1;
    g_last_ret_ptr_levels++;
    /* ポインタ修飾子 `int *const f()` / `int *volatile f()` の const/volatile を読み飛ばす。
     * これがないと戻り型の `*` の後の const で declarator が止まり E2006 になっていた
     * (ag_c は値の正しさのみ対象なので修飾子はパースして捨てる)。 */
    while (curtok()->kind == TK_CONST || curtok()->kind == TK_VOLATILE) {
      set_curtok(curtok()->next);
    }
  }
}

static void resolve_func_ret_typedef(token_kind_t *ret_kind, tk_float_kind_t *ret_fp_kind,
                                     token_ident_t **ret_tag, int *ret_is_ptr,
                                     int *ret_is_unsigned) {
  token_ident_t *td_id = (token_ident_t *)curtok();
  token_kind_t td_base = TK_EOF;
  tk_float_kind_t td_fp = TK_FLOAT_KIND_NONE;
  token_kind_t td_tag = TK_EOF;
  char *td_tag_name = NULL;
  int td_tag_len = 0;
  int td_is_ptr = 0;
  int td_is_unsigned = 0;
  /* typedef の unsigned 性を捕捉する。`typedef unsigned char u8` の戻り型 `u8 f()` は
   * td_base=TK_CHAR だが unsigned。捨てると sub-int 戻り値が符号拡張され
   * `u8 f(){return 200;}` が -56 に化ける (uint8_t ローカルと同根の戻り型版)。 */
  psx_typedef_info_t _ti = {0};
  if (psx_ctx_find_typedef_name(td_id->str, td_id->len, &_ti)) {
    td_base = _ti.base_kind; td_fp = _ti.fp_kind;
    td_tag = _ti.tag_kind; td_tag_name = _ti.tag_name; td_tag_len = _ti.tag_len;
    td_is_ptr = _ti.is_pointer; td_is_unsigned = _ti.is_unsigned;
    g_func_ret_is_funcptr = _ti.is_funcptr;
    g_func_funcptr_ret_is_pointer = _ti.funcptr_ret_is_pointer;
  }
  set_curtok(curtok()->next);
  *ret_kind = td_base;
  *ret_fp_kind = td_fp;
  if (td_is_ptr) *ret_is_ptr = 1;
  if (ret_is_unsigned && td_is_unsigned) *ret_is_unsigned = 1;
  if (td_tag != TK_EOF) {
    *ret_tag = calloc(1, sizeof(token_ident_t));
    (*ret_tag)->str = td_tag_name;
    (*ret_tag)->len = td_tag_len;
    *ret_kind = td_tag;
  }
}

static token_ident_t *parse_func_name_declarator_recursive(void) {
  psx_skip_gnu_attributes();
  while (tk_consume('*')) {
    skip_ptr_qualifiers();
  }
  if (tk_consume('(')) {
    while (tk_consume('*')) {
      skip_ptr_qualifiers();
    }
    token_ident_t *name = parse_func_name_declarator_recursive();
    tk_expect(')');
    return name;
  }
  return tk_consume_ident();
}

static token_ident_t *parse_func_declarator(int *out_is_variadic, int *out_has_unnamed_param,
                                            node_t ***out_args, int *out_nargs) {
  int arg_cap = 16;
  node_t **args = calloc(arg_cap, sizeof(node_t *));
  int nargs = 0;
  int is_variadic = 0;
  int has_unnamed_param = 0;
  int parsed_nested_inner_params = 0;
  g_func_ret_pointee_first_dim = 0;
  g_func_ret_pointee_second_dim = 0;
  g_func_ret_pointee_dim_count = 0;

  psx_skip_gnu_attributes();
  token_ident_t *tok = NULL;
  // function declarator returning function pointer:
  //   int (*f(void))(int) { ... }
  if (curtok()->kind == TK_LPAREN && curtok()->next && curtok()->next->kind == TK_MUL) {
    tk_expect('(');
    while (tk_consume('*')) {
      /* `int (*choose(...))(int)` のように外側 declarator が `(*` を含むとき、
       * 戻り値型は宣言子としてはポインタ (関数ポインタ) になる。
       * funcdef 側に戻り値ポインタを伝えるため、g_last_outer_is_ptr を立てる。 */
      g_last_outer_declarator_is_ptr = 1;
    }
    if (curtok()->kind == TK_LPAREN && curtok()->next && curtok()->next->kind == TK_MUL) {
      // nested pointer declarator: (*(*f(void))(int))
      tk_expect('(');
      while (tk_consume('*')) {
        g_last_outer_declarator_is_ptr = 1;
      }
      tok = tk_consume_ident();
      if (!tok) {
        psx_diag_ctx(curtok(), "funcdef", "%s",
                     diag_message_for(DIAG_ERR_PARSER_FUNCTION_DEF_EXPECTED));
      }
      tk_expect('(');
      if (!tk_consume(')')) {
        bool done = false;
        node_func_t node_tmp = {0};
        node_tmp.args = args;
        while (!done) {
          if (curtok()->kind == TK_ELLIPSIS) {
            set_curtok(curtok()->next);
            if (curtok()->kind == ',') {
              diag_emit_tokf(DIAG_ERR_PARSER_INVALID_CONTEXT, curtok(),
                             "%s",
                             diag_message_for(DIAG_ERR_PARSER_VARIADIC_NOT_LAST));
            }
            is_variadic = 1;
            done = true;
            continue;
          }
          if (parse_param_decl(&node_tmp, &nargs, &arg_cap, 0)) has_unnamed_param = 1;
          args = node_tmp.args;
          if (!tk_consume(',')) break;
          if (curtok()->kind == TK_RPAREN) {
            psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_PARAMETER));
          }
        }
        tk_expect(')');
      }
      tk_expect(')');
      parsed_nested_inner_params = 1;
    } else {
      tok = parse_func_name_declarator_recursive();
      if (!tok) {
        psx_diag_ctx(curtok(), "funcdef", "%s",
                     diag_message_for(DIAG_ERR_PARSER_FUNCTION_DEF_EXPECTED));
      }
    }
    tk_expect('(');
    if (!tk_consume(')')) {
      bool done = false;
      node_func_t node_tmp = {0};
      node_tmp.args = args;
      while (!done) {
        if (curtok()->kind == TK_ELLIPSIS) {
          set_curtok(curtok()->next);
          if (curtok()->kind == ',') {
            diag_emit_tokf(DIAG_ERR_PARSER_INVALID_CONTEXT, curtok(),
                           "%s",
                           diag_message_for(DIAG_ERR_PARSER_VARIADIC_NOT_LAST));
          }
          is_variadic = 1;
          done = true;
          continue;
        }
        if (parse_param_decl(&node_tmp, &nargs, &arg_cap, 0) && !parsed_nested_inner_params) {
          has_unnamed_param = 1;
        }
        args = node_tmp.args;
        if (!tk_consume(',')) break;
        if (curtok()->kind == TK_RPAREN) {
          psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_PARAMETER));
        }
      }
      tk_expect(')');
    }
    tk_expect(')');
    // consume trailing direct-declarator suffixes for returned function pointer type.
    while (curtok()->kind == TK_LPAREN || curtok()->kind == TK_LBRACKET) {
      if (tk_consume('(')) {
        int depth = 1;
        while (depth > 0) {
          if (tk_consume('(')) {
            depth++;
          } else if (tk_consume(')')) {
            depth--;
          } else {
            set_curtok(curtok()->next);
          }
        }
        continue;
      }
      if (tk_consume('[')) {
        /* pointee 配列次元 `int (*f())[N]` / `int (*f())[N][M]` を捕捉する。これを記録しないと
         * 呼び出し結果 `f()[i]` の行ストライドが分からず base 要素サイズで誤スケール→SIGSEGV。 */
        if (curtok()->kind != TK_RBRACKET) {
          int n = psx_parse_array_size_constexpr();
          if (g_func_ret_pointee_dim_count == 0) g_func_ret_pointee_first_dim = n;
          else if (g_func_ret_pointee_dim_count == 1) g_func_ret_pointee_second_dim = n;
        }
        g_func_ret_pointee_dim_count++;
        tk_expect(']');
      }
    }
  } else {
    tok = parse_func_name_declarator_recursive();
    if (!tok) {
      psx_diag_ctx(curtok(), "funcdef", "%s",
                   diag_message_for(DIAG_ERR_PARSER_FUNCTION_DEF_EXPECTED));
    }
    tk_expect('(');
    if (!tk_consume(')')) {
      bool done = false;
      node_func_t node_tmp = {0};
      node_tmp.args = args;
      while (!done) {
        if (curtok()->kind == TK_ELLIPSIS) {
          set_curtok(curtok()->next);
          if (curtok()->kind == ',') {
            diag_emit_tokf(DIAG_ERR_PARSER_INVALID_CONTEXT, curtok(),
                           "%s",
                           diag_message_for(DIAG_ERR_PARSER_VARIADIC_NOT_LAST));
          }
          is_variadic = 1;
          done = true;
          continue;
        }
        if (parse_param_decl(&node_tmp, &nargs, &arg_cap, 1)) has_unnamed_param = 1;
        args = node_tmp.args;
        if (!tk_consume(',')) break;
        if (curtok()->kind == TK_RPAREN) {
          psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_PARAMETER));
        }
      }
      tk_expect(')');
    }
  }

  psx_skip_gnu_attributes();
  *out_is_variadic = is_variadic;
  *out_has_unnamed_param = has_unnamed_param;
  *out_args = args;
  *out_nargs = nargs;
  return tok;
}

// funcdef = "int"? ident "(" params? ")" (";" | "{" stmt* "}")
// params  = "int"? ident ("," "int"? ident)*
/* 関数本体の `{ ... }` を 1 つの node_block_t にパースする。
 * 既に opening `{` は呼出側が consume 済みの前提。block scope を enter / leave し、
 * 未到達コード警告 (DIAG_WARN_PARSER_UNREACHABLE_CODE) も内部で発火する。
 * pragma pack マーカーは透過に消費する。 */
static node_block_t *parse_funcdef_body_block(void) {
  psx_ctx_enter_block_scope();
  node_block_t *body = arena_alloc(sizeof(node_block_t));
  body->base.kind = ND_BLOCK;
  int i = 0;
  int body_cap = 16;
  body->body = calloc(body_cap, sizeof(node_t *));
  int prev_terminates = 0;
  while (!tk_consume('}')) {
    // #pragma pack マーカーは関数本体冒頭・任意の位置で出現しうる。透過処理。
    if (psx_try_consume_pragma_pack_marker()) continue;
    if (prev_terminates && curtok()->kind != TK_CASE && curtok()->kind != TK_DEFAULT &&
        !(curtok()->kind == TK_IDENT && curtok()->next && curtok()->next->kind == TK_COLON)) {
      diag_warn_tokf(DIAG_WARN_PARSER_UNREACHABLE_CODE, curtok(),
                     "%s", diag_warn_message_for(DIAG_WARN_PARSER_UNREACHABLE_CODE));
      prev_terminates = 0;
    }
    if (i >= body_cap - 1) {
      body_cap = pda_next_cap(body_cap, i + 2);
      body->body = pda_xreallocarray(body->body, (size_t)body_cap, sizeof(node_t *));
    }
    body->body[i] = psx_stmt_stmt();
    node_kind_t k = body->body[i]->kind;
    prev_terminates = (k == ND_RETURN || k == ND_BREAK || k == ND_CONTINUE || k == ND_GOTO);
    i++;
  }
  body->body[i] = NULL;
  psx_ctx_leave_block_scope();
  return body;
}

/* 関数本体パース完了後、未使用変数・未初期化変数の警告を出す。
 * 仮引数 / underscore-prefix / 配列は対象外。 */
static void warn_unused_uninit_locals(void) {
  for (lvar_t *v = psx_decl_get_locals(); v; v = v->next_all) {
    if (!v->is_used && !v->is_param && v->name[0] != '_') {
      diag_warn_tokf(DIAG_WARN_PARSER_UNUSED_VARIABLE, curtok(),
                     diag_warn_message_for(DIAG_WARN_PARSER_UNUSED_VARIABLE),
                     v->len, v->name);
    } else if (v->is_used && !v->is_initialized && !v->is_param && !v->is_array) {
      diag_warn_tokf(DIAG_WARN_PARSER_UNINITIALIZED_VARIABLE, curtok(),
                     diag_warn_message_for(DIAG_WARN_PARSER_UNINITIALIZED_VARIABLE),
                     v->len, v->name);
    }
  }
}

static node_t *funcdef(void) {
  token_kind_t ret_kind;
  tk_float_kind_t ret_fp_kind = TK_FLOAT_KIND_NONE;
  token_ident_t *ret_tag = NULL;
  int ret_is_ptr = 0;
  int ret_td_unsigned = 0;
  g_last_outer_declarator_is_ptr = 0;
  g_last_ret_ptr_levels = 0;
  parse_func_decl_spec(&ret_kind, &ret_fp_kind, &ret_tag, &ret_is_ptr, &ret_td_unsigned);
  /* static 関数 (内部リンケージ) かを捕捉する。parse_func_decl_spec 直後に取る
   * (parse_func_declarator がパラメータ型で g_last_decl_is_static を上書きする前)。 */
  int fn_is_static = 0;
  psx_take_static_flag(&fn_is_static);
  if (ret_kind == TK_EOF) {
    diag_warn_tokf(DIAG_WARN_PARSER_IMPLICIT_INT_RETURN, curtok(),
                   "%s", diag_warn_message_for(DIAG_WARN_PARSER_IMPLICIT_INT_RETURN));
  }
  token_kind_t ret_token_kind = (ret_kind == TK_EOF) ? TK_INT : ret_kind;
  /* 戻り値型の unsigned 性を捕捉する (`unsigned` は ret_kind が TK_INT に潰れ
   * 符号性が落ちるため別管理)。parse_func_decl_spec 直後に読む (parse_func_declarator
   * が g_last_type_unsigned を変えるより前)。後段で関数名判明後に記録する。 */
  /* 基底型の unsigned 性。戻り値そのものの符号 (ret_is_unsigned, ポインタ返しは無関係なので
   * !ret_is_ptr でゲート) と、`unsigned char *g(); g()[i]` の pointee 符号 (ret_base_unsigned、
   * ポインタ返しでも保持し subscript の zero-extend に使う) を分ける。 */
  int ret_base_unsigned = psx_last_type_is_unsigned() || ret_td_unsigned;
  int ret_is_unsigned = !ret_is_ptr && ret_base_unsigned;
  int ret_pointee_const = g_last_type_const_qualified;
  int ret_pointee_volatile = g_last_type_volatile_qualified;
  /* 戻り型が _Complex か (parse_func_declarator が g_last_type_complex を変える前に読む)。
   * IR builder が複素数戻り値 (HFA: re→d0, im→d1) を組むために node に記録する。 */
  int ret_is_complex = !ret_is_ptr && psx_last_type_is_complex();
  psx_expr_set_current_func_ret_type(ret_token_kind, ret_fp_kind);
  psx_expr_set_current_func_ret_is_pointer(ret_is_ptr);
  psx_expr_set_current_func_ret_is_unsigned(ret_is_unsigned);
  // 構造体戻り値の場合、サイズを記録（ポインタ戻り値は除く）
  if ((ret_kind == TK_STRUCT || ret_kind == TK_UNION) && !ret_is_ptr) {
    if (ret_tag && psx_ctx_has_tag_type(ret_kind, ret_tag->str, ret_tag->len)) {
      psx_expr_set_current_func_ret_struct_size(
          psx_ctx_get_tag_size(ret_kind, ret_tag->str, ret_tag->len));
    } else {
      psx_expr_set_current_func_ret_struct_size(0);
    }
  } else {
    psx_expr_set_current_func_ret_struct_size(0);
  }
  // 関数ごとにローカル変数テーブルをリセット
  psx_decl_reset_locals();
  psx_ctx_reset_function_scope();
  psx_loop_reset();

  int is_variadic = 0;
  int has_unnamed_param = 0;
  node_t **args = NULL;
  int nargs = 0;
  token_ident_t *tok = parse_func_declarator(&is_variadic, &has_unnamed_param, &args, &nargs);
  /* declarator が `(*` を含めば、戻り値型は関数ポインタ。ret_is_ptr を立てて
   * 既に伝播していた ret_is_pointer / track_function_ret_type を更新。 */
  if (g_last_outer_declarator_is_ptr) {
    ret_is_ptr = 1;
    psx_expr_set_current_func_ret_is_pointer(1);
  }
  node_func_t *node = arena_alloc(sizeof(node_func_t));
  node->base.kind = ND_FUNCDEF;
  node->base.ret_struct_size = psx_expr_current_func_ret_struct_size();
  /* 戻り型の fp_kind をノードへ記録。IR builder の ir_type_from_node が
   * 関数の戻り型 (IR_TY_F32/F64) を決定し、callee が fp レジスタで返すために必要。
   * ただし `double *g()` のようにポインタを返す関数は戻り値が x0 のポインタ値なので
   * fp_kind を立ててはいけない (立てると funcall が d0 から読み SIGSEGV)。pointee が
   * fp であることは別途 ret_token_kind 経由 (psx_node_pointee_fp_kind) で扱う。 */
  node->base.fp_kind = ret_is_ptr ? TK_FLOAT_KIND_NONE : ret_fp_kind;
  node->base.is_complex = ret_is_complex;
  node->funcname = tok->str;
  node->funcname_len = tok->len;
  /* 同名グローバル変数とのコンフリクト検出 (C11 6.7p4: 関数と変数は同じ名前空間)。
   * `int bar; int bar(int){...}` のように変数として登録済みなら違法。 */
  if (find_global_var_by_name(tok->str, tok->len)) {
    psx_diag_ctx(curtok(), "funcdef",
                 "'%.*s' はグローバル変数として既に宣言されています (C11 6.7p4)",
                 tok->len, tok->str);
  }
  psx_ctx_define_function_name_with_ret(tok->str, tok->len,
                                         psx_expr_current_func_ret_struct_size());
  // float / double 戻り値型を記録 → call 経路で fcvtzs を挿入できるようにする
  // (ポインタ返しは fp 値でないので除外)
  if (ret_fp_kind != TK_FLOAT_KIND_NONE && !ret_is_ptr) {
    psx_ctx_set_function_ret_fp_kind(tok->str, tok->len, ret_fp_kind);
  }
  // 戻り値が _Complex なら記録 → 呼び出し側 funcall ノードへ is_complex を伝播。
  if (ret_is_complex) {
    psx_ctx_set_function_ret_is_complex(tok->str, tok->len, 1);
  }
  // 戻り値型が void かどうかを記録。代入/初期化での void 値使用検出に使う。
  if (ret_kind == TK_VOID && !ret_is_ptr) {
    psx_ctx_set_function_ret_void(tok->str, tok->len, 1);
  }
  /* C11 6.7p3: 同名関数の再宣言で戻り値型が異なるとエラー。 */
  if (!psx_ctx_track_function_ret_type(tok->str, tok->len, ret_token_kind, ret_is_ptr)) {
    psx_diag_ctx(curtok(), "funcdef",
                 "関数 '%.*s' の戻り値型が以前の宣言と異なります (C11 6.7p3)",
                 tok->len, tok->str);
  }
  /* ctx には基底型の unsigned を保存 (ポインタ返しでも pointee 符号として subscript で使う)。
   * 戻り値そのものの符号 (比較等) は call 側で is_pointer を見て別途ガードする。 */
  if (ret_base_unsigned) psx_ctx_set_function_ret_unsigned(tok->str, tok->len, 1);
  if (ret_is_ptr && (ret_pointee_const || ret_pointee_volatile)) {
    psx_ctx_set_function_ret_pointee_qualifiers(tok->str, tok->len,
                                                ret_pointee_const,
                                                ret_pointee_volatile);
  }
  /* 多段ポインタ戻り `int **g()`: ポインタ段数を記録 (`**g()` の deref 幅決定に使う)。
   * 基底型 `*` の段数のみ (`int (*f())[N]` の outer declarator ポインタは別扱いなので、
   * g_last_outer_declarator_is_ptr 由来の +1 は数えない)。 */
  if (ret_is_ptr && g_last_ret_ptr_levels > 0) {
    psx_ctx_set_function_ret_pointer_levels(tok->str, tok->len, g_last_ret_ptr_levels);
  }
  /* 配列へのポインタ戻り `int (*f())[N]` / `int (*f())[N][M]`: 先頭次元 N と、
   * あれば第2次元 M を記録。呼び出し結果 `f()[i]` のストライドを N*M*elem にし、
   * 第1 subscript 結果へ M*elem を carry するのに使う (0 なら通常のポインタ戻り)。 */
  if (ret_is_ptr && g_func_ret_pointee_dim_count >= 1 && g_func_ret_pointee_first_dim > 0) {
    psx_ctx_set_function_ret_pointee_array_first_dim(tok->str, tok->len,
                                                     g_func_ret_pointee_first_dim);
    if (g_func_ret_pointee_dim_count >= 2 && g_func_ret_pointee_second_dim > 0) {
      psx_ctx_set_function_ret_pointee_array_second_dim(tok->str, tok->len,
                                                        g_func_ret_pointee_second_dim);
    }
  }
  // variadic 情報と固定引数数を記録。caller 側 codegen が register/stack 切替に使い、
  // build_unqualified_call が引数数チェックに使う。
  // 非 variadic 関数でも nargs_fixed を記録するため常に呼ぶ。
  /* C11 6.7p4: 同名関数の再宣言で引数数 / 可変長性が異なれば conflicting types。
   * 戻り型 (上の track_function_ret_type) と同じ要領で初回値を覚えて以降比較する。 */
  if (!psx_ctx_track_function_nargs(tok->str, tok->len, nargs, is_variadic)) {
    psx_diag_ctx(curtok(), "funcdef",
                 "関数 '%.*s' の引数数が以前の宣言と異なります (C11 6.7p4)",
                 tok->len, tok->str);
  }
  psx_ctx_set_function_variadic(tok->str, tok->len, is_variadic ? 1 : 0, nargs);
  /* 仮引数 i の fp_kind を記録 → 呼び出し側で int→double 暗黙変換を挿入できる。
   * args[i] は parse_param_decl で fp_kind がセット済みの ND_LVAR。
   * 同時に再宣言時の型 mismatch も粗粒度カテゴリで照合する (C11 6.7p4)。 */
  for (int i = 0; i < nargs && i < 16; i++) {
    tk_float_kind_t pfk = (tk_float_kind_t)(args[i] ? args[i]->fp_kind : 0);
    int param_cat = PSX_PCAT_UNSET;
    if (pfk == TK_FLOAT_KIND_FLOAT) param_cat = PSX_PCAT_FLOAT;
    else if (pfk >= TK_FLOAT_KIND_DOUBLE) param_cat = PSX_PCAT_DOUBLE;
    else if (args[i] && ps_node_is_pointer(args[i])) param_cat = PSX_PCAT_PTR;
    else if (args[i]) {
      /* 整数の幅 (4 vs 8) は宣言と定義で粒度を変えても等価扱いするため (proto は
       * placeholder ND_NUM で sz=4、def は ND_LVAR で abi_type_size=8 となり一致しない
       * ことが多いため) INT カテゴリ 1 つに集約する。fp/pointer/struct との不一致は検出する。
       * 厳密な long vs int 区別はパラメータ型を別途追跡する必要があり後続課題。 */
      int sz = ps_node_type_size(args[i]);
      if (sz >= 1 && sz <= 8) param_cat = PSX_PCAT_INT4;     /* INT 系を一律 */
      else if (sz > 0) param_cat = PSX_PCAT_STRUCT;
    }
    if (!psx_ctx_track_function_param_category(tok->str, tok->len, i, param_cat)) {
      psx_diag_ctx(curtok(), "funcdef",
                   "関数 '%.*s' の引数 %d の型が以前の宣言と異なります (C11 6.7p4)",
                   tok->len, tok->str, i + 1);
    }
    if (pfk != TK_FLOAT_KIND_NONE) {
      psx_ctx_set_function_param_fp_kind(tok->str, tok->len, i, pfk);
    } else if (args[i] && !ps_node_is_pointer(args[i])) {
      /* 整数スカラ仮引数の幅を記録 → 呼び出し側で fp 実引数を F2I 変換できる
       * (`f(7.9)` の 7.9 を int に切り詰め)。サイズ 1/2/4 は w 幅 (4)、8 は x 幅。
       * struct/union メンバ等の非スカラ (>8) は対象外 (値渡しは別経路)。 */
      int sz = ps_node_type_size(args[i]);
      if (sz >= 1 && sz <= 4) {
        psx_ctx_set_function_param_int_size(tok->str, tok->len, i, 4);
      } else if (sz == 8) {
        psx_ctx_set_function_param_int_size(tok->str, tok->len, i, 8);
      }
    }
  }
  /* struct/union を返す関数のタグを記録する。ポインタ返し (`struct N *get(void)`)
   * でも記録し、`get()->m` のメンバアクセスを解決できるようにする。ポインタ性は
   * psx_ctx_get_function_ret_is_pointer で別途参照される。 */
  if ((ret_kind == TK_STRUCT || ret_kind == TK_UNION) && ret_tag) {
    psx_ctx_set_function_ret_tag(tok->str, tok->len, ret_kind, ret_tag->str, ret_tag->len);
  }
  {
    int ret_is_funcptr = g_func_ret_is_funcptr;
    int funcptr_ret_is_pointer = g_func_funcptr_ret_is_pointer;
    if (g_last_outer_declarator_is_ptr) {
      ret_is_funcptr = 1;
      if ((ret_kind == TK_STRUCT || ret_kind == TK_UNION) && ret_tag)
        funcptr_ret_is_pointer = ret_is_ptr ? 1 : 0;
    }
    if (ret_is_funcptr)
      psx_ctx_set_function_ret_is_funcptr(tok->str, tok->len, 1, funcptr_ret_is_pointer);
  }
  psx_expr_set_current_funcname(tok->str, tok->len); // __func__ 用
  node->is_static = fn_is_static;
  node->args = args;
  node->is_variadic = is_variadic;
  node->nargs = nargs;
  // 可変長引数関数: ローカル変数スペースを引数レジスタ保存領域の後ろに移動する
  if (node->is_variadic) {
    psx_decl_reserve_variadic_regs();
  }

  // 関数プロトタイプ宣言（本体なし）
  if (tk_consume(';')) {
    /* __func__ 用に立てた現在関数名を NULL に戻す。プロトタイプの後はファイルスコープ
     * なので、ここを残すと後続のファイルスコープ複合リテラル `&(int){5}` 等が「関数内」と
     * 誤判定されローカル lvar 経路に乗ってしまう (assert.h の宣言後に顕在化)。 */
    psx_expr_set_current_funcname(NULL, 0);
    return NULL;
  }
  if (has_unnamed_param) {
    // 関数定義の仮引数では識別子必須。
    psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_PARAMETER));
  }
  /* C11 6.9p3: 同一名の関数を 2 度定義することはできない。プロトタイプ宣言は何度でも OK
   * だが、本体 `{...}` を伴う定義は 1 度のみ。`;` (プロトタイプ) を弾いた後にチェックする。 */
  if (!psx_ctx_track_function_defined(tok->str, tok->len)) {
    psx_diag_ctx(curtok(), "funcdef",
                 "関数 '%.*s' の重複定義 (C11 6.9p3)",
                 tok->len, tok->str);
  }

  // 関数本体 (ブロック)
  tk_expect('{');
  node_block_t *body = parse_funcdef_body_block();
  node->base.rhs = (node_t *)body;
  psx_ctx_validate_goto_refs();

  warn_unused_uninit_locals();

  /* IR builder (Phase 4d-1〜) が関数ごとの lvar リストを必要とするため、
   * 関数解析完了時点の all_locals 先頭を node に保存しておく。
   * psx_decl_reset_locals は次の関数開始時に呼ばれるが、それは静的変数を
   * NULL に戻すだけで、既存 lvar_t は arena/calloc されたまま残る。 */
  node->lvars = psx_decl_get_locals();

  /* 関数本体を抜けたらファイルスコープに戻る。現在関数名を NULL に戻し、関数間の
   * ファイルスコープ宣言が「関数内」と誤判定されないようにする。 */
  psx_expr_set_current_funcname(NULL, 0);

  return (node_t *)node;
}

// expr = assign ("," assign)*
node_t *ps_expr_ctx(tokenizer_context_t *tk_ctx, token_t *start) {
  if (tk_ctx) {
    tk_set_current_token_ctx(tk_ctx, start);
  }
  tk_set_current_token(start);
  node_t *node = psx_expr_expr();
  if (tk_ctx) {
    tk_set_current_token_ctx(tk_ctx, tk_get_current_token());
  }
  return node;
}

node_t *ps_expr_from(token_t *start) {
  return ps_expr_ctx(NULL, start);
}

node_t *ps_expr(void) {
  return ps_expr_ctx(NULL, tk_get_current_token());
}
