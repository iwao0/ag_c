#include "parser.h"
#include "parser_public.h"  /* codegen_iter_globals prototype */
#include "internal/arena.h"
#include "internal/node_utils.h"
#include "internal/semantic_ctx.h"
#include "internal/decl.h"
#include "internal/core.h"
#include "internal/alignas_value.h"
#include "internal/anon_tag.h"
#include "internal/array_suffixes.h"
#include "internal/diag.h"
#include "internal/dynarray.h"
#include "internal/enum_const.h"
#include "internal/expr.h"
#include "internal/loop_ctx.h"
#include "internal/stmt.h"
#include "internal/struct_layout.h"
#include "internal/switch_ctx.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include "../pragma_pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

string_lit_t *string_literals = NULL;
float_lit_t *float_literals = NULL;
global_var_t *global_vars = NULL;

/* parser_public.h で宣言した visitor の実装 (Phase C3-1)。
 * codegen 側が global_vars / string_literals / float_literals リストを
 * 直接舐めるのを廃して、走査経路を 1 箇所にまとめる。 */
void codegen_iter_globals(global_var_visitor_t fn, void *user) {
  for (global_var_t *gv = global_vars; gv; gv = gv->next) {
    fn(gv, user);
  }
}

bool codegen_iter_string_literals(string_lit_visitor_t fn, void *user) {
  if (!string_literals) return false;
  for (string_lit_t *lit = string_literals; lit; lit = lit->next) {
    fn(lit, user);
  }
  return true;
}

bool codegen_iter_float_literals(float_lit_visitor_t fn, void *user) {
  if (!float_literals) return false;
  for (float_lit_t *lit = float_literals; lit; lit = lit->next) {
    fn(lit, user);
  }
  return true;
}

bool codegen_has_string_literals(void) { return string_literals != NULL; }
bool codegen_has_float_literals(void) { return float_literals != NULL; }
static int g_last_type_const_qualified = 0;
static int g_last_type_volatile_qualified = 0;
static int g_last_alignas_value = 0;
/* funcdef の外側 declarator (`int (*f(...))(...)`) で `(*` を見たら 1。
 * 戻り値型を関数ポインタ (= ポインタ) として扱うため、parse_func_declarator
 * から funcdef へ伝える。各 funcdef 開始時にリセットする。 */
static int g_last_outer_declarator_is_ptr = 0;
static int g_last_decl_is_extern = 0;
static int g_last_decl_is_static = 0;
static int g_toplevel_decl_elem_size = 8;
static int g_toplevel_decl_is_extern = 0;
static int g_toplevel_decl_is_thread_local = 0;
static int g_toplevel_decl_is_typedef = 0;
static token_kind_t g_toplevel_decl_base_kind = TK_EOF;
static int g_toplevel_decl_is_unsigned = 0;
static tk_float_kind_t g_toplevel_decl_fp_kind = TK_FLOAT_KIND_NONE;
static token_kind_t g_toplevel_decl_tag_kind = TK_EOF;
static char *g_toplevel_decl_tag_name = NULL;
static int g_toplevel_decl_tag_len = 0;
static int g_toplevel_decl_base_is_ptr = 0;
static int g_toplevel_decl_pointee_const = 0;
static int g_toplevel_decl_pointee_volatile = 0;
// typedef 由来の配列型の dims (使用側 `M2 g;` で typedef の `[2][3]` を保持)。
// reset_toplevel_decl_spec_state でクリア、resolve_toplevel_typedef_ref で
// 設定。parse_toplevel_array_suffixes が dims を append する。
static int g_toplevel_decl_td_array_dims[8] = {0};
static int g_toplevel_decl_td_array_dim_count = 0;

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
                                           const int *td_dims, int td_dim_count);
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
static int parse_param_decl(node_func_t *node, int *nargs, int *arg_cap);
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
} param_decl_spec_t;
static int parse_param_tag_decl_spec(param_decl_spec_t *out);
static void parse_param_scalar_decl_spec(param_decl_spec_t *out);
static void parse_param_decl_spec(param_decl_spec_t *out);
static void parse_func_decl_spec(token_kind_t *ret_kind, tk_float_kind_t *ret_fp_kind,
                                 token_ident_t **ret_tag, int *ret_is_ptr);
