#include "parser.h"
#include "internal/node_utils.h"
#include "internal/semantic_ctx.h"
#include "internal/decl.h"
#include "internal/diag.h"
#include "internal/dynarray.h"
#include "internal/expr.h"
#include "internal/loop_ctx.h"
#include "internal/stmt.h"
#include "internal/switch_ctx.h"
#include "../tokenizer/tokenizer.h"
#include <stdio.h>
#include <stdlib.h>

string_lit_t *string_literals = NULL;
float_lit_t *float_literals = NULL;

static node_t *funcdef(void);
static void parse_toplevel_decl_after_type(void);
static void parse_toplevel_tag_decl(void);
static int is_toplevel_function_signature(token_t *tok);
static int parse_tag_definition_body_toplevel(token_kind_t tag_kind);

// program = funcdef*
node_t **ps_program(void) {
  int cap = 16;
  node_t **codes = calloc(cap, sizeof(node_t*));
  int i = 0;
  while (!tk_at_eof()) {
    if (psx_ctx_is_tag_keyword(token->kind)) {
      parse_toplevel_tag_decl();
      continue;
    }
    if (psx_ctx_is_type_token(token->kind) && !is_toplevel_function_signature(token)) {
      token = token->next; // base type
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
  if (!tok || !psx_ctx_is_type_token(tok->kind)) return 0;
  token_t *t = tok->next;
  while (t && t->kind == TK_MUL) t = t->next;
  if (!t || t->kind != TK_IDENT) return 0;
  return t->next && t->next->kind == TK_LPAREN;
}

static void parse_toplevel_declarator_list(void) {
  for (;;) {
    while (tk_consume('*')) {}
    token_ident_t *name = tk_consume_ident();
    if (!name) psx_diag_ctx(token, "decl", "変数名が期待されます");
    if (tk_consume('[')) {
      tk_expect_number();
      tk_expect(']');
    }
    if (tk_consume('=')) {
      psx_expr_assign();
    }
    if (!tk_consume(',')) break;
  }
}

static void parse_toplevel_decl_after_type(void) {
  parse_toplevel_declarator_list();
  tk_expect(';');
}

static void parse_member_type_specifier_toplevel(void) {
  if (psx_ctx_is_type_token(token->kind)) {
    token = token->next;
    return;
  }
  if (psx_ctx_is_tag_keyword(token->kind)) {
    token_kind_t nested_kind = token->kind;
    token = token->next;
    token_ident_t *nested_tag = tk_consume_ident();
    if (!nested_tag) psx_diag_missing(token, "タグ名");
    if (tk_consume('{')) {
      int n = parse_tag_definition_body_toplevel(nested_kind);
      psx_ctx_define_tag_type_with_members(nested_kind, nested_tag->str, nested_tag->len, n);
      return;
    }
    if (!psx_ctx_has_tag_type(nested_kind, nested_tag->str, nested_tag->len)) {
      psx_diag_undefined_with_name(token, "のタグ型", nested_tag->str, nested_tag->len);
    }
    return;
  }
  psx_diag_ctx(token, "decl", "メンバ型が期待されます");
}

static int parse_struct_or_union_members_toplevel(void) {
  int member_count = 0;
  while (!tk_consume('}')) {
    parse_member_type_specifier_toplevel();
    for (;;) {
      while (tk_consume('*')) {}
      token_ident_t *member = tk_consume_ident();
      if (!member) psx_diag_missing(token, "メンバ名");
      member_count++;
      if (tk_consume('[')) {
        tk_expect_number();
        tk_expect(']');
      }
      if (!tk_consume(',')) break;
    }
    tk_expect(';');
  }
  return member_count;
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
      value = tk_expect_number();
    }
    psx_ctx_define_enum_const(enumerator->str, enumerator->len, value);
    next_value = value + 1;
    if (tk_consume('}')) break;
    tk_expect(',');
    if (tk_consume('}')) break;
  }
  return member_count;
}

static int parse_tag_definition_body_toplevel(token_kind_t tag_kind) {
  if (tag_kind == TK_ENUM) return parse_enum_members_toplevel();
  return parse_struct_or_union_members_toplevel();
}

static void parse_toplevel_tag_decl(void) {
  token_kind_t tag_kind = token->kind;
  token = token->next;
  token_ident_t *tag = tk_consume_ident();
  if (!tag) psx_diag_missing(token, "タグ名");

  if (tk_consume('{')) {
    int member_count = parse_tag_definition_body_toplevel(tag_kind);
    psx_ctx_define_tag_type_with_members(tag_kind, tag->str, tag->len, member_count);
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
  if (token->kind == TK_INT || token->kind == TK_CHAR || token->kind == TK_VOID ||
      token->kind == TK_SHORT || token->kind == TK_LONG || token->kind == TK_FLOAT ||
      token->kind == TK_DOUBLE || token->kind == TK_BOOL || token->kind == TK_SIGNED ||
      token->kind == TK_UNSIGNED) {
    token_kind_t kind = token->kind;
    token = token->next;
    return kind;
  }
  return TK_EOF; // 型なし
}

static bool consume_type(void) {
  return psx_consume_type_kind() != TK_EOF;
}

// funcdef = "int"? ident "(" params? ")" (";" | "{" stmt* "}")
// params  = "int"? ident ("," "int"? ident)*
static node_t *funcdef(void) {
  token_kind_t ret_kind = psx_consume_type_kind(); // 戻り値の型（省略可）
  token_kind_t ret_token_kind = (ret_kind == TK_EOF) ? TK_INT : ret_kind;
  tk_float_kind_t ret_fp_kind = TK_FLOAT_KIND_NONE;
  if (ret_kind == TK_FLOAT) ret_fp_kind = TK_FLOAT_KIND_FLOAT;
  else if (ret_kind == TK_DOUBLE) ret_fp_kind = TK_FLOAT_KIND_DOUBLE;
  psx_expr_set_current_func_ret_type(ret_token_kind, ret_fp_kind);
  token_ident_t *tok = tk_consume_ident();
  if (!tok) {
    psx_diag_ctx(token, "funcdef", "関数定義が期待されます");
  }
  node_func_t *node = calloc(1, sizeof(node_func_t));
  node->base.kind = ND_FUNCDEF;
  node->funcname = tok->str;
  node->funcname_len = tok->len;

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
    consume_type(); // 仮引数の型
    while (tk_consume('*')) {} // ポインタの * を読み飛ばす
    token_ident_t *param = tk_consume_ident();
    if (param) {
      lvar_t *var = psx_decl_register_lvar(param->str, param->len);
      node->args[nargs++] = psx_node_new_lvar(var->offset);
    }
    while (tk_consume(',')) {
      if (nargs >= arg_cap) {
        arg_cap = pda_next_cap(arg_cap, nargs + 1);
        node->args = pda_xreallocarray(node->args, (size_t)arg_cap, sizeof(node_t *));
      }
      consume_type(); // 仮引数の型
      while (tk_consume('*')) {} // ポインタの * を読み飛ばす
      param = tk_consume_ident();
      if (param) {
        lvar_t *var = psx_decl_register_lvar(param->str, param->len);
        node->args[nargs++] = psx_node_new_lvar(var->offset);
      }
    }
    tk_expect(')');
  }
  node->nargs = nargs;

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
