#include "parser.h"
#include "internal/arena.h"
#include "internal/node_utils.h"
#include "internal/semantic_ctx.h"
#include "internal/decl.h"
#include "internal/core.h"
#include "internal/diag.h"
#include "internal/dynarray.h"
#include "internal/expr.h"
#include "internal/loop_ctx.h"
#include "internal/stmt.h"
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
static int g_last_type_const_qualified = 0;
static int g_last_type_volatile_qualified = 0;
static int g_last_alignas_value = 0;
static int g_last_decl_is_extern = 0;
static int g_toplevel_decl_elem_size = 8;
static int g_toplevel_decl_is_extern = 0;
static int g_toplevel_decl_is_thread_local = 0;

static node_t *funcdef(void);
static void parse_toplevel_decl_after_type(void);
static void parse_toplevel_decl_spec(void);
static void parse_toplevel_tag_decl(void);
static void parse_toplevel_typedef_decl(void);
static token_ident_t *parse_toplevel_typedef_name_decl(int *is_ptr);
static token_ident_t *parse_toplevel_decl_name(int *is_ptr);
static token_ident_t *parse_decl_name_recursive(int *is_ptr, int require_name);
static token_ident_t *parse_member_decl_name_recursive_toplevel(int *is_ptr, int *out_has_func_suffix);
static int is_toplevel_function_signature(token_t *tok);
static int is_tag_return_function_signature(token_t *tok);
static int parse_tag_definition_body_toplevel(token_kind_t tag_kind, char *tag_name, int tag_len, int *out_size);
static void skip_balanced_group(token_kind_t lkind, token_kind_t rkind);
static token_ident_t *parse_param_declarator_name(int *out_is_array_declarator, int *out_is_pointer_declarator);
static token_ident_t *parse_param_declarator_name_recursive(int *out_is_array_declarator,
                                                            int *out_is_pointer_declarator);
static void parse_param_decl(node_func_t *node, int *nargs, int *arg_cap);
typedef struct {
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int struct_size;
  int elem_size;
} param_decl_spec_t;
static void parse_param_decl_spec(param_decl_spec_t *out);
static void parse_func_decl_spec(token_kind_t *ret_kind, tk_float_kind_t *ret_fp_kind,
                                 token_ident_t **ret_tag, int *ret_is_ptr);
static token_ident_t *parse_func_declarator(int *out_is_variadic, node_t ***out_args, int *out_nargs);
static void parse_static_assert_toplevel(void);
static long long parse_enum_const_expr_toplevel(void);
static long long parse_enum_const_conditional_toplevel(void);
static long long parse_enum_const_logor_toplevel(void);
static long long parse_enum_const_logand_toplevel(void);
static long long parse_enum_const_bitor_toplevel(void);
static long long parse_enum_const_bitxor_toplevel(void);
static long long parse_enum_const_bitand_toplevel(void);
static long long parse_enum_const_eq_toplevel(void);
static long long parse_enum_const_rel_toplevel(void);
static long long parse_enum_const_shift_toplevel(void);
static long long parse_enum_const_add_toplevel(void);
static long long parse_enum_const_mul_toplevel(void);
static long long parse_enum_const_unary_toplevel(void);
static long long parse_enum_const_primary_toplevel(void);
static token_t *skip_decl_prefix_lookahead(token_t *t);
static token_kind_t parse_atomic_type_specifier(void);
static int parse_array_size_constexpr_toplevel(void);
static int parse_alignas_value_toplevel(void);
static void make_anonymous_tag_name_toplevel(char **out_name, int *out_len);
static int anonymous_tag_seq_toplevel = 0;

static bool is_decl_prefix_token(token_kind_t k) {
  return k == TK_CONST || k == TK_VOLATILE || k == TK_EXTERN || k == TK_STATIC ||
         k == TK_AUTO || k == TK_REGISTER || k == TK_INLINE || k == TK_NORETURN ||
         k == TK_THREAD_LOCAL || k == TK_ALIGNAS || k == TK_ATOMIC;
}

static void make_anonymous_tag_name_toplevel(char **out_name, int *out_len) {
  int seq = anonymous_tag_seq_toplevel++;
  int len = snprintf(NULL, 0, "__anon_tag_top_%d", seq);
  char *name = calloc((size_t)len + 1, 1);
  snprintf(name, (size_t)len + 1, "__anon_tag_top_%d", seq);
  *out_name = name;
  *out_len = len;
}

static int g_last_type_atomic;
static int g_last_type_thread_local;

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
  while (is_decl_prefix_token(curtok()->kind)) {
    if (curtok()->kind == TK_CONST) g_last_type_const_qualified = 1;
    if (curtok()->kind == TK_VOLATILE) g_last_type_volatile_qualified = 1;
    if (curtok()->kind == TK_EXTERN) g_last_decl_is_extern = 1;
    if (curtok()->kind == TK_ALIGNAS) {
      set_curtok(curtok()->next);
      if (curtok()->kind != TK_LPAREN) {
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_ALIGNAS_LPAREN_REQUIRED));
      }
      int av = parse_alignas_value_toplevel();
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

static void skip_ptr_qualifiers(void) {
  while (curtok()->kind == TK_CONST || curtok()->kind == TK_VOLATILE || curtok()->kind == TK_RESTRICT) {
    set_curtok(curtok()->next);
  }
}