static void parse_pointer_suffix_flags(int *out_is_ptr);
static void resolve_func_ret_typedef(token_kind_t *ret_kind, tk_float_kind_t *ret_fp_kind,
                                     token_ident_t **ret_tag, int *ret_is_ptr);
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
  int td_sizeof = 0;
  int td_first = 0;
  int td_dim_count = 0;
  int td_is_unsigned = 0;
  psx_ctx_find_typedef_name_ex3(id->str, id->len, &td_base, &td_elem, &td_fp,
                                &td_tag, &td_tag_name, &td_tag_len, &td_is_ptr,
                                NULL, NULL, &td_is_unsigned, &td_is_array, &td_sizeof,
                                &td_first, g_toplevel_decl_td_array_dims, &td_dim_count, 8);
  g_toplevel_decl_td_array_dim_count = (td_is_array && td_dim_count > 0) ? td_dim_count : 0;
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
  g_toplevel_decl_is_typedef = 0;
  g_toplevel_decl_base_kind = TK_EOF;
  g_toplevel_decl_is_unsigned = 0;
  g_toplevel_decl_fp_kind = TK_FLOAT_KIND_NONE;
  g_toplevel_decl_tag_kind = TK_EOF;
  g_toplevel_decl_tag_name = NULL;
  g_toplevel_decl_tag_len = 0;
  g_toplevel_decl_base_is_ptr = 0;
  g_toplevel_decl_pointee_const = 0;
  g_toplevel_decl_pointee_volatile = 0;
  g_toplevel_decl_td_array_dim_count = 0;
  for (int i = 0; i < 8; i++) g_toplevel_decl_td_array_dims[i] = 0;
}

static int parse_toplevel_tag_decl_spec(void) {
  if (!psx_ctx_is_tag_keyword(curtok()->kind)) return 0;
  parse_toplevel_tag_head(&g_toplevel_decl_tag_kind, &g_toplevel_decl_tag_name, &g_toplevel_decl_tag_len);
  g_toplevel_decl_base_kind = g_toplevel_decl_tag_kind;
  resolve_toplevel_tag_decl_layout_or_ref();
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
  psx_diag_undefined_with_name(curtok(), diag_text_for(DIAG_TEXT_TAG_TYPE_SUFFIX),
                               g_toplevel_decl_tag_name, g_toplevel_decl_tag_len);
}

static void parse_toplevel_tag_head(token_kind_t *out_kind, char **out_name, int *out_len) {
  *out_kind = curtok()->kind;
  set_curtok(curtok()->next);
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
  g_toplevel_decl_is_extern = 0;
  g_toplevel_decl_is_thread_local = 0;
  psx_take_type_qualifiers(&g_toplevel_decl_pointee_const, &g_toplevel_decl_pointee_volatile);
}

