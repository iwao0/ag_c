#include "stmt.h"
#include "decl.h"
#include "diag.h"
#include "dynarray.h"
#include "expr.h"
#include "loop_ctx.h"
#include "node_utils.h"
#include "semantic_ctx.h"
#include "switch_ctx.h"
#include "../tokenizer/tokenizer.h"
#include <stdlib.h>

node_t *ps_expr(void);

static node_t *stmt_internal(void) {
  if (tk_consume('{')) {
    node_block_t *node = calloc(1, sizeof(node_block_t));
    node->base.kind = ND_BLOCK;
    int i = 0;
    int cap = 16;
    node->body = calloc(cap, sizeof(node_t*));
    while (!tk_consume('}')) {
      if (i >= cap - 1) {
        cap = pda_next_cap(cap, i + 2);
        node->body = realloc(node->body, sizeof(node_t*) * cap);
      }
      node->body[i++] = stmt_internal();
    }
    node->body[i] = NULL;
    return (node_t *)node;
  }

  if (psx_ctx_is_type_token(token->kind)) {
    return psx_decl_parse_declaration();
  }

  if (psx_ctx_is_tag_keyword(token->kind)) {
    token_kind_t tag_kind = token->kind;
    token = token->next;
    token_ident_t *tag = tk_consume_ident();
    if (!tag) {
      psx_diag_missing(token, "タグ名");
    }
    if (tk_consume('{')) {
      tk_error_tok(token, "struct/union/enum のメンバ宣言は未対応です");
    }
    if (tk_consume(';')) {
      psx_ctx_define_tag_type(tag_kind, tag->str, tag->len);
      return psx_node_new_num(0);
    }
    if (!psx_ctx_has_tag_type(tag_kind, tag->str, tag->len)) {
      psx_diag_undefined_with_name(token, "のタグ型", tag->str, tag->len);
    }
    return psx_decl_parse_declaration_after_type(8, TK_FLOAT_KIND_NONE);
  }

  if (token->kind == TK_RETURN) {
    token = token->next;
    node_t *node = calloc(1, sizeof(node_t));
    node->kind = ND_RETURN;
    if (tk_consume(';')) {
      if (psx_expr_current_func_ret_token_kind() != TK_VOID) {
        tk_error_tok(token, "void 以外の関数では return に式が必要です");
      }
      node->lhs = NULL;
      node->fp_kind = psx_expr_current_func_ret_fp_kind();
      return node;
    }
    node->lhs = ps_expr();
    if (psx_expr_current_func_ret_token_kind() == TK_VOID) {
      tk_error_tok(token, "void 関数では return に式を指定できません");
    }
    node->fp_kind = psx_expr_current_func_ret_fp_kind();
    tk_expect(';');
    return node;
  }

  if (token->kind == TK_IF) {
    token = token->next;
    tk_expect('(');
    node_ctrl_t *node = calloc(1, sizeof(node_ctrl_t));
    node->base.kind = ND_IF;
    node->base.lhs = ps_expr();
    tk_expect(')');
    node->base.rhs = stmt_internal();
    if (token->kind == TK_ELSE) {
      token = token->next;
      node->els = stmt_internal();
    }
    return (node_t *)node;
  }

  if (token->kind == TK_WHILE) {
    token = token->next;
    tk_expect('(');
    node_ctrl_t *node = calloc(1, sizeof(node_ctrl_t));
    node->base.kind = ND_WHILE;
    node->base.lhs = ps_expr();
    tk_expect(')');
    psx_loop_enter();
    node->base.rhs = stmt_internal();
    psx_loop_leave();
    return (node_t *)node;
  }

  if (token->kind == TK_DO) {
    token = token->next;
    node_ctrl_t *node = calloc(1, sizeof(node_ctrl_t));
    node->base.kind = ND_DO_WHILE;
    psx_loop_enter();
    node->base.rhs = stmt_internal();
    psx_loop_leave();
    if (token->kind != TK_WHILE) {
      psx_diag_missing(token, "'while'");
    }
    token = token->next;
    tk_expect('(');
    node->base.lhs = ps_expr();
    tk_expect(')');
    tk_expect(';');
    return (node_t *)node;
  }

  if (token->kind == TK_FOR) {
    token = token->next;
    tk_expect('(');
    node_ctrl_t *node = calloc(1, sizeof(node_ctrl_t));
    node->base.kind = ND_FOR;
    if (!tk_consume(';')) {
      if (psx_ctx_is_type_token(token->kind)) {
        node->init = psx_decl_parse_declaration();
      } else {
        node->init = ps_expr();
        tk_expect(';');
      }
    }
    if (!tk_consume(';')) {
      node->base.lhs = ps_expr();
      tk_expect(';');
    }
    if (!tk_consume(')')) {
      node->inc = ps_expr();
      tk_expect(')');
    }
    psx_loop_enter();
    node->base.rhs = stmt_internal();
    psx_loop_leave();
    return (node_t *)node;
  }

  if (token->kind == TK_SWITCH) {
    token = token->next;
    tk_expect('(');
    node_ctrl_t *node = calloc(1, sizeof(node_ctrl_t));
    node->base.kind = ND_SWITCH;
    node->base.lhs = ps_expr();
    tk_expect(')');
    psx_switch_push_ctx();
    node->base.rhs = stmt_internal();
    psx_switch_pop_ctx();
    return (node_t *)node;
  }

  if (token->kind == TK_CASE) {
    token = token->next;
    node_case_t *node = calloc(1, sizeof(node_case_t));
    node->base.kind = ND_CASE;
    node->val = tk_expect_number();
    psx_switch_register_case(node->val, token);
    tk_expect(':');
    node->base.rhs = stmt_internal();
    return (node_t *)node;
  }

  if (token->kind == TK_DEFAULT) {
    token = token->next;
    psx_switch_register_default(token);
    node_default_t *node = calloc(1, sizeof(node_default_t));
    node->base.kind = ND_DEFAULT;
    tk_expect(':');
    node->base.rhs = stmt_internal();
    return (node_t *)node;
  }

  if (token->kind == TK_BREAK) {
    if (psx_loop_depth() == 0 && !psx_switch_has_ctx()) {
      psx_diag_only_in(token, "break", "ループまたはswitch内");
    }
    token = token->next;
    node_t *node = calloc(1, sizeof(node_t));
    node->kind = ND_BREAK;
    tk_expect(';');
    return node;
  }

  if (token->kind == TK_CONTINUE) {
    if (psx_loop_depth() == 0) {
      psx_diag_only_in(token, "continue", "ループ内");
    }
    token = token->next;
    node_t *node = calloc(1, sizeof(node_t));
    node->kind = ND_CONTINUE;
    tk_expect(';');
    return node;
  }

  if (token->kind == TK_GOTO) {
    token_t *goto_tok = token;
    token = token->next;
    token_ident_t *ident = tk_consume_ident();
    if (!ident) {
      psx_diag_missing(token, "goto の後のラベル名");
    }
    node_jump_t *node = calloc(1, sizeof(node_jump_t));
    node->base.kind = ND_GOTO;
    node->name = ident->str;
    node->name_len = ident->len;
    psx_ctx_register_goto_ref(ident->str, ident->len, goto_tok);
    tk_expect(';');
    return (node_t *)node;
  }

  if (token->kind == TK_IDENT && token->next && token->next->kind == TK_COLON) {
    token_ident_t *ident = tk_consume_ident();
    tk_expect(':');
    node_jump_t *node = calloc(1, sizeof(node_jump_t));
    node->base.kind = ND_LABEL;
    node->name = ident->str;
    node->name_len = ident->len;
    psx_ctx_register_label_def(ident->str, ident->len, token);
    node->base.rhs = stmt_internal();
    return (node_t *)node;
  }

  node_t *node = ps_expr();
  tk_expect(';');
  return node;
}

node_t *psx_stmt_stmt(void) {
  return stmt_internal();
}