static void parse_static_assert_toplevel(void) {
  if (curtok()->kind != TK_STATIC_ASSERT) {
    diag_emit_tokf(DIAG_ERR_PARSER_STATIC_ASSERT_EXPECTED, curtok(), "%s",
                   diag_message_for(DIAG_ERR_PARSER_STATIC_ASSERT_EXPECTED));
  }
  set_curtok(curtok()->next);
  tk_expect('(');
  long long cond_val = parse_enum_const_expr_toplevel();
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
  while (t && is_decl_prefix_token(t->kind)) {
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
  if (token->kind != TK_ATOMIC) return TK_EOF;
  token = token->next;
  if (!tk_consume('(')) {
    // qualifier-form: "_Atomic int" は前置指定子として扱う
    return TK_EOF;
  }
  token_kind_t inner = psx_consume_type_kind();
  if (inner == TK_EOF) {
    psx_diag_ctx(token, "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_ATOMIC_TYPE_NAME_REQUIRED));
  }
  // Minimal support for derived declarators in _Atomic(type), e.g. _Atomic(int*).
  while (tk_consume('*')) {
    while (token->kind == TK_CONST || token->kind == TK_VOLATILE || token->kind == TK_RESTRICT) {
      token = token->next;
    }
  }
  tk_expect(')');
  return inner;
}

static int parse_array_size_constexpr_toplevel(void) {
  long long v = parse_enum_const_expr_toplevel();
  if (v <= 0) {
    psx_diag_ctx(token, "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
  }
  return (int)v;
}

static void parse_toplevel_decl_spec(void) {
  if (psx_ctx_is_typedef_name_token(token)) {
    token_ident_t *id = (token_ident_t *)token;
    token_kind_t td_base = TK_EOF;
    int td_elem = 8;
    tk_float_kind_t td_fp = TK_FLOAT_KIND_NONE;
    token_kind_t td_tag = TK_EOF;
    char *td_tag_name = NULL;
    int td_tag_len = 0;
    int td_is_ptr = 0;
    psx_ctx_find_typedef_name(id->str, id->len, &td_base, &td_elem, &td_fp,
                              &td_tag, &td_tag_name, &td_tag_len, &td_is_ptr);
    token = token->next;
    g_toplevel_decl_elem_size = td_elem;
    g_toplevel_decl_is_extern = 0;
    g_toplevel_decl_is_thread_local = 0;
    return;
  }

  token_kind_t tl_kind = psx_consume_type_kind();
  g_toplevel_decl_elem_size = 8;
  if (tl_kind != TK_EOF) psx_ctx_get_type_info(tl_kind, NULL, &g_toplevel_decl_elem_size);
  g_toplevel_decl_is_extern = g_last_decl_is_extern;
  g_toplevel_decl_is_thread_local = g_last_type_thread_local;
}

// program = funcdef*
node_t **ps_program_from(token_t *start) {
  tk_set_current_token(start);
  int cap = 16;
  node_t **codes = calloc(cap, sizeof(node_t*));
  int i = 0;
  while (!tk_at_eof()) {
    if (token->kind == TK_PRAGMA_PACK_PUSH) {
      pragma_pack_push((int)((token_num_int_t *)token)->val);
      token = token->next;
      continue;
    }
    if (token->kind == TK_PRAGMA_PACK_POP) {
      pragma_pack_pop();
      token = token->next;
      continue;
    }
    if (token->kind == TK_PRAGMA_PACK_SET) {
      pragma_pack_set((int)((token_num_int_t *)token)->val);
      token = token->next;
      continue;
    }
    if (token->kind == TK_PRAGMA_PACK_RESET) {
      pragma_pack_reset();
      token = token->next;
      continue;
    }
    if (token->kind == TK_STATIC_ASSERT) {
      parse_static_assert_toplevel();
      continue;
    }
    if (psx_ctx_is_tag_keyword(token->kind)) {
      if (!is_tag_return_function_signature(token)) {
        parse_toplevel_tag_decl();
        continue;
      }
      // struct/union Tag func(...) — 戻り値型がタグ型の関数定義: funcdef() へ fall through
    }
    if (token->kind == TK_TYPEDEF) {
      parse_toplevel_typedef_decl();
      continue;
    }
    if ((psx_ctx_is_type_token(token->kind) || is_decl_prefix_token(token->kind) || psx_ctx_is_typedef_name_token(token)) &&
        !is_toplevel_function_signature(token)) {
      parse_toplevel_decl_spec();
      parse_toplevel_decl_after_type();
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
  return codes;
}

node_t **ps_program(void) {
  return ps_program_from(tk_get_current_token());
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
  if (!t || t->kind != TK_IDENT) return 0;
  return t->next && t->next->kind == TK_LPAREN;
}

// struct/union Tag [*] ident ( のパターンを検出（戻り値型がタグ型の関数定義）
static int is_tag_return_function_signature(token_t *tok) {
  if (!tok || !psx_ctx_is_tag_keyword(tok->kind)) return 0;
  token_t *t = tok->next; // skip struct/union keyword
  if (!t || t->kind != TK_IDENT) return 0;
  t = t->next; // skip tag name
  while (t && t->kind == TK_MUL) t = t->next; // skip optional pointer(s)
  if (!t || t->kind != TK_IDENT) return 0;
  return t->next && t->next->kind == TK_LPAREN;
}

static void parse_toplevel_declarator_list(void) {
  for (;;) {
    int is_ptr = 0;
    while (tk_consume('*')) {
      is_ptr = 1;
      skip_ptr_qualifiers();
    }
    token_ident_t *name = parse_toplevel_decl_name(&is_ptr);
    if (!name) {
      psx_diag_ctx(token, "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED));
    }
    // 配列宣言子を消費し、配列サイズを記録
    int arr_total = 1;
    int is_array = 0;
    while (tk_consume('[')) {
      arr_total *= parse_array_size_constexpr_toplevel();
      is_array = 1;
      tk_expect(']');
    }
    if (!g_toplevel_decl_is_extern) {
      // グローバル変数テーブルに登録
      global_var_t *gv = calloc(1, sizeof(global_var_t));
      gv->name = name->str;
      gv->name_len = name->len;
      gv->type_size = is_ptr ? 8 : g_toplevel_decl_elem_size * arr_total;
      gv->deref_size = g_toplevel_decl_elem_size;
      gv->is_array = is_array;
      gv->is_thread_local = g_toplevel_decl_is_thread_local;
      if (tk_consume('=')) {
        node_t *init_expr = psx_expr_assign();
        if (init_expr && init_expr->kind == ND_NUM) {
          gv->has_init = 1;
          gv->init_val = ((node_num_t *)init_expr)->val;
        } else if (init_expr && init_expr->kind == ND_ADDR &&
                   init_expr->lhs && init_expr->lhs->kind == ND_GVAR) {
          node_gvar_t *ref = (node_gvar_t *)init_expr->lhs;
          gv->has_init = 1;
          gv->init_symbol = ref->name;
          gv->init_symbol_len = ref->name_len;
        }
      }
      gv->next = global_vars;
      global_vars = gv;
    } else {
      // extern宣言: テーブルに登録（is_extern_decl=1）
      int found = 0;
      for (global_var_t *gv = global_vars; gv; gv = gv->next) {
        if (gv->name_len == name->len && memcmp(gv->name, name->str, (size_t)name->len) == 0) {
          found = 1; break;
        }
      }
      if (!found) {
        global_var_t *gv = calloc(1, sizeof(global_var_t));
        gv->name = name->str;
        gv->name_len = name->len;
        gv->type_size = is_ptr ? 8 : g_toplevel_decl_elem_size;
        gv->deref_size = g_toplevel_decl_elem_size;
        gv->is_extern_decl = 1;
        gv->next = global_vars;
        global_vars = gv;
      }
      if (tk_consume('=')) psx_expr_assign(); // 初期化子（extern宣言では通常ないが消費する）
    }
    if (!tk_consume(',')) break;
  }
}

static token_ident_t *parse_toplevel_decl_name(int *is_ptr) {
  token_ident_t *name = parse_decl_name_recursive(is_ptr, 1);
  while (token->kind == TK_LPAREN) {
    int depth = 1;
    token = token->next;
    while (depth > 0) {
      if (token->kind == TK_EOF) {
        diag_emit_tokf(DIAG_ERR_PARSER_MISSING_FUNC_DECL_RPAREN, token, "%s",
                       diag_message_for(DIAG_ERR_PARSER_MISSING_FUNC_DECL_RPAREN));
      }
      if (token->kind == TK_LPAREN) depth++;
      else if (token->kind == TK_RPAREN) depth--;
      token = token->next;
    }
  }
  return name;
}

static token_ident_t *parse_toplevel_typedef_name_decl(int *is_ptr) {
  token_ident_t *name = parse_decl_name_recursive(is_ptr, 1);
  while (token->kind == TK_LPAREN) {
    int depth = 1;
    token = token->next;
    while (depth > 0) {
      if (token->kind == TK_EOF) {
        diag_emit_tokf(DIAG_ERR_PARSER_MISSING_FUNC_DECL_RPAREN, token, "%s",
                       diag_message_for(DIAG_ERR_PARSER_MISSING_FUNC_DECL_RPAREN));
      }
      if (token->kind == TK_LPAREN) depth++;
      else if (token->kind == TK_RPAREN) depth--;
      token = token->next;
    }
  }
  return name;
}

static token_ident_t *parse_decl_name_recursive(int *is_ptr, int require_name) {
  while (tk_consume('*')) {
    *is_ptr = 1;
    skip_ptr_qualifiers();
  }
  token_ident_t *name = NULL;
  if (tk_consume('(')) {
    name = parse_decl_name_recursive(is_ptr, require_name);
    tk_expect(')');
  } else {
    name = tk_consume_ident();
    if (!name && require_name) {
      diag_emit_tokf(DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED, token, "%s",
                     diag_message_for(DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED));
    }
  }
  return name;
}

static token_ident_t *parse_member_decl_name_recursive_toplevel(int *is_ptr, int *out_has_func_suffix) {
  while (tk_consume('*')) {
    *is_ptr = 1;
    skip_ptr_qualifiers();
  }
  token_ident_t *name = NULL;
  if (tk_consume('(')) {
    name = parse_member_decl_name_recursive_toplevel(is_ptr, out_has_func_suffix);
    tk_expect(')');
  } else {
    name = tk_consume_ident();
  }
  while (token->kind == TK_LPAREN) {
    if (out_has_func_suffix) *out_has_func_suffix = 1;
    skip_balanced_group(TK_LPAREN, TK_RPAREN);
  }
  return name;
}

static void parse_toplevel_decl_after_type(void) {
  parse_toplevel_declarator_list();
  tk_expect(';');
}

static void parse_toplevel_typedef_decl(void) {
  if (token->kind != TK_TYPEDEF) {
    psx_diag_ctx(token, "typedef", "%s",
                 diag_message_for(DIAG_ERR_PARSER_TYPEDEF_KEYWORD_REQUIRED));
  }
  token = token->next;
  token_kind_t base_kind = TK_EOF;
  int elem_size = 8;
  tk_float_kind_t fp_kind = TK_FLOAT_KIND_NONE;
  token_kind_t tag_kind = TK_EOF;
  char *tag_name = NULL;
  int tag_len = 0;
  int is_ptr_base = 0;

  token_kind_t builtin_kind = psx_consume_type_kind();
  if (builtin_kind != TK_EOF) {
    base_kind = builtin_kind;
    psx_ctx_get_type_info(builtin_kind, NULL, &elem_size);
    if (builtin_kind == TK_FLOAT) fp_kind = TK_FLOAT_KIND_FLOAT;
    else if (builtin_kind == TK_DOUBLE) fp_kind = TK_FLOAT_KIND_DOUBLE;
  } else if (psx_ctx_is_tag_keyword(token->kind)) {
    base_kind = token->kind;
    tag_kind = token->kind;
    token = token->next;
    token_ident_t *tag = tk_consume_ident();
    if (!tag) psx_diag_missing(token, diag_text_for(DIAG_TEXT_TAG_NAME));
    tag_name = tag->str;
    tag_len = tag->len;
    if (tk_consume('{')) {
      int member_count = 0;
      int tag_size = 0;
      member_count = parse_tag_definition_body_toplevel(tag_kind, tag_name, tag_len, &tag_size);
      psx_ctx_define_tag_type_with_layout(tag_kind, tag_name, tag_len, member_count, tag_size);
    } else if (!psx_ctx_has_tag_type(tag_kind, tag_name, tag_len)) {
      psx_diag_undefined_with_name(token, diag_text_for(DIAG_TEXT_TAG_TYPE_SUFFIX), tag_name, tag_len);
    }
    elem_size = psx_ctx_get_tag_size(tag_kind, tag_name, tag_len);
  } else if (psx_ctx_is_typedef_name_token(token)) {
    token_ident_t *id = (token_ident_t *)token;
    psx_ctx_find_typedef_name(id->str, id->len, &base_kind, &elem_size, &fp_kind, &tag_kind, &tag_name, &tag_len, &is_ptr_base);
    token = token->next;
  } else {
    diag_emit_tokf(DIAG_ERR_PARSER_TYPE_NAME_REQUIRED, token, "%s",
                   diag_message_for(DIAG_ERR_PARSER_TYPE_NAME_REQUIRED));
  }

  for (;;) {
    int is_ptr = is_ptr_base;
    while (tk_consume('*')) {
      is_ptr = 1;
      skip_ptr_qualifiers();
    }
    token_ident_t *name = parse_toplevel_typedef_name_decl(&is_ptr);
    while (tk_consume('[')) {
      (void)parse_array_size_constexpr_toplevel();
      tk_expect(']');
    }
    psx_ctx_define_typedef_name(name->str, name->len, base_kind, elem_size, fp_kind, tag_kind, tag_name, tag_len, is_ptr);
    if (!tk_consume(',')) break;
  }
  tk_expect(';');
}

static int parse_struct_or_union_members_layout_toplevel(token_kind_t tag_kind, char *tag_name, int tag_len, int *out_size) {
  int member_count = 0;
  int current_off = 0;
  int union_size = 0;
  int agg_align = 1;
  // ビットフィールドパッキング状態
  int bf_storage_offset = -1;   // 現在のストレージユニット先頭バイトオフセット（-1: なし）
  int bf_storage_type_size = 0; // ストレージユニットの型サイズ
  int bf_bits_used = 0;         // ストレージユニット内使用済みビット数
  #define ALIGN_UP(v, a) (((v) + ((a) - 1)) / (a) * (a))
  while (!tk_consume('}')) {
    int elem_size = 8;
    int is_signed_type = 1; // default: signed
    token_kind_t member_tag_kind = TK_EOF;
    char *member_tag_name = NULL;
    int member_tag_len = 0;
    int member_alignas = 0;
    // skip leading qualifiers (const, volatile, _Alignas)
    while (token->kind == TK_CONST || token->kind == TK_VOLATILE || token->kind == TK_ALIGNAS) {
      if (token->kind == TK_ALIGNAS) {
        token = token->next;
        int av = parse_alignas_value_toplevel();
        if (av > member_alignas) member_alignas = av;
      } else {
        token = token->next;
      }
    }
    if (psx_ctx_is_type_token(token->kind)) {
      is_signed_type = (token->kind != TK_UNSIGNED);
      psx_ctx_get_type_info(token->kind, NULL, &elem_size);
      token = token->next;
      // handle "unsigned int", "signed int", etc.
      while (psx_ctx_is_type_token(token->kind)) {
        if (token->kind != TK_UNSIGNED && token->kind != TK_SIGNED) {
          psx_ctx_get_type_info(token->kind, NULL, &elem_size);
        }
        token = token->next;
      }
    } else if (psx_ctx_is_tag_keyword(token->kind)) {
      member_tag_kind = token->kind;
      token = token->next;
      token_ident_t *nested_tag = tk_consume_ident();
      if (nested_tag) {
        member_tag_name = nested_tag->str;
        member_tag_len = nested_tag->len;
      } else if (token->kind == TK_LBRACE) {
        make_anonymous_tag_name_toplevel(&member_tag_name, &member_tag_len);
      } else {
        psx_diag_missing(token, diag_text_for(DIAG_TEXT_TAG_NAME));
      }
      if (tk_consume('{')) {
        int nested_n = 0;
        int nested_sz = 0;
        nested_n = parse_tag_definition_body_toplevel(member_tag_kind, member_tag_name, member_tag_len, &nested_sz);
        psx_ctx_define_tag_type_with_layout(member_tag_kind, member_tag_name, member_tag_len, nested_n, nested_sz);
      } else if (!psx_ctx_has_tag_type(member_tag_kind, member_tag_name, member_tag_len)) {
        // ポインタメンバの場合は不完全型（自己参照等）を許可する
        if (token->kind != TK_MUL) {
          psx_diag_undefined_with_name(token, diag_text_for(DIAG_TEXT_TAG_TYPE_SUFFIX), member_tag_name, member_tag_len);
        }
      }
      if (psx_ctx_has_tag_type(member_tag_kind, member_tag_name, member_tag_len)) {
        elem_size = psx_ctx_get_tag_size(member_tag_kind, member_tag_name, member_tag_len);
      }
      if (elem_size <= 0 && token->kind != TK_MUL) {
        psx_diag_ctx(token, "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_INCOMPLETE_MEMBER_FORBIDDEN));
      }
    } else {
      psx_diag_ctx(token, "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_MEMBER_TYPE_REQUIRED));
    }

    for (;;) {
      int is_ptr = 0;
      int has_func_suffix = 0;
      token_ident_t *member = parse_member_decl_name_recursive_toplevel(&is_ptr, &has_func_suffix);
      int has_member_name = member != NULL;
      if (!has_member_name && !(member_tag_kind == TK_STRUCT || member_tag_kind == TK_UNION)
          && token->kind != TK_COLON) {
        psx_diag_missing(token, diag_text_for(DIAG_TEXT_MEMBER_NAME));
      }
      if (has_func_suffix && !is_ptr) {
        psx_diag_ctx(token, "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_FUNCTION_MEMBER_FORBIDDEN));
      }

      int bit_width = 0;
      int bit_field_offset_in_storage = 0;
      if (token->kind == TK_COLON) {
        token = token->next; // consume ':'
        long long bw = parse_enum_const_expr_toplevel();
        if (bw < 0) bw = 0;
        bit_width = (int)bw;
        int storage_size = is_ptr ? 8 : (elem_size > 0 ? elem_size : 4);
        if (storage_size > 4) storage_size = 4; // max 32-bit storage unit for bit fields
        int storage_bits = storage_size * 8;
        if (bit_width == 0) {
          // 幅0 → 次のストレージユニット境界に強制アライン
          bf_storage_offset = -1;
          bf_bits_used = 0;
          if (tag_kind != TK_UNION)
            current_off = ALIGN_UP(current_off, storage_size);
          if (!has_member_name) { if (!tk_consume(',')) break; continue; }
        }
        if (tag_kind != TK_UNION) {
          // 現在のストレージユニットに収まるか判定
          if (bf_storage_offset < 0
              || bf_storage_type_size != storage_size
              || bf_bits_used + bit_width > storage_bits) {
            // 新しいストレージユニットを開始
            current_off = ALIGN_UP(current_off, storage_size);
            bf_storage_offset = current_off;
            bf_storage_type_size = storage_size;
            bf_bits_used = 0;
            current_off += storage_size;
            if (storage_size > agg_align) agg_align = storage_size;
          }
          bit_field_offset_in_storage = bf_bits_used;
          bf_bits_used += bit_width;
        } else {
          // union: 全メンバoffset=0, bit_offset=0
          bit_field_offset_in_storage = 0;
          bf_storage_offset = 0;
          bf_storage_type_size = storage_size;
          if (storage_size > union_size) union_size = storage_size;
          if (storage_size > agg_align) agg_align = storage_size;
        }
        if (has_member_name) {
          int storage_type_size = bf_storage_type_size > 0 ? bf_storage_type_size : 4;
          psx_ctx_add_tag_member_bf(tag_kind, tag_name, tag_len,
                                    member->str, member->len,
                                    tag_kind == TK_UNION ? 0 : bf_storage_offset,
                                    storage_type_size, 0, 0,
                                    TK_EOF, NULL, 0, 0,
                                    bit_width, bit_field_offset_in_storage, is_signed_type);
          member_count++;
        }
        if (!tk_consume(',')) break;
        continue; // next declarator in this declaration
      }

      // 通常（非ビットフィールド）メンバ → ビットフィールド状態をリセット
      bf_storage_offset = -1;
      bf_bits_used = 0;

      int arr_size = 1;
      int is_flex_array = 0;
      while (tk_consume('[')) {
        if (!has_member_name) psx_diag_missing(token, diag_text_for(DIAG_TEXT_MEMBER_NAME));
        if (token->kind == TK_RBRACKET) {
          // フレキシブル配列メンバー: int data[];
          is_flex_array = 1;
          arr_size = 0;
        } else {
          arr_size *= parse_array_size_constexpr_toplevel();
        }
        tk_expect(']');
      }
      int total_size = is_flex_array ? 0 : (is_ptr ? 8 : elem_size * arr_size);
      int deref_size = is_ptr ? elem_size : 0;
      int member_align = is_ptr ? 8 : elem_size;
      if (member_align <= 0) member_align = 1;
      if (member_align > 8) member_align = 8;
      int pack_align = pragma_pack_current;
      if (pack_align > 0 && pack_align < member_align) member_align = pack_align;
      if (member_alignas > member_align) member_align = member_alignas;
      if (member_align > agg_align) agg_align = member_align;
      int off = 0;
      if (tag_kind == TK_UNION) {
        off = 0;
      } else {
        current_off = ALIGN_UP(current_off, member_align);
        off = current_off;
      }
      char *member_name = has_member_name ? member->str : "";
      int member_len = has_member_name ? member->len : 0;
      int member_array_len = (is_ptr || arr_size <= 1) ? 0 : arr_size;
      if (has_member_name || (member_tag_kind == TK_STRUCT || member_tag_kind == TK_UNION)) {
        psx_ctx_add_tag_member(tag_kind, tag_name, tag_len,
                               member_name, member_len, off, is_ptr ? 8 : elem_size, deref_size,
                               member_array_len,
                               member_tag_kind, member_tag_name, member_tag_len, is_ptr ? 1 : 0);
        member_count++;
      }
      if (tag_kind == TK_UNION) {
        if (total_size > union_size) union_size = total_size;
      } else {
        current_off += total_size;
      }
      if (!has_member_name && tk_consume(',')) psx_diag_missing(token, diag_text_for(DIAG_TEXT_MEMBER_NAME));
      if (!tk_consume(',')) break;
    }
    tk_expect(';');
  }
  *out_size = (tag_kind == TK_UNION) ? ALIGN_UP(union_size, agg_align) : ALIGN_UP(current_off, agg_align);
  #undef ALIGN_UP
  return member_count;
}

static long long parse_enum_const_expr_toplevel(void) { return parse_enum_const_conditional_toplevel(); }

static long long parse_enum_const_conditional_toplevel(void) {
  long long cond = parse_enum_const_logor_toplevel();
  if (!tk_consume('?')) return cond;
  long long then_v = parse_enum_const_expr_toplevel();
  tk_expect(':');
  long long else_v = parse_enum_const_conditional_toplevel();
  return cond ? then_v : else_v;
}

static long long parse_enum_const_logor_toplevel(void) {
  long long v = parse_enum_const_logand_toplevel();
  while (token->kind == TK_OROR) {
    token = token->next;
    long long r = parse_enum_const_logand_toplevel();
    v = (v || r) ? 1 : 0;
  }
  return v;
}

static long long parse_enum_const_logand_toplevel(void) {
  long long v = parse_enum_const_bitor_toplevel();
  while (token->kind == TK_ANDAND) {
    token = token->next;
    long long r = parse_enum_const_bitor_toplevel();
    v = (v && r) ? 1 : 0;
  }
  return v;
}

static long long parse_enum_const_bitor_toplevel(void) {
  long long v = parse_enum_const_bitxor_toplevel();
  while (token->kind == TK_PIPE) {
    token = token->next;
    long long r = parse_enum_const_bitxor_toplevel();
    v |= r;
  }
  return v;
}

static long long parse_enum_const_bitxor_toplevel(void) {
  long long v = parse_enum_const_bitand_toplevel();
  while (token->kind == TK_CARET) {
    token = token->next;
    long long r = parse_enum_const_bitand_toplevel();
    v ^= r;
  }
  return v;
}

static long long parse_enum_const_bitand_toplevel(void) {
  long long v = parse_enum_const_eq_toplevel();
  while (token->kind == TK_AMP) {
    token = token->next;
    long long r = parse_enum_const_eq_toplevel();
    v &= r;
  }
  return v;
}

static long long parse_enum_const_eq_toplevel(void) {
  long long v = parse_enum_const_rel_toplevel();
  while (token->kind == TK_EQEQ || token->kind == TK_NEQ) {
    token_kind_t op = token->kind;
    token = token->next;
    long long r = parse_enum_const_rel_toplevel();
    v = (op == TK_EQEQ) ? (v == r) : (v != r);
  }
  return v;
}

static long long parse_enum_const_rel_toplevel(void) {
  long long v = parse_enum_const_shift_toplevel();
  while (token->kind == TK_LT || token->kind == TK_LE || token->kind == TK_GT || token->kind == TK_GE) {
    token_kind_t op = token->kind;
    token = token->next;
    long long r = parse_enum_const_shift_toplevel();
    switch (op) {
      case TK_LT: v = (v < r); break;
      case TK_LE: v = (v <= r); break;
      case TK_GT: v = (v > r); break;
      default: v = (v >= r); break;
    }
  }
  return v;
}

static long long parse_enum_const_shift_toplevel(void) {
  long long v = parse_enum_const_add_toplevel();
  while (token->kind == TK_SHL || token->kind == TK_SHR) {
    token_kind_t op = token->kind;
    token = token->next;
    long long r = parse_enum_const_add_toplevel();
    v = (op == TK_SHL) ? (v << r) : (v >> r);
  }
  return v;
}

static long long parse_enum_const_add_toplevel(void) {
  long long v = parse_enum_const_mul_toplevel();
  while (token->kind == TK_PLUS || token->kind == TK_MINUS) {
    token_kind_t op = token->kind;
    token = token->next;
    long long r = parse_enum_const_mul_toplevel();
    v = (op == TK_PLUS) ? (v + r) : (v - r);
  }
  return v;
}

static long long parse_enum_const_mul_toplevel(void) {
  long long v = parse_enum_const_unary_toplevel();
  while (token->kind == TK_MUL || token->kind == TK_DIV || token->kind == TK_MOD) {
    token_kind_t op = token->kind;
    token = token->next;
    long long r = parse_enum_const_unary_toplevel();
    if (op == TK_MUL) v *= r;
    else if (op == TK_DIV) v /= r;
    else v %= r;
  }
  return v;
}

static long long parse_enum_const_unary_toplevel(void) {
  if (token->kind == TK_PLUS) {
    token = token->next;
    return parse_enum_const_unary_toplevel();
  }
  if (token->kind == TK_MINUS) {
    token = token->next;
    return -parse_enum_const_unary_toplevel();
  }
  if (token->kind == TK_TILDE) {
    token = token->next;
    return ~parse_enum_const_unary_toplevel();
  }
  if (token->kind == TK_BANG) {
    token = token->next;
    return !parse_enum_const_unary_toplevel();
  }
  if (token->kind == TK_SIZEOF) {
    token = token->next;
    if (token->kind == TK_LPAREN) {
      token = token->next;
      int sz = 8;
      if (psx_ctx_is_type_token(token->kind) || psx_ctx_is_tag_keyword(token->kind)) {
        psx_ctx_get_type_info(token->kind, NULL, &sz);
        if (psx_ctx_is_tag_keyword(token->kind)) {
          token_kind_t tk = token->kind;
          token = token->next;
          token_ident_t *tag = tk_consume_ident();
          if (tag && psx_ctx_has_tag_type(tk, tag->str, tag->len)) {
            sz = psx_ctx_get_tag_size(tk, tag->str, tag->len);
          }
        } else {
          token = token->next;
          while (psx_ctx_is_type_token(token->kind)) token = token->next;
        }
        while (token->kind == TK_MUL) { sz = 8; token = token->next; }
      }
      tk_expect(')');
      return sz;
    }
    return parse_enum_const_unary_toplevel();
  }
  return parse_enum_const_primary_toplevel();
}

static long long parse_enum_const_primary_toplevel(void) {
  if (token->kind == TK_LPAREN) {
    token = token->next;
    long long v = parse_enum_const_expr_toplevel();
    tk_expect(')');
    return v;
  }
  token_ident_t *id = tk_consume_ident();
  if (id) {
    long long v = 0;
    if (!psx_ctx_find_enum_const(id->str, id->len, &v)) {
      psx_diag_ctx(token, "enum", diag_message_for(DIAG_ERR_PARSER_ENUM_CONST_UNDEFINED),
                   id->len, id->str);
    }
    return v;
  }
  return tk_expect_number();
}

static int parse_enum_members_toplevel(void) {
  int member_count = 0;
  long long next_value = 0;
  while (!tk_consume('}')) {
    token_ident_t *enumerator = tk_consume_ident();
    if (!enumerator) psx_diag_missing(token, diag_text_for(DIAG_TEXT_ENUMERATOR_NAME));
    long long value = next_value;
    member_count++;
    if (tk_consume('=')) {
      value = parse_enum_const_expr_toplevel();
    }
    psx_ctx_define_enum_const(enumerator->str, enumerator->len, value);
    next_value = value + 1;
    if (tk_consume('}')) break;
    tk_expect(',');
    if (tk_consume('}')) break;
  }
  return member_count;
}

static int parse_tag_definition_body_toplevel(token_kind_t tag_kind, char *tag_name, int tag_len, int *out_size) {
  if (tag_kind == TK_ENUM) {
    if (out_size) *out_size = 4;
    return parse_enum_members_toplevel();
  }
  return parse_struct_or_union_members_layout_toplevel(tag_kind, tag_name, tag_len, out_size);
}

static void parse_toplevel_tag_decl(void) {
  token_kind_t tag_kind = token->kind;
  token = token->next;
  token_ident_t *tag = tk_consume_ident();
  // 匿名タグ（enum { A=1 }; など）: タグ名なしで '{' が来る場合
  if (!tag && token->kind != TK_LBRACE) {
    psx_diag_missing(token, diag_text_for(DIAG_TEXT_TAG_NAME));
  }
  static int anon_tag_counter_tl = 0;
  char anon_buf[32];
  char *tag_name = tag ? tag->str : anon_buf;
  int tag_len = tag ? tag->len : 0;
  if (!tag) {
    tag_len = snprintf(anon_buf, sizeof(anon_buf), "__anon_tl_%d", anon_tag_counter_tl++);
  }

  if (tk_consume('{')) {
    int member_count = 0;
    int tag_size = 0;
    member_count = parse_tag_definition_body_toplevel(tag_kind, tag_name, tag_len, &tag_size);
    psx_ctx_define_tag_type_with_layout(tag_kind, tag_name, tag_len, member_count, tag_size);
    if (tk_consume(';')) return;
    parse_toplevel_declarator_list();
    tk_expect(';');
    return;
  }
  if (tk_consume(';')) {
    psx_ctx_define_tag_type(tag_kind, tag_name, tag_len);
    return;
  }
  if (!psx_ctx_has_tag_type(tag_kind, tag_name, tag_len)) {
    psx_diag_undefined_with_name(token, diag_text_for(DIAG_TEXT_TAG_TYPE_SUFFIX), tag_name, tag_len);
  }
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

// consume_type: 型キーワードがあれば読み進め、そのトークン種別を返す（0=型なし）
token_kind_t psx_consume_type_kind(void) {
  g_last_type_unsigned = 0;
  g_last_type_complex = 0;
  g_last_type_atomic = 0;
  g_last_type_thread_local = 0;
  skip_cv_qualifiers();
  if (token->kind == TK_ATOMIC && token->next && token->next->kind == TK_LPAREN) {
    g_last_type_atomic = 1;
    token_kind_t inner = parse_atomic_type_specifier();
    if (inner != TK_EOF) return inner;
  }
  // qualifier-form: _Atomic int x;
  if (token->kind == TK_ATOMIC) {
    g_last_type_atomic = 1;
    token = token->next;
  }
  token_t *start = token;
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
    token_kind_t k = token->kind;
    if (k == TK_COMPLEX) {
      if (saw_complex || saw_imaginary || saw_void || saw_char || saw_short || saw_int || saw_bool) {
        diag_emit_tokf(DIAG_ERR_PARSER_INVALID_TYPE_SPEC, token, "%s", diag_message_for(DIAG_ERR_PARSER_INVALID_TYPE_SPEC));
      }
      saw_complex = 1;
      token = token->next;
      continue;
    }
    if (k == TK_IMAGINARY) {
      if (saw_complex || saw_imaginary || saw_void || saw_char || saw_short || saw_int || saw_bool) {
        diag_emit_tokf(DIAG_ERR_PARSER_INVALID_TYPE_SPEC, token, "%s", diag_message_for(DIAG_ERR_PARSER_INVALID_TYPE_SPEC));
      }
      saw_imaginary = 1;
      token = token->next;
      continue;
    }
    if (k == TK_SIGNED) {
      if (saw_signed || saw_unsigned || saw_char || saw_short || long_count || saw_int || saw_void || saw_float || saw_double || saw_bool) {
        diag_emit_tokf(DIAG_ERR_PARSER_INVALID_TYPE_SPEC, token, "%s", diag_message_for(DIAG_ERR_PARSER_INVALID_TYPE_SPEC));
      }
      saw_signed = 1;
      token = token->next;
      continue;
    }
    if (k == TK_UNSIGNED) {
      if (saw_signed || saw_unsigned || saw_char || saw_short || long_count || saw_int || saw_void || saw_float || saw_double || saw_bool) {
        diag_emit_tokf(DIAG_ERR_PARSER_INVALID_TYPE_SPEC, token, "%s", diag_message_for(DIAG_ERR_PARSER_INVALID_TYPE_SPEC));
      }
      saw_unsigned = 1;
      token = token->next;
      continue;
    }
    if (k == TK_LONG) {
      if (saw_char || saw_short || saw_void || saw_float || saw_bool || long_count >= 2) {
        diag_emit_tokf(DIAG_ERR_PARSER_INVALID_TYPE_SPEC, token, "%s", diag_message_for(DIAG_ERR_PARSER_INVALID_TYPE_SPEC));
      }
      long_count++;
      token = token->next;
      continue;
    }
    if (k == TK_SHORT) {
      if (saw_char || saw_short || long_count || saw_void || saw_float || saw_double || saw_bool) {
        diag_emit_tokf(DIAG_ERR_PARSER_INVALID_TYPE_SPEC, token, "%s", diag_message_for(DIAG_ERR_PARSER_INVALID_TYPE_SPEC));
      }
      saw_short = 1;
      token = token->next;
      continue;
    }
    if (k == TK_INT) {
      if (saw_int || saw_char || saw_void || saw_float || saw_double || saw_bool) {
        diag_emit_tokf(DIAG_ERR_PARSER_INVALID_TYPE_SPEC, token, "%s", diag_message_for(DIAG_ERR_PARSER_INVALID_TYPE_SPEC));
      }
      saw_int = 1;
      token = token->next;
      continue;
    }
    if (k == TK_CHAR) {
      if (saw_char || saw_short || long_count || saw_int || saw_void || saw_float || saw_double || saw_bool) {
        diag_emit_tokf(DIAG_ERR_PARSER_INVALID_TYPE_SPEC, token, "%s", diag_message_for(DIAG_ERR_PARSER_INVALID_TYPE_SPEC));
      }
      saw_char = 1;
      token = token->next;
      continue;
    }
    if (k == TK_VOID) {
      if (saw_signed || saw_unsigned || saw_char || saw_short || long_count || saw_int || saw_float || saw_double || saw_bool) {
        diag_emit_tokf(DIAG_ERR_PARSER_INVALID_TYPE_SPEC, token, "%s", diag_message_for(DIAG_ERR_PARSER_INVALID_TYPE_SPEC));
      }
      saw_void = 1;
      token = token->next;
      continue;
    }
    if (k == TK_FLOAT) {
      if (saw_signed || saw_unsigned || saw_char || saw_short || long_count || saw_int || saw_void || saw_double || saw_bool) {
        diag_emit_tokf(DIAG_ERR_PARSER_INVALID_TYPE_SPEC, token, "%s", diag_message_for(DIAG_ERR_PARSER_INVALID_TYPE_SPEC));
      }
      saw_float = 1;
      token = token->next;
      continue;
    }
    if (k == TK_DOUBLE) {
      if (saw_signed || saw_unsigned || saw_char || saw_short || saw_int || saw_void || saw_float || saw_bool) {
        diag_emit_tokf(DIAG_ERR_PARSER_INVALID_TYPE_SPEC, token, "%s", diag_message_for(DIAG_ERR_PARSER_INVALID_TYPE_SPEC));
      }
      saw_double = 1;
      token = token->next;
      continue;
    }
    if (k == TK_BOOL) {
      if (saw_signed || saw_unsigned || saw_char || saw_short || long_count || saw_int || saw_void || saw_float || saw_double) {
        diag_emit_tokf(DIAG_ERR_PARSER_INVALID_TYPE_SPEC, token, "%s", diag_message_for(DIAG_ERR_PARSER_INVALID_TYPE_SPEC));
      }
      saw_bool = 1;
      token = token->next;
      continue;
    }
    // 後置 cv 修飾子（int const, volatile int const など）
    if (k == TK_CONST) {
      g_last_type_const_qualified = 1;
      token = token->next;
      continue;
    }
    if (k == TK_VOLATILE) {
      g_last_type_volatile_qualified = 1;
      token = token->next;
      continue;
    }
    if (k == TK_RESTRICT) {
      token = token->next;
      continue;
    }
    if (k == TK_ATOMIC) {
      g_last_type_atomic = 1;
      token = token->next;
      continue;
    }
    break;
  }

  if (token == start) return TK_EOF;
  g_last_type_unsigned = saw_unsigned;
  g_last_type_complex = saw_complex;
  if ((saw_complex || saw_imaginary) && !(saw_float || saw_double)) {
    diag_emit_tokf(DIAG_ERR_PARSER_INVALID_CONTEXT, start,
                   "%s",
                   diag_message_for(DIAG_ERR_PARSER_COMPLEX_IMAGINARY_TYPE_REQUIRES_FLOAT));
  }
  if (saw_void) return TK_VOID;
  if (saw_float) return TK_FLOAT;
  if (saw_double) return TK_DOUBLE;
  if (saw_bool) return TK_BOOL;
  if (saw_char) return TK_CHAR;
  if (saw_short) return TK_SHORT;
  if (long_count > 0) return TK_LONG;
  return TK_INT;
}


// _Alignas( constant-expression | type-name )
static int parse_alignas_value_toplevel(void) {
  tk_expect('(');
  int val = 1;
  if (psx_ctx_is_type_token(token->kind) || psx_ctx_is_typedef_name_token(token)) {
    // _Alignas(type) — alignment = natural alignment of type
    int elem_size = 8;
    psx_ctx_get_type_info(token->kind, NULL, &elem_size);
    val = elem_size;
    while (token->kind != TK_RPAREN && token->kind != TK_EOF) token = token->next;
  } else {
    long long v = parse_enum_const_expr_toplevel();
    val = (v > 0) ? (int)v : 1;
  }
  tk_expect(')');
  return val;
}

static void skip_balanced_group(token_kind_t lkind, token_kind_t rkind) {
  if (token->kind != lkind) return;
  int depth = 0;
  while (token && token->kind != TK_EOF) {
    if (token->kind == lkind) depth++;
    else if (token->kind == rkind) {
      depth--;
      if (depth == 0) {
        token = token->next;
        return;
      }
    }
    token = token->next;
  }
  psx_diag_ctx(token, "param", "%s",
               diag_message_for(DIAG_ERR_PARSER_MISSING_CLOSING_PAREN));
}

static token_ident_t *parse_param_declarator_name(int *out_is_array_declarator, int *out_is_pointer_declarator) {
  if (out_is_array_declarator) *out_is_array_declarator = 0;
  if (out_is_pointer_declarator) *out_is_pointer_declarator = 0;
  token_ident_t *param = parse_param_declarator_name_recursive(out_is_array_declarator,
                                                               out_is_pointer_declarator);
  return param;
}

static token_ident_t *parse_param_declarator_name_recursive(int *out_is_array_declarator,
                                                            int *out_is_pointer_declarator) {
  while (tk_consume('*')) {
    if (out_is_pointer_declarator) *out_is_pointer_declarator = 1;
    skip_ptr_qualifiers();
  }
  token_ident_t *name = NULL;
  if (tk_consume('(')) {
    name = parse_param_declarator_name_recursive(out_is_array_declarator, out_is_pointer_declarator);
    tk_expect(')');
  } else {
    name = tk_consume_ident();
  }
  while (token->kind == TK_LPAREN || token->kind == TK_LBRACKET) {
    if (token->kind == TK_LPAREN) {
      skip_balanced_group(TK_LPAREN, TK_RPAREN);
    } else {
      if (out_is_array_declarator) *out_is_array_declarator = 1;
      skip_balanced_group(TK_LBRACKET, TK_RBRACKET);
    }
  }
  return name;
}

static void parse_param_decl(node_func_t *node, int *nargs, int *arg_cap) {
  param_decl_spec_t ds = {0};
  parse_param_decl_spec(&ds);
  // ポインタ修飾子を確認してから parse_param_declarator_name へ
  int param_is_ptr = 0;
  int param_is_array_declarator = 0;
  token_ident_t *param = parse_param_declarator_name(&param_is_array_declarator, &param_is_ptr);
  if (!param) return;

  if (*nargs >= *arg_cap) {
    *arg_cap = pda_next_cap(*arg_cap, *nargs + 1);
    node->args = pda_xreallocarray(node->args, (size_t)(*arg_cap), sizeof(node_t *));
  }
  lvar_t *var;
  if (param_is_array_declarator && ds.tag_kind == TK_EOF && !param_is_ptr) {
    // 仮引数 VLA 宣言子: int a[n] → int *a として扱う (C11 6.7.6.3p7)
    // size=8 (pointer), elem_size=実際の要素サイズ, sizeof(a)==8
    var = psx_decl_register_lvar_sized(param->str, param->len, 8, ds.elem_size, 0);
  } else if (ds.tag_kind != TK_EOF && !param_is_ptr && ds.struct_size > 16) {
    // >16バイト構造体の値渡し → ABI: アドレス渡し（byref）
    // フレームスロットはポインタ(8B)、elem_size=実際の構造体サイズ
    var = psx_decl_register_lvar_sized_align(param->str, param->len, 8, ds.struct_size, 0, 0);
    var->tag_kind = ds.tag_kind;
    var->tag_name = ds.tag_name;
    var->tag_len = ds.tag_len;
    var->is_tag_pointer = 0;
    var->is_byref_param = 1;
  } else if (ds.tag_kind != TK_EOF && !param_is_ptr && ds.struct_size > 0) {
    // ≤16バイト構造体の値渡し → ABI: レジスタ渡し（1 or 2レジスタ）
    var = psx_decl_register_lvar_sized_align(param->str, param->len, ds.struct_size, ds.struct_size, 0, 8);
    var->tag_kind = ds.tag_kind;
    var->tag_name = ds.tag_name;
    var->tag_len = ds.tag_len;
    var->is_tag_pointer = 0;
  } else if (ds.tag_kind != TK_EOF && param_is_ptr) {
    // struct/union へのポインタ仮引数
    var = psx_decl_register_lvar_sized_align(param->str, param->len, 8, 8, 0, 0);
    var->tag_kind = ds.tag_kind;
    var->tag_name = ds.tag_name;
    var->tag_len = ds.tag_len;
    var->is_tag_pointer = 1;
  } else {
    // スカラー型仮引数（既存の動作）
    var = psx_decl_register_lvar(param->str, param->len);
  }
  var->is_param = 1;
  var->is_initialized = 1;
  // args[] には「ABIサイズ」を type_size に持つ ND_LVAR を格納
  // codegen がレジスタ数（1 or 2）を判断するため
  int abi_type_size = (ds.tag_kind != TK_EOF && !param_is_ptr && ds.struct_size > 0)
                      ? ds.struct_size : 8;
  node->args[(*nargs)++] = psx_node_new_lvar_typed(var->offset, abi_type_size);
}

static void parse_param_decl_spec(param_decl_spec_t *out) {
  out->tag_kind = TK_EOF;
  out->tag_name = NULL;
  out->tag_len = 0;
  out->struct_size = 0;
  out->elem_size = 8;

  // 仮引数の型解析（struct/union の値渡し/ポインタ渡しを含む）
  skip_cv_qualifiers();
  if (psx_ctx_is_tag_keyword(token->kind)) {
    // struct/union 型仮引数
    out->tag_kind = token->kind;
    token = token->next;
    token_ident_t *tag_ident = tk_consume_ident();
    if (tag_ident) {
      out->tag_name = tag_ident->str;
      out->tag_len = tag_ident->len;
      if (psx_ctx_has_tag_type(out->tag_kind, out->tag_name, out->tag_len)) {
        out->struct_size = psx_ctx_get_tag_size(out->tag_kind, out->tag_name, out->tag_len);
      }
    }
    return;
  }

  // スカラー型: 仮引数配列宣言子のelemサイズ取得のため型を明示消費
  skip_cv_qualifiers();
  token_kind_t param_type_kind = psx_consume_type_kind();
  if (param_type_kind != TK_EOF) {
    psx_ctx_get_type_info(param_type_kind, NULL, &out->elem_size);
  } else if (psx_ctx_is_typedef_name_token(token)) {
    token = token->next; // typedef名: elem_size は 8 のまま
  }
}

static void parse_func_decl_spec(token_kind_t *ret_kind, tk_float_kind_t *ret_fp_kind,
                                 token_ident_t **ret_tag, int *ret_is_ptr) {
  *ret_kind = TK_EOF;
  *ret_fp_kind = TK_FLOAT_KIND_NONE;
  *ret_tag = NULL;
  *ret_is_ptr = 0;
  if (psx_ctx_is_tag_keyword(token->kind)) {
    // 戻り値型が struct/union Tag [*] の関数定義
    *ret_kind = token->kind; // TK_STRUCT or TK_UNION
    token = token->next;     // skip struct/union keyword
    *ret_tag = tk_consume_ident(); // consume tag name
    while (token->kind == TK_MUL) { token = token->next; *ret_is_ptr = 1; } // skip optional pointer(s)
    return;
  }

  *ret_kind = psx_consume_type_kind(); // 通常の戻り値型（省略可）
  if (*ret_kind == TK_EOF && psx_ctx_is_typedef_name_token(token)) {
    // typedef 名を戻り値型として認識（size_t, FILE 等）
    token_ident_t *td_id = (token_ident_t *)token;
    token_kind_t td_base = TK_EOF;
    int td_elem = 8;
    tk_float_kind_t td_fp = TK_FLOAT_KIND_NONE;
    token_kind_t td_tag = TK_EOF;
    char *td_tag_name = NULL;
    int td_tag_len = 0;
    int td_is_ptr = 0;
    psx_ctx_find_typedef_name(td_id->str, td_id->len, &td_base, &td_elem, &td_fp,
                              &td_tag, &td_tag_name, &td_tag_len, &td_is_ptr);
    token = token->next;
    *ret_kind = td_base;
    *ret_fp_kind = td_fp;
    if (td_is_ptr) *ret_is_ptr = 1;
    if (td_tag != TK_EOF) {
      *ret_tag = calloc(1, sizeof(token_ident_t));
      (*ret_tag)->str = td_tag_name;
      (*ret_tag)->len = td_tag_len;
      *ret_kind = td_tag; // struct/union として扱う
    }
  }
  if (*ret_kind == TK_FLOAT) *ret_fp_kind = TK_FLOAT_KIND_FLOAT;
  else if (*ret_kind == TK_DOUBLE) *ret_fp_kind = TK_FLOAT_KIND_DOUBLE;
  while (token->kind == TK_MUL) { token = token->next; *ret_is_ptr = 1; }
}

static token_ident_t *parse_func_declarator(int *out_is_variadic, node_t ***out_args, int *out_nargs) {
  token_ident_t *tok = tk_consume_ident();
  if (!tok) {
    psx_diag_ctx(token, "funcdef", "%s",
                 diag_message_for(DIAG_ERR_PARSER_FUNCTION_DEF_EXPECTED));
  }
  tk_expect('(');

  int arg_cap = 16;
  node_t **args = calloc(arg_cap, sizeof(node_t *));
  int nargs = 0;
  int is_variadic = 0;
  if (!tk_consume(')')) {
    bool done = false;
    node_func_t node_tmp = {0};
    node_tmp.args = args;
    while (!done) {
      if (token->kind == TK_ELLIPSIS) {
        token = token->next;
        if (token->kind == ',') {
          diag_emit_tokf(DIAG_ERR_PARSER_INVALID_CONTEXT, token,
                         "%s",
                         diag_message_for(DIAG_ERR_PARSER_VARIADIC_NOT_LAST));
        }
        is_variadic = 1;
        done = true;
        continue;
      }
      parse_param_decl(&node_tmp, &nargs, &arg_cap);
      args = node_tmp.args;
      if (!tk_consume(',')) break;
      if (token->kind == TK_RPAREN) {
        psx_diag_missing(token, diag_text_for(DIAG_TEXT_PARAMETER));
      }
    }
    tk_expect(')');
  }
  *out_is_variadic = is_variadic;
  *out_args = args;
  *out_nargs = nargs;
  return tok;
}

// funcdef = "int"? ident "(" params? ")" (";" | "{" stmt* "}")
// params  = "int"? ident ("," "int"? ident)*
static node_t *funcdef(void) {
  token_kind_t ret_kind;
  tk_float_kind_t ret_fp_kind = TK_FLOAT_KIND_NONE;
  token_ident_t *ret_tag = NULL;
  int ret_is_ptr = 0;
  parse_func_decl_spec(&ret_kind, &ret_fp_kind, &ret_tag, &ret_is_ptr);
  if (ret_kind == TK_EOF) {
    diag_warn_tokf(DIAG_WARN_PARSER_IMPLICIT_INT_RETURN, token,
                   "%s", diag_warn_message_for(DIAG_WARN_PARSER_IMPLICIT_INT_RETURN));
  }
  token_kind_t ret_token_kind = (ret_kind == TK_EOF) ? TK_INT : ret_kind;
  psx_expr_set_current_func_ret_type(ret_token_kind, ret_fp_kind);
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
  node_t **args = NULL;
  int nargs = 0;
  token_ident_t *tok = parse_func_declarator(&is_variadic, &args, &nargs);
  node_func_t *node = arena_alloc(sizeof(node_func_t));
  node->base.kind = ND_FUNCDEF;
  node->base.ret_struct_size = psx_expr_current_func_ret_struct_size();
  node->funcname = tok->str;
  node->funcname_len = tok->len;
  psx_ctx_define_function_name_with_ret(tok->str, tok->len,
                                         psx_expr_current_func_ret_struct_size());
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

  // 関数本体 (ブロック)
  tk_expect('{');
  psx_ctx_enter_block_scope();
  node_block_t *body = arena_alloc(sizeof(node_block_t));
  body->base.kind = ND_BLOCK;
  int i = 0;
  int body_cap = 16;
  body->body = calloc(body_cap, sizeof(node_t*));
  int prev_terminates = 0;
  while (!tk_consume('}')) {
    if (prev_terminates && token->kind != TK_CASE && token->kind != TK_DEFAULT &&
        !(token->kind == TK_IDENT && token->next && token->next->kind == TK_COLON)) {
      diag_warn_tokf(DIAG_WARN_PARSER_UNREACHABLE_CODE, token,
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
  node->base.rhs = (node_t *)body;
  psx_ctx_validate_goto_refs();

  // 未使用変数・未初期化変数の警告
  for (lvar_t *v = psx_decl_get_locals(); v; v = v->next_all) {
    if (!v->is_used && !v->is_param && v->name[0] != '_') {
      diag_warn_tokf(DIAG_WARN_PARSER_UNUSED_VARIABLE, token,
                     diag_warn_message_for(DIAG_WARN_PARSER_UNUSED_VARIABLE),
                     v->len, v->name);
    } else if (v->is_used && !v->is_initialized && !v->is_param && !v->is_array) {
      diag_warn_tokf(DIAG_WARN_PARSER_UNINITIALIZED_VARIABLE, token,
                     diag_warn_message_for(DIAG_WARN_PARSER_UNINITIALIZED_VARIABLE),
                     v->len, v->name);
    }
  }

  return (node_t *)node;
}

// expr = assign ("," assign)*
node_t *ps_expr_from(token_t *start) {
  tk_set_current_token(start);
  node_t *node = psx_expr_expr();
  return node;
}

node_t *ps_expr(void) {
  return ps_expr_from(tk_get_current_token());
}