bool psx_is_decl_prefix_token(token_kind_t k) {
  return k == TK_CONST || k == TK_VOLATILE || k == TK_EXTERN || k == TK_STATIC ||
         k == TK_AUTO || k == TK_REGISTER || k == TK_INLINE || k == TK_NORETURN ||
         k == TK_THREAD_LOCAL || k == TK_ALIGNAS || k == TK_ATOMIC;
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

static void skip_ptr_qualifiers(void) {
  while (curtok()->kind == TK_CONST || curtok()->kind == TK_VOLATILE || curtok()->kind == TK_RESTRICT) {
    set_curtok(curtok()->next);
  }
}

void psx_consume_pointer_prefix(int *is_ptr) {
  while (tk_consume('*')) {
    if (is_ptr) *is_ptr = 1;
    skip_ptr_qualifiers();
  }
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

static void parse_toplevel_decl_spec(void) {
  reset_toplevel_decl_spec_state();
  consume_toplevel_typedef_storage_class();

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
node_t **ps_program_ctx(tokenizer_context_t *tk_ctx, token_t *start) {
  if (tk_ctx) {
    tk_set_current_token_ctx(tk_ctx, start);
  }
  tk_set_current_token(start);
  /* 翻訳単位境界で関数名テーブルを初期化。
   * テストが同プロセスで複数プログラムを処理しても前回の登録が漏れないようにする。 */
  psx_ctx_reset_function_names();
  int cap = 16;
  node_t **codes = calloc(cap, sizeof(node_t*));
  int i = 0;
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
    if (i >= cap - 1) { // NULL終端用
      cap = pda_next_cap(cap, i + 2);
      codes = pda_xreallocarray(codes, (size_t)cap, sizeof(node_t *));
    }
    codes[i++] = fn;
  }
  codes[i] = NULL;
  if (tk_ctx) {
    tk_set_current_token_ctx(tk_ctx, tk_get_current_token());
  }
  return codes;
}

node_t **ps_program_from(token_t *start) {
  return ps_program_ctx(NULL, start);
}

node_t **ps_program(void) {
  return ps_program_ctx(NULL, tk_get_current_token());
}

static int is_toplevel_function_signature(token_t *tok) {
  if (!tok) return 0;
  token_t *t = skip_decl_prefix_lookahead(tok);
  if (!t) return 0;
  if (psx_ctx_is_type_token(t->kind)) {
    // 複合型キーワード（unsigned long 等）を全てスキップ
    while (t && psx_ctx_is_type_token(t->kind)) t = t->next;
  } else if (psx_ctx_is_typedef_name_token(t)) {
    t = t->next; // typedef 名は1トークン
  } else {
    return 0;
  }
  while (t && t->kind == TK_MUL) t = t->next;
  if (!t) return 0;
  if (t->kind == TK_IDENT) {
    return t->next && t->next->kind == TK_LPAREN;
  }
  // function declarator returning function pointer:
  //   int (*f(void))(int)
  //   int (*(*f(void))(int))[3]
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
  while (t && t->kind == TK_MUL) t = t->next; // skip optional pointer(s)
  if (!t) return 0;
  if (t->kind == TK_IDENT) {
    return t->next && t->next->kind == TK_LPAREN;
  }
  // parenthesized function name declarator: struct S {...} (f)(...)
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

static global_var_t *find_global_var_by_name(char *name, int len) {
  for (global_var_t *gv = global_vars; gv; gv = gv->next) {
    if (gv->name_len == len && memcmp(gv->name, name, (size_t)len) == 0) {
      return gv;
    }
  }
  return NULL;
}

static global_var_t *register_toplevel_global_decl(char *name, int len, int is_ptr,
                                                   int is_array, int arr_total, int is_extern_decl,
                                                   int has_incomplete_array) {
  if (is_extern_decl) {
    global_var_t *existing = find_global_var_by_name(name, len);
    if (existing) return existing;
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
  /* tag (struct / union) 情報を decl spec から引き継ぐ。
   * is_ptr のときは is_tag_pointer=1 を立て、`pp->x` のメンバアクセスで
   * build_member_access が tag を引けるようにする。 */
  psx_decl_set_gvar_tag(gv, g_toplevel_decl_tag_kind, g_toplevel_decl_tag_name,
                         g_toplevel_decl_tag_len, is_ptr);
  /* 浮動小数スカラのとき fp_kind を引き継ぐ。ポインタは整数として扱う。 */
  gv->fp_kind = is_ptr ? (unsigned char)TK_FLOAT_KIND_NONE
                       : (unsigned char)g_toplevel_decl_fp_kind;
  /* _Bool スカラ: 代入/初期化を 0/1 に正規化するため記録する。 */
  gv->is_bool = (!is_ptr && !is_array && g_toplevel_decl_base_kind == TK_BOOL) ? 1 : 0;
  gv->elem_is_bool = (!is_ptr && is_array && g_toplevel_decl_base_kind == TK_BOOL) ? 1 : 0;
  /* unsigned スカラ/配列要素: load を zero-extend / 比較を unsigned にするため記録。
   * スカラは node の is_unsigned、配列は pointee_is_unsigned に使う (ポインタ値
   * 自体は unsigned ではないので is_ptr は除外)。 */
  gv->is_unsigned = (!is_ptr && g_toplevel_decl_is_unsigned) ? 1 : 0;
  gv->next = global_vars;
  global_vars = gv;
  return gv;
}

void psx_skip_func_suffix_groups(int *out_has_func_suffix) {
  while (curtok()->kind == TK_LPAREN) {
    if (out_has_func_suffix) *out_has_func_suffix = 1;
    int depth = 1;
    set_curtok(curtok()->next);
    while (depth > 0) {
      if (curtok()->kind == TK_EOF) {
        diag_emit_tokf(DIAG_ERR_PARSER_MISSING_FUNC_DECL_RPAREN, curtok(), "%s",
                       diag_message_for(DIAG_ERR_PARSER_MISSING_FUNC_DECL_RPAREN));
      }
      if (curtok()->kind == TK_LPAREN) depth++;
      else if (curtok()->kind == TK_RPAREN) depth--;
      set_curtok(curtok()->next);
    }
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
  psx_diag_ctx(curtok(), "decl", "宣言子列が多すぎます（上限 %d）", PS_MAX_DECLARATOR_COUNT);
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

/* グローバル struct/union の `.member` designator を解決する。
 * struct: そのメンバの flat slot index (先行メンバの slot 数の総和) を返す。
 * union : 0 を返し *out_ordinal に活性メンバ序数を入れる (codegen がその型で出力)。
 * 見つからなければ -1。 */
static int resolve_global_member_designator(global_var_t *gv, char *mname, int mlen,
                                            int *out_ordinal) {
  int n = psx_ctx_get_tag_member_count(gv->tag_kind, gv->tag_name, gv->tag_len);
  int slot = 0;
  for (int i = 0; i < n; i++) {
    tag_member_info_t mi = {0};
    if (!psx_ctx_get_tag_member_info(gv->tag_kind, gv->tag_name, gv->tag_len, i, &mi)) break;
    if (mi.len == mlen && mi.name && strncmp(mi.name, mname, (size_t)mlen) == 0) {
      if (out_ordinal) *out_ordinal = i;
      return (gv->tag_kind == TK_UNION) ? 0 : slot;
    }
    /* 入れ子 struct メンバは内側スカラ数だけ slot を進める (`i` は 2 slot)。 */
    slot += global_member_flat_slots(&mi);
  }
  return -1;
}

static int resolve_global_addr_init(node_t *e, char **sym, int *sym_len, long long *off);

/* static local 配列の lowering (decl.c) からも使えるよう非 static 化。 */
void psx_parse_global_brace_init_flat(global_var_t *gv, int *cap, int start_idx) {
  tk_expect('{');
  if (tk_consume('}')) return;
  /* 書き込み位置はフラットな絶対 index。ネスト brace の再帰でも連続して
   * 追記できるよう、現在の充填位置 (init_count) から開始する。
   * designator [N]=/.member= で外側が cur_idx をジャンプ済みのときは、その slot
   * (start_idx) から書き始める (`{.z=14, .i={12,13}}` で .i の brace を slot 0 へ)。
   * start_idx < 0 は「init_count から」を意味する (トップレベル呼出)。 */
  int cur_idx = (start_idx >= 0) ? start_idx : gv->init_count;
  for (;;) {
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
      tk_expect(']');
      tk_expect('=');
      /* struct 要素配列の `[N]=` は要素 1 つが内側スカラ数だけ slot を占めるので
       * N にその数を掛ける (`struct P g[3]={[2]={5,6}}` の [2] は flat slot 4)。
       * scalar 要素配列は 1 slot なので従来どおり N。 */
      int elem_slots = 1;
      if (gv->tag_kind == TK_STRUCT) {
        elem_slots = global_flat_slot_count(gv->tag_kind, gv->tag_name, gv->tag_len);
        if (elem_slots < 1) elem_slots = 1;
      }
      cur_idx = (int)idx_val * elem_slots;
    }
    /* `.member = expr` 形式の struct/union メンバ designator (C11 6.7.9p6)。
     * メンバの flat slot へ cur_idx を飛ばす。union は活性メンバ序数を記録。 */
    else if (curtok()->kind == TK_DOT) {
      set_curtok(curtok()->next);
      token_ident_t *m = tk_consume_ident();
      if (!m || gv->tag_kind == TK_EOF) {
        psx_diag_ctx(curtok(), "decl", "メンバ指定初期化子が不正です");
      }
      int ordinal = 0;
      int slot = resolve_global_member_designator(gv, m->str, m->len, &ordinal);
      if (slot < 0) {
        psx_diag_ctx(curtok(), "decl", "メンバ指定初期化子のメンバが見つかりません");
      }
      tk_expect('=');
      cur_idx = slot;
      if (gv->tag_kind == TK_UNION) gv->union_init_ordinal = ordinal;
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
       * 後方ジャンプ済みのときも正しい slot へ)。 */
      psx_parse_global_brace_init_flat(gv, cap, cur_idx);
      cur_idx = gv->init_count;
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
    case ND_GVAR: {
      node_gvar_t *g = (node_gvar_t *)e;
      *sym = g->name; *sym_len = g->name_len;
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

static void apply_toplevel_object_initializer(global_var_t *gv) {
  if (!tk_consume('=')) return;
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
      gv->type_size = gv->init_count * gv->deref_size;
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
      /* C11 6.7.6.2p1 + 6.7.9p14: `char a[] = "...";` 形式。文字列の各バイトと
       * null 終端を init_values へ展開し、type_size を確定する。
       * char 以外 (wchar_t など) は未対応。 */
      int elem = gv->deref_size > 0 ? gv->deref_size : 1;
      if (elem == 1) {
        int total = s->byte_len + 1; /* null 終端を含む */
        gv->has_init = 1;
        gv->init_values = calloc((size_t)total, sizeof(long long));
        string_lit_t *lit = NULL;
        for (string_lit_t *l = string_literals; l; l = l->next) {
          if (strcmp(l->label, s->string_label) == 0) { lit = l; break; }
        }
        if (lit) {
          for (int i = 0; i < s->byte_len; i++) {
            gv->init_values[i] = (unsigned char)lit->str[i];
          }
        }
        gv->init_values[s->byte_len] = 0;
        gv->init_count = total;
        if (gv->type_size == 0 && gv->is_array) gv->type_size = total;
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
  toplevel_array_suffix_t arr = parse_toplevel_array_suffixes(head.paren_array_mul);
  validate_toplevel_object_array_suffix(arr);
  global_var_t *gv = register_toplevel_object_from_declarator(head.name, head.is_ptr, arr);
  if (gv && !head.is_ptr && arr.is_array && arr.dim_count >= 2) {
    apply_global_multidim_strides(gv, arr.dims, arr.dim_count, g_toplevel_decl_elem_size);
  }
  finalize_toplevel_object_declarator(gv);
}

static toplevel_declarator_head_t parse_toplevel_declarator_head(int base_is_ptr, int require_name) {
  toplevel_declarator_head_t out = new_toplevel_declarator_head(base_is_ptr);
  psx_consume_pointer_prefix(&out.is_ptr);
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
  int td_is_array = (!is_ptr && (arr.is_array || arr.has_incomplete_array)) ? 1 : 0;
  int td_first_dim = td_is_array ? arr.first_dim : 0;
  int td_dim_count = (td_is_array && !is_ptr) ? arr.dim_count : 0;
  register_toplevel_typedef_name(name, stored_base_kind, is_ptr, typedef_sizeof, td_is_array,
                                 td_first_dim, arr.dims, td_dim_count);
}

static void register_toplevel_typedef_name(token_ident_t *name, token_kind_t stored_base_kind,
                                           int is_ptr, int typedef_sizeof, int td_is_array,
                                           int td_first_dim,
                                           const int *td_dims, int td_dim_count) {
  if (!psx_ctx_define_typedef_name_ex3(name->str, name->len, stored_base_kind, g_toplevel_decl_elem_size,
                                  g_toplevel_decl_fp_kind, g_toplevel_decl_tag_kind,
                                  g_toplevel_decl_tag_name, g_toplevel_decl_tag_len,
                                  is_ptr, typedef_sizeof,
                                  g_toplevel_decl_pointee_const, g_toplevel_decl_pointee_volatile,
                                  is_toplevel_typedef_unsigned(stored_base_kind), td_is_array,
                                  td_first_dim, td_dims, td_dim_count)) {
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
  token_ident_t *name = parse_decl_name_recursive(is_ptr, 1, out_paren_array_mul);
  psx_skip_func_suffix_groups(NULL);
  return name;
}

static token_ident_t *parse_decl_name_recursive(int *is_ptr, int require_name, int *out_paren_array_mul) {
  psx_consume_pointer_prefix(is_ptr);
  token_ident_t *name = NULL;
  int had_parens = 0;
  int paren_array_mul = 1;
  if (tk_consume('(')) {
    had_parens = 1;
    name = parse_decl_name_recursive(is_ptr, require_name, &paren_array_mul);
    paren_array_mul = psx_parse_array_suffixes_constexpr_required(paren_array_mul);
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
  while (curtok()->kind == TK_LPAREN) {
    skip_balanced_group(TK_LPAREN, TK_RPAREN);
  }
}

static void parse_toplevel_decl_after_type(void) {
  if (g_toplevel_decl_is_typedef) {
    parse_toplevel_declarator_stmt(g_toplevel_decl_base_is_ptr, apply_toplevel_typedef_from_head);
    return;
  }
  parse_toplevel_declarator_stmt(0, apply_toplevel_object_from_head);
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
      !is_toplevel_function_signature(curtok())) {
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
    psx_diag_undefined_with_name(curtok(), diag_text_for(DIAG_TEXT_TAG_TYPE_SUFFIX), tag_name, tag_len);
  }
  install_toplevel_tag_decl_globals(tag_kind, tag_name, tag_len);
  parse_toplevel_declarator_list();
  tk_expect(';');
}

static int g_last_type_unsigned = 0;
static int g_last_type_complex = 0;
// g_last_type_atomic is defined above (before skip_cv_qualifiers)

int psx_last_type_is_unsigned(void) {
  return g_last_type_unsigned;
}

int psx_last_type_is_complex(void) {
  return g_last_type_complex;
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
    break;
  }

  if (curtok() == start) return TK_EOF;
  g_last_type_unsigned = saw_unsigned;
  g_last_type_complex = saw_complex;
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
      }
      bracket_count++;
    }
  }
  return name;
}

/* 仮引数 VLA / 多次元配列宣言子の lvar 登録 (`int a[n]` / `int a[][N]` /
 * `int a[][N][M]` / VLA dim that's another param 等)。
 * C11 6.7.6.3p7 により int *a として扱われるが、pointee が配列の場合は
 * outer_stride / mid_stride 等を立てて `a[i]` の steping を pointee 全体に揃える。 */
static lvar_t *register_vla_array_param(token_ident_t *param, param_decl_spec_t *ds,
                                         int inner_first_dim, int inner_second_dim,
                                         token_ident_t *inner_first_dim_ident) {
  // size=8 (pointer), elem_size=実際の要素サイズ
  lvar_t *var = psx_decl_register_lvar_sized(param->str, param->len, 8, ds->elem_size, 0);
  /* 1D の fp 要素配列引数 (`double a[n]`): pointee の fp 種別を伝播。size==elem_size
   * (=8) で lvar_is_pointer の size>elem_size 判定に漏れる double 要素でも、これで
   * ポインタ認識され subscript が fp load になる (int a[n] は size>elem_size で OK)。 */
  if (inner_first_dim == 0 && ds->fp_kind != TK_FLOAT_KIND_NONE) {
    var->pointee_fp_kind = ds->fp_kind;
  }
  if (inner_first_dim > 0) {
    var->outer_stride = inner_first_dim * ds->elem_size;
    var->base_deref_size = (short)ds->elem_size;
    if (inner_second_dim > 0) {
      var->outer_stride = inner_first_dim * inner_second_dim * ds->elem_size;
      var->mid_stride = inner_second_dim * ds->elem_size;
    }
    return var;
  }
  if (!inner_first_dim_ident) return var;
  /* C99 6.7.6.3p7 VLA-as-param: `int g[n][m]` の内側 dim が他のパラメータ。
   * row stride スロットを確保し、関数 entry で
   *   *[rs_slot] = *[src_param] * elem_size
   * を計算する。これにより subscript の vla_rsf 経路 (expr.c) が
   * runtime stride を読んで `g[i]` を正しく steping できる。 */
  lvar_t *src = psx_decl_find_lvar(inner_first_dim_ident->str,
                                   inner_first_dim_ident->len);
  if (!src || !src->is_param) {
    psx_diag_ctx(curtok(), "param",
                 "VLA パラメータの dim '%.*s' は同関数の先行パラメータでなければなりません",
                 inner_first_dim_ident->len, inner_first_dim_ident->str);
    return var;
  }
  /* row stride スロットを匿名で確保。名前は param 名 + "__rs" で
   * 衝突しないようにする (実体は VLA param 内部用)。 */
  static char rs_name_buf[64];
  int rs_name_len = snprintf(rs_name_buf, sizeof(rs_name_buf),
                              "__rs_%.*s", param->len, param->str);
  char *rs_name = arena_alloc((size_t)rs_name_len + 1);
  memcpy(rs_name, rs_name_buf, (size_t)rs_name_len);
  rs_name[rs_name_len] = '\0';
  lvar_t *rs = psx_decl_register_lvar_sized(rs_name, rs_name_len, 8, 8, 0);
  var->is_vla = 1;
  var->vla_row_stride_frame_off = rs->offset;
  var->vla_row_stride_src_offset = src->offset;
  var->vla_row_stride_elem_size = (short)ds->elem_size;
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
                                     param_inner_second_dim,
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
    /* `struct V (*p)[N]` 配列へのポインタ仮引数: 上の is_tag_pointer=1 のままだと
     * 1 要素 (struct サイズ) ずつしか進まず行を跨げない。outer_stride を 1 行 (N 要素)
     * に設定し is_tag_pointer をクリアして、ローカルの `struct V (*p)[N]` と同じ
     * 「配列へのポインタ」表現にする (`p[i][j].m` が正しくスケールする)。 */
    if (param_is_array_declarator && param_inner_first_dim > 0 && ds->struct_size > 0) {
      var->is_tag_pointer = 0;
      var->base_deref_size = (short)ds->struct_size;
      var->outer_stride = param_inner_first_dim * ds->struct_size;
      if (param_inner_second_dim > 0) {
        var->outer_stride = param_inner_first_dim * param_inner_second_dim * ds->struct_size;
        var->mid_stride = param_inner_second_dim * ds->struct_size;
      }
    }
    return var;
  }
  if (param_is_ptr && ds->tag_kind == TK_EOF) {
    /* スカラー型へのポインタ仮引数（int *p, char *p, int **pp など）。
     * 多段ポインタなら pointee_size=8、それ以外は ds->elem_size。 */
    int pointee_size = (param_ptr_levels >= 2) ? 8 : ds->elem_size;
    lvar_t *var = psx_decl_register_lvar_sized(param->str, param->len, 8, pointee_size, 0);
    var->base_deref_size = (short)ds->elem_size;
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
      var->outer_stride = param_inner_first_dim * ds->elem_size;
      if (param_inner_second_dim > 0) {
        var->outer_stride = param_inner_first_dim * param_inner_second_dim * ds->elem_size;
        var->mid_stride = param_inner_second_dim * ds->elem_size;
      }
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
  // スカラー型仮引数（既存の動作）
  return psx_decl_register_lvar(param->str, param->len);
}

static int parse_param_decl(node_func_t *node, int *nargs, int *arg_cap) {
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
  if (!param) {
    // int f(void) の "void" は仮引数0件として扱う（C11 6.7.6.3）。
    if (ds.base_type_kind == TK_VOID && ds.tag_kind == TK_EOF && !ds.saw_typedef_name &&
        !param_is_ptr && !param_is_array_declarator) {
      return 0;
    }
    // decl-specifier はあるが識別子が無い仮引数（例: int f(int);）は
    // プロトタイプでは許容し、関数定義時のみ呼び出し元で診断する。
    if (ds.base_type_kind != TK_EOF || ds.tag_kind != TK_EOF || ds.saw_typedef_name) {
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
    psx_ctx_get_type_info(param_type_kind, NULL, &out->elem_size);
    if (param_type_kind == TK_FLOAT) out->fp_kind = TK_FLOAT_KIND_FLOAT;
    else if (param_type_kind == TK_DOUBLE) out->fp_kind = TK_FLOAT_KIND_DOUBLE;
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
    if (psx_ctx_find_typedef_name_ex3(id->str, id->len, NULL, &td_elem_size, &td_fp_kind,
                                      &td_tag_kind, &td_tag_name, &td_tag_len, NULL, NULL, NULL, NULL,
                                      &td_is_array, &td_sizeof_size, &td_first_dim,
                                      out->typedef_array_dims, &td_dim_count, 8)) {
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
    }
    set_curtok(curtok()->next);
  }
}

static void parse_func_decl_spec(token_kind_t *ret_kind, tk_float_kind_t *ret_fp_kind,
                                 token_ident_t **ret_tag, int *ret_is_ptr) {
  *ret_kind = TK_EOF;
  *ret_fp_kind = TK_FLOAT_KIND_NONE;
  *ret_tag = NULL;
  *ret_is_ptr = 0;
  if (psx_ctx_is_tag_keyword(curtok()->kind)) {
    resolve_func_ret_tag_spec(ret_kind, ret_tag);
    parse_pointer_suffix_flags(ret_is_ptr); // skip optional pointer(s)
    return;
  }

  *ret_kind = psx_consume_type_kind(); // 通常の戻り値型（省略可）
  if (*ret_kind == TK_EOF && psx_ctx_is_typedef_name_token(curtok())) {
    resolve_func_ret_typedef(ret_kind, ret_fp_kind, ret_tag, ret_is_ptr);
  }
  *ret_fp_kind = fp_kind_for_type_kind_toplevel(*ret_kind);
  parse_pointer_suffix_flags(ret_is_ptr);
}

static void resolve_func_ret_tag_spec(token_kind_t *ret_kind, token_ident_t **ret_tag) {
  *ret_kind = curtok()->kind;
  set_curtok(curtok()->next);
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
    psx_diag_undefined_with_name(curtok(), diag_text_for(DIAG_TEXT_TAG_TYPE_SUFFIX), tag->str, tag->len);
  }
}

static void parse_pointer_suffix_flags(int *out_is_ptr) {
  while (curtok()->kind == TK_MUL) {
    set_curtok(curtok()->next);
    if (out_is_ptr) *out_is_ptr = 1;
  }
}

static void resolve_func_ret_typedef(token_kind_t *ret_kind, tk_float_kind_t *ret_fp_kind,
                                     token_ident_t **ret_tag, int *ret_is_ptr) {
  token_ident_t *td_id = (token_ident_t *)curtok();
  token_kind_t td_base = TK_EOF;
  int td_elem = 8;
  tk_float_kind_t td_fp = TK_FLOAT_KIND_NONE;
  token_kind_t td_tag = TK_EOF;
  char *td_tag_name = NULL;
  int td_tag_len = 0;
  int td_is_ptr = 0;
  psx_ctx_find_typedef_name(td_id->str, td_id->len, &td_base, &td_elem, &td_fp,
                            &td_tag, &td_tag_name, &td_tag_len, &td_is_ptr, NULL, NULL, NULL);
  set_curtok(curtok()->next);
  *ret_kind = td_base;
  *ret_fp_kind = td_fp;
  if (td_is_ptr) *ret_is_ptr = 1;
  if (td_tag != TK_EOF) {
    *ret_tag = calloc(1, sizeof(token_ident_t));
    (*ret_tag)->str = td_tag_name;
    (*ret_tag)->len = td_tag_len;
    *ret_kind = td_tag;
  }
}

static token_ident_t *parse_func_name_declarator_recursive(void) {
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
          if (parse_param_decl(&node_tmp, &nargs, &arg_cap)) has_unnamed_param = 1;
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
        if (parse_param_decl(&node_tmp, &nargs, &arg_cap) && !parsed_nested_inner_params) {
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
        while (!tk_consume(']')) set_curtok(curtok()->next);
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
        if (parse_param_decl(&node_tmp, &nargs, &arg_cap)) has_unnamed_param = 1;
        args = node_tmp.args;
        if (!tk_consume(',')) break;
        if (curtok()->kind == TK_RPAREN) {
          psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_PARAMETER));
        }
      }
      tk_expect(')');
    }
  }

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
  g_last_outer_declarator_is_ptr = 0;
  parse_func_decl_spec(&ret_kind, &ret_fp_kind, &ret_tag, &ret_is_ptr);
  if (ret_kind == TK_EOF) {
    diag_warn_tokf(DIAG_WARN_PARSER_IMPLICIT_INT_RETURN, curtok(),
                   "%s", diag_warn_message_for(DIAG_WARN_PARSER_IMPLICIT_INT_RETURN));
  }
  token_kind_t ret_token_kind = (ret_kind == TK_EOF) ? TK_INT : ret_kind;
  /* 戻り値型の unsigned 性を捕捉する (`unsigned` は ret_kind が TK_INT に潰れ
   * 符号性が落ちるため別管理)。parse_func_decl_spec 直後に読む (parse_func_declarator
   * が g_last_type_unsigned を変えるより前)。後段で関数名判明後に記録する。 */
  int ret_is_unsigned = !ret_is_ptr && psx_last_type_is_unsigned();
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
   * 関数の戻り型 (IR_TY_F32/F64) を決定し、callee が fp レジスタで返すために必要。 */
  node->base.fp_kind = ret_fp_kind;
  node->funcname = tok->str;
  node->funcname_len = tok->len;
  psx_ctx_define_function_name_with_ret(tok->str, tok->len,
                                         psx_expr_current_func_ret_struct_size());
  // float / double 戻り値型を記録 → call 経路で fcvtzs を挿入できるようにする
  if (ret_fp_kind != TK_FLOAT_KIND_NONE) {
    psx_ctx_set_function_ret_fp_kind(tok->str, tok->len, ret_fp_kind);
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
  if (ret_is_unsigned) psx_ctx_set_function_ret_unsigned(tok->str, tok->len, 1);
  // variadic 情報と固定引数数を記録。caller 側 codegen が register/stack 切替に使い、
  // build_unqualified_call が引数数チェックに使う。
  // 非 variadic 関数でも nargs_fixed を記録するため常に呼ぶ。
  psx_ctx_set_function_variadic(tok->str, tok->len, is_variadic ? 1 : 0, nargs);
  /* 仮引数 i の fp_kind を記録 → 呼び出し側で int→double 暗黙変換を挿入できる。
   * args[i] は parse_param_decl で fp_kind がセット済みの ND_LVAR。 */
  for (int i = 0; i < nargs && i < 16; i++) {
    tk_float_kind_t pfk = (tk_float_kind_t)(args[i] ? args[i]->fp_kind : 0);
    if (pfk != TK_FLOAT_KIND_NONE) {
      psx_ctx_set_function_param_fp_kind(tok->str, tok->len, i, pfk);
    }
  }
  /* struct/union を返す関数のタグを記録する。ポインタ返し (`struct N *get(void)`)
   * でも記録し、`get()->m` のメンバアクセスを解決できるようにする。ポインタ性は
   * psx_ctx_get_function_ret_is_pointer で別途参照される。 */
  if ((ret_kind == TK_STRUCT || ret_kind == TK_UNION) && ret_tag) {
    psx_ctx_set_function_ret_tag(tok->str, tok->len, ret_kind, ret_tag->str, ret_tag->len);
  }
  psx_expr_set_current_funcname(tok->str, tok->len); // __func__ 用
  node->args = args;
  node->is_variadic = is_variadic;
  node->nargs = nargs;
  // 可変長引数関数: ローカル変数スペースを引数レジスタ保存領域の後ろに移動する
  if (node->is_variadic) {
    psx_decl_reserve_variadic_regs();
  }

  // 関数プロトタイプ宣言（本体なし）
  if (tk_consume(';')) {
    return NULL;
  }
  if (has_unnamed_param) {
    // 関数定義の仮引数では識別子必須。
    psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_PARAMETER));
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
