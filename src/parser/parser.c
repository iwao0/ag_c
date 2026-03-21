#include "parser.h"
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

static node_t *funcdef(void);
static void parse_toplevel_decl_after_type(void);
static void parse_toplevel_tag_decl(void);
static void parse_toplevel_typedef_decl(void);
static token_ident_t *parse_toplevel_typedef_name_decl(int *is_ptr);
static token_ident_t *parse_toplevel_decl_name(int *is_ptr);
static int is_toplevel_function_signature(token_t *tok);
static int is_tag_return_function_signature(token_t *tok);
static int parse_tag_definition_body_toplevel(token_kind_t tag_kind, char *tag_name, int tag_len, int *out_size);
static void skip_balanced_group(token_kind_t lkind, token_kind_t rkind);
static token_ident_t *parse_param_declarator_name(int *out_is_array_declarator);
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

static void skip_cv_qualifiers(void) {
  g_last_type_const_qualified = 0;
  g_last_type_volatile_qualified = 0;
  g_last_alignas_value = 0;
  g_last_decl_is_extern = 0;
  while (is_decl_prefix_token(token->kind)) {
    if (token->kind == TK_CONST) g_last_type_const_qualified = 1;
    if (token->kind == TK_VOLATILE) g_last_type_volatile_qualified = 1;
    if (token->kind == TK_EXTERN) g_last_decl_is_extern = 1;
    if (token->kind == TK_ALIGNAS) {
      token = token->next;
      if (token->kind != TK_LPAREN) {
        psx_diag_ctx(token, "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_ALIGNAS_LPAREN_REQUIRED));
      }
      int av = parse_alignas_value_toplevel();
      if (av > g_last_alignas_value) g_last_alignas_value = av;
      continue;
    }
    if (token->kind == TK_ATOMIC && token->next && token->next->kind == TK_LPAREN) {
      return;
    }
    token = token->next;
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
  while (token->kind == TK_CONST || token->kind == TK_VOLATILE || token->kind == TK_RESTRICT) {
    token = token->next;
  }
}

static void parse_static_assert_toplevel(void) {
  if (token->kind != TK_STATIC_ASSERT) {
    diag_emit_tokf(DIAG_ERR_PARSER_STATIC_ASSERT_EXPECTED, token, "%s",
                   diag_message_for(DIAG_ERR_PARSER_STATIC_ASSERT_EXPECTED));
  }
  token = token->next;
  tk_expect('(');
  node_t *cond = psx_expr_assign();
  if (cond->kind != ND_NUM) {
    diag_emit_tokf(DIAG_ERR_PARSER_STATIC_ASSERT_COND_NOT_CONST, token, "%s",
                   diag_message_for(DIAG_ERR_PARSER_STATIC_ASSERT_COND_NOT_CONST));
  }
  tk_expect(',');
  if (token->kind != TK_STRING) {
    diag_emit_tokf(DIAG_ERR_PARSER_STATIC_ASSERT_MSG_NOT_STRING, token, "%s",
                   diag_message_for(DIAG_ERR_PARSER_STATIC_ASSERT_MSG_NOT_STRING));
  }
  token = token->next;
  tk_expect(')');
  tk_expect(';');
  if (((node_num_t *)cond)->val == 0) {
    diag_emit_tokf(DIAG_ERR_PARSER_STATIC_ASSERT_FAILED, token, "%s",
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

// program = funcdef*
node_t **ps_program(void) {
  int cap = 16;
  node_t **codes = calloc(cap, sizeof(node_t*));
  int i = 0;
  while (!tk_at_eof()) {
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
      if (psx_ctx_is_typedef_name_token(token)) {
        token = token->next;
        g_toplevel_decl_elem_size = 8;
        g_toplevel_decl_is_extern = 0;
      } else {
        token_kind_t tl_kind = psx_consume_type_kind();
        g_toplevel_decl_elem_size = 8;
        if (tl_kind != TK_EOF) psx_ctx_get_type_info(tl_kind, NULL, &g_toplevel_decl_elem_size);
        g_toplevel_decl_is_extern = g_last_decl_is_extern;
      }
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

static int is_toplevel_function_signature(token_t *tok) {
  if (!tok) return 0;
  token_t *t = skip_decl_prefix_lookahead(tok);
  if (!t || (!psx_ctx_is_type_token(t->kind) && !psx_ctx_is_typedef_name_token(t))) return 0;
  t = t->next;
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
    // 配列宣言子を消費（将来的に配列サイズをテーブルに記録することも可能）
    while (tk_consume('[')) {
      (void)parse_array_size_constexpr_toplevel();
      tk_expect(']');
    }
    if (!g_toplevel_decl_is_extern) {
      // グローバル変数テーブルに登録
      global_var_t *gv = calloc(1, sizeof(global_var_t));
      gv->name = name->str;
      gv->name_len = name->len;
      gv->type_size = is_ptr ? 8 : g_toplevel_decl_elem_size;
      gv->deref_size = g_toplevel_decl_elem_size;
      if (tk_consume('=')) {
        node_t *init_expr = psx_expr_assign();
        if (init_expr && init_expr->kind == ND_NUM) {
          gv->has_init = 1;
          gv->init_val = ((node_num_t *)init_expr)->val;
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
  int open_parens = 0;
  while (tk_consume('(')) open_parens++;
  while (tk_consume('*')) {
    *is_ptr = 1;
    skip_ptr_qualifiers();
  }
  token_ident_t *name = tk_consume_ident();
  if (!name) {
    diag_emit_tokf(DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED, token, "%s",
                   diag_message_for(DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED));
  }
  while (open_parens-- > 0) tk_expect(')');
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
  int open_parens = 0;
  while (tk_consume('(')) open_parens++;
  while (tk_consume('*')) {
    *is_ptr = 1;
    skip_ptr_qualifiers();
  }
  token_ident_t *name = tk_consume_ident();
  if (!name) {
    diag_emit_tokf(DIAG_ERR_PARSER_TYPEDEF_NAME_REQUIRED, token, "%s",
                   diag_message_for(DIAG_ERR_PARSER_TYPEDEF_NAME_REQUIRED));
  }
  while (open_parens-- > 0) tk_expect(')');
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
    if (!tag) psx_diag_missing(token, "タグ名");
    tag_name = tag->str;
    tag_len = tag->len;
    if (tk_consume('{')) {
      int member_count = 0;
      int tag_size = 0;
      member_count = parse_tag_definition_body_toplevel(tag_kind, tag_name, tag_len, &tag_size);
      psx_ctx_define_tag_type_with_layout(tag_kind, tag_name, tag_len, member_count, tag_size);
    } else if (!psx_ctx_has_tag_type(tag_kind, tag_name, tag_len)) {
      psx_diag_undefined_with_name(token, "のタグ型", tag_name, tag_len);
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
        psx_diag_missing(token, "タグ名");
      }
      if (tk_consume('{')) {
        int nested_n = 0;
        int nested_sz = 0;
        nested_n = parse_tag_definition_body_toplevel(member_tag_kind, member_tag_name, member_tag_len, &nested_sz);
        psx_ctx_define_tag_type_with_layout(member_tag_kind, member_tag_name, member_tag_len, nested_n, nested_sz);
      } else if (!psx_ctx_has_tag_type(member_tag_kind, member_tag_name, member_tag_len)) {
        psx_diag_undefined_with_name(token, "のタグ型", member_tag_name, member_tag_len);
      }
      elem_size = psx_ctx_get_tag_size(member_tag_kind, member_tag_name, member_tag_len);
      if (elem_size <= 0) {
        psx_diag_ctx(token, "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_INCOMPLETE_MEMBER_FORBIDDEN));
      }
    } else {
      psx_diag_ctx(token, "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_MEMBER_TYPE_REQUIRED));
    }

    for (;;) {
      int is_ptr = 0;
      while (tk_consume('*')) {
        is_ptr = 1;
        skip_ptr_qualifiers();
      }
      token_ident_t *member = tk_consume_ident();
      int has_member_name = member != NULL;
      if (!has_member_name && !(member_tag_kind == TK_STRUCT || member_tag_kind == TK_UNION)
          && token->kind != TK_COLON) {
        psx_diag_missing(token, "メンバ名");
      }
      if (token->kind == TK_LPAREN) {
        skip_balanced_group(TK_LPAREN, TK_RPAREN);
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
      while (tk_consume('[')) {
        if (!has_member_name) psx_diag_missing(token, "メンバ名");
        arr_size *= parse_array_size_constexpr_toplevel();
        tk_expect(']');
      }
      int total_size = is_ptr ? 8 : elem_size * arr_size;
      int deref_size = is_ptr ? elem_size : 0;
      int member_align = is_ptr ? 8 : elem_size;
      if (member_align <= 0) member_align = 1;
      if (member_align > 8) member_align = 8;
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
      if (!has_member_name && tk_consume(',')) psx_diag_missing(token, "メンバ名");
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
    if (!enumerator) psx_diag_missing(token, "列挙子名");
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
  if (!tag) psx_diag_missing(token, "タグ名");

  if (tk_consume('{')) {
    int member_count = 0;
    int tag_size = 0;
    member_count = parse_tag_definition_body_toplevel(tag_kind, tag->str, tag->len, &tag_size);
    psx_ctx_define_tag_type_with_layout(tag_kind, tag->str, tag->len, member_count, tag_size);
    if (tk_consume(';')) return;
    parse_toplevel_declarator_list();
    tk_expect(';');
    return;
  }
  if (tk_consume(';')) {
    psx_ctx_define_tag_type(tag_kind, tag->str, tag->len);
    return;
  }
  if (!psx_ctx_has_tag_type(tag_kind, tag->str, tag->len)) {
    psx_diag_undefined_with_name(token, "のタグ型", tag->str, tag->len);
  }
  parse_toplevel_declarator_list();
  tk_expect(';');
}

// consume_type: 型キーワードがあれば読み進め、そのトークン種別を返す（0=型なし）
token_kind_t psx_consume_type_kind(void) {
  skip_cv_qualifiers();
  if (token->kind == TK_ATOMIC && token->next && token->next->kind == TK_LPAREN) {
    token_kind_t inner = parse_atomic_type_specifier();
    if (inner != TK_EOF) return inner;
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
    break;
  }

  if (token == start) return TK_EOF;
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

static token_ident_t *parse_param_declarator_name(int *out_is_array_declarator) {
  if (out_is_array_declarator) *out_is_array_declarator = 0;
  token_ident_t *param = NULL;
  int open_parens = 0;
  while (tk_consume('(')) open_parens++;
  while (tk_consume('*')) {
    skip_ptr_qualifiers();
  }
  param = tk_consume_ident();
  if (param) {
    while (open_parens-- > 0) tk_expect(')');
  } else if (open_parens > 0) {
    skip_balanced_group(TK_LPAREN, TK_RPAREN);
  }

  while (token->kind == TK_LPAREN || token->kind == TK_LBRACKET) {
    if (token->kind == TK_LPAREN) {
      skip_balanced_group(TK_LPAREN, TK_RPAREN);
    } else {
      // 仮引数配列宣言子 int a[n] を検出: a は int* として扱う (C11 6.7.6.3p7)
      if (out_is_array_declarator) *out_is_array_declarator = 1;
      skip_balanced_group(TK_LBRACKET, TK_RBRACKET);
    }
  }
  return param;
}

// funcdef = "int"? ident "(" params? ")" (";" | "{" stmt* "}")
// params  = "int"? ident ("," "int"? ident)*
static node_t *funcdef(void) {
  token_kind_t ret_kind;
  tk_float_kind_t ret_fp_kind = TK_FLOAT_KIND_NONE;
  token_ident_t *ret_tag = NULL;
  int ret_is_ptr = 0;
  if (psx_ctx_is_tag_keyword(token->kind)) {
    // 戻り値型が struct/union Tag [*] の関数定義
    ret_kind = token->kind; // TK_STRUCT or TK_UNION
    token = token->next;    // skip struct/union keyword
    ret_tag = tk_consume_ident(); // consume tag name
    while (token->kind == TK_MUL) { token = token->next; ret_is_ptr = 1; } // skip optional pointer(s)
  } else {
    ret_kind = psx_consume_type_kind(); // 通常の戻り値型（省略可）
    if (ret_kind == TK_FLOAT) ret_fp_kind = TK_FLOAT_KIND_FLOAT;
    else if (ret_kind == TK_DOUBLE) ret_fp_kind = TK_FLOAT_KIND_DOUBLE;
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
  token_ident_t *tok = tk_consume_ident();
  if (!tok) {
    psx_diag_ctx(token, "funcdef", "%s",
                 diag_message_for(DIAG_ERR_PARSER_FUNCTION_DEF_EXPECTED));
  }
  node_func_t *node = calloc(1, sizeof(node_func_t));
  node->base.kind = ND_FUNCDEF;
  node->funcname = tok->str;
  node->funcname_len = tok->len;
  psx_ctx_define_function_name_with_ret(tok->str, tok->len,
                                         psx_expr_current_func_ret_struct_size());
  psx_expr_set_current_funcname(tok->str, tok->len); // __func__ 用

  // 関数ごとにローカル変数テーブルをリセット
  psx_decl_reset_locals();
  psx_ctx_reset_function_scope();
  psx_loop_reset();

  tk_expect('(');
  // 仮引数のパース
  int arg_cap = 16;
  node->args = calloc(arg_cap, sizeof(node_t*));
  int nargs = 0;
  if (!tk_consume(')')) {
    bool done = false;
    while (!done) {
      if (token->kind == TK_ELLIPSIS) {
        token = token->next;
        if (token->kind == ',') {
          diag_emit_tokf(DIAG_ERR_PARSER_INVALID_CONTEXT, token,
                         "%s",
                         diag_message_for(DIAG_ERR_PARSER_VARIADIC_NOT_LAST));
        }
        node->is_variadic = 1;
        done = true;
        continue;
      }

      // 仮引数の型解析（struct/union の値渡し/ポインタ渡しを含む）
      skip_cv_qualifiers();
      token_kind_t ptag_kind = TK_EOF;
      char *ptag_name = NULL;
      int ptag_len = 0;
      int param_struct_size = 0;
      int param_elem_size = 8; // スカラー型の要素サイズ（仮引数配列宣言子用）
      if (psx_ctx_is_tag_keyword(token->kind)) {
        // struct/union 型仮引数
        ptag_kind = token->kind;
        token = token->next;
        token_ident_t *tag_ident = tk_consume_ident();
        if (tag_ident) {
          ptag_name = tag_ident->str;
          ptag_len = tag_ident->len;
          if (psx_ctx_has_tag_type(ptag_kind, ptag_name, ptag_len)) {
            param_struct_size = psx_ctx_get_tag_size(ptag_kind, ptag_name, ptag_len);
          }
        }
      } else {
        // スカラー型: 仮引数配列宣言子のelemサイズ取得のため型を明示消費
        skip_cv_qualifiers();
        token_kind_t param_type_kind = psx_consume_type_kind();
        if (param_type_kind != TK_EOF) {
          psx_ctx_get_type_info(param_type_kind, NULL, &param_elem_size);
        } else if (psx_ctx_is_typedef_name_token(token)) {
          token = token->next; // typedef名: elem_size は 8 のまま
        }
      }
      // ポインタ修飾子を確認してから parse_param_declarator_name へ
      int param_is_ptr = (token->kind == '*');
      int param_is_array_declarator = 0;
      token_ident_t *param = parse_param_declarator_name(&param_is_array_declarator);
      if (param) {
        if (nargs >= arg_cap) {
          arg_cap = pda_next_cap(arg_cap, nargs + 1);
          node->args = pda_xreallocarray(node->args, (size_t)arg_cap, sizeof(node_t *));
        }
        lvar_t *var;
        if (param_is_array_declarator && ptag_kind == TK_EOF && !param_is_ptr) {
          // 仮引数 VLA 宣言子: int a[n] → int *a として扱う (C11 6.7.6.3p7)
          // size=8 (pointer), elem_size=実際の要素サイズ, sizeof(a)==8
          var = psx_decl_register_lvar_sized(param->str, param->len, 8, param_elem_size, 0);
        } else if (ptag_kind != TK_EOF && !param_is_ptr && param_struct_size > 16) {
          // >16バイト構造体の値渡し → ABI: アドレス渡し（byref）
          // フレームスロットはポインタ(8B)、elem_size=実際の構造体サイズ
          var = psx_decl_register_lvar_sized_align(param->str, param->len, 8, param_struct_size, 0, 0);
          var->tag_kind = ptag_kind;
          var->tag_name = ptag_name;
          var->tag_len = ptag_len;
          var->is_tag_pointer = 0;
          var->is_byref_param = 1;
        } else if (ptag_kind != TK_EOF && !param_is_ptr && param_struct_size > 0) {
          // ≤16バイト構造体の値渡し → ABI: レジスタ渡し（1 or 2レジスタ）
          var = psx_decl_register_lvar_sized_align(param->str, param->len, param_struct_size, param_struct_size, 0, 8);
          var->tag_kind = ptag_kind;
          var->tag_name = ptag_name;
          var->tag_len = ptag_len;
          var->is_tag_pointer = 0;
        } else if (ptag_kind != TK_EOF && param_is_ptr) {
          // struct/union へのポインタ仮引数
          var = psx_decl_register_lvar_sized_align(param->str, param->len, 8, 8, 0, 0);
          var->tag_kind = ptag_kind;
          var->tag_name = ptag_name;
          var->tag_len = ptag_len;
          var->is_tag_pointer = 1;
        } else {
          // スカラー型仮引数（既存の動作）
          var = psx_decl_register_lvar(param->str, param->len);
        }
        // args[] には「ABIサイズ」を type_size に持つ ND_LVAR を格納
        // codegen がレジスタ数（1 or 2）を判断するため
        int abi_type_size = (ptag_kind != TK_EOF && !param_is_ptr && param_struct_size > 0)
                            ? param_struct_size : 8;
        node->args[nargs++] = psx_node_new_lvar_typed(var->offset, abi_type_size);
      }

      if (!tk_consume(',')) break;
      if (token->kind == TK_RPAREN) {
        psx_diag_missing(token, "仮引数");
      }
    }
    tk_expect(')');
  }
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
  node_block_t *body = calloc(1, sizeof(node_block_t));
  body->base.kind = ND_BLOCK;
  int i = 0;
  int body_cap = 16;
  body->body = calloc(body_cap, sizeof(node_t*));
  while (!tk_consume('}')) {
    if (i >= body_cap - 1) {
      body_cap = pda_next_cap(body_cap, i + 2);
      body->body = pda_xreallocarray(body->body, (size_t)body_cap, sizeof(node_t *));
    }
    body->body[i++] = psx_stmt_stmt();
  }
  body->body[i] = NULL;
  psx_ctx_leave_block_scope();
  node->base.rhs = (node_t *)body;
  psx_ctx_validate_goto_refs();

  return (node_t *)node;
}

// expr = assign ("," assign)*
node_t *ps_expr(void) {
  node_t *node = psx_expr_expr();
  return node;
}
