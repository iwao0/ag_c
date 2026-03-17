#include "parser.h"
#include "node_utils.h"
#include "semantic_ctx.h"
#include "decl.h"
#include "diag.h"
#include "dynarray.h"
#include "expr.h"
#include "loop_ctx.h"
#include "stmt.h"
#include "switch_ctx.h"
#include "../tokenizer/tokenizer.h"
#include <stdio.h>
#include <stdlib.h>

node_t **code;
string_lit_t *string_literals = NULL;
float_lit_t *float_literals = NULL;

static node_t *funcdef(void);

// program = funcdef*
void ps_program(void) {
  int cap = 16;
  code = calloc(cap, sizeof(node_t*));
  int i = 0;
  while (!tk_at_eof()) {
    node_t *fn = funcdef();
    if (!fn) continue; // 関数プロトタイプ宣言はASTへ載せない
    if (i >= cap - 1) { // NULL終端用
      cap = pda_next_cap(cap, i + 2);
      code = realloc(code, sizeof(node_t*) * cap);
    }
    code[i++] = fn;
  }
  code[i] = NULL;
}

// consume_type: 型キーワードがあれば読み進め、そのトークン種別を返す（0=型なし）
token_kind_t psx_consume_type_kind(void) {
  if (token->kind == TK_INT || token->kind == TK_CHAR || token->kind == TK_VOID || token->kind == TK_SHORT || token->kind == TK_LONG || token->kind == TK_FLOAT || token->kind == TK_DOUBLE) {
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
        node->args = realloc(node->args, sizeof(node_t*) * arg_cap);
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
  node_block_t *body = calloc(1, sizeof(node_block_t));
  body->base.kind = ND_BLOCK;
  int i = 0;
  int body_cap = 16;
  body->body = calloc(body_cap, sizeof(node_t*));
  while (!tk_consume('}')) {
    if (i >= body_cap - 1) {
      body_cap = pda_next_cap(body_cap, i + 2);
      body->body = realloc(body->body, sizeof(node_t*) * body_cap);
    }
    body->body[i++] = psx_stmt_stmt();
  }
  body->body[i] = NULL;
  node->base.rhs = (node_t *)body;
  psx_ctx_validate_goto_refs();

  return (node_t *)node;
}

// expr = assign ("," assign)*
node_t *ps_expr(void) {
  node_t *node = psx_expr_expr();
  return node;
}
