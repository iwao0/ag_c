#include "parser_stmt.h"
#include "parser_decl.h"
#include "parser_diag.h"
#include "parser_dynarray.h"
#include "parser_expr.h"
#include "parser_loop_ctx.h"
#include "parser_node_utils.h"
#include "parser_semantic_ctx.h"
#include "parser_switch_ctx.h"
#include "../tokenizer/tokenizer.h"
#include <stdlib.h>

node_t *expr(void);

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

  if (pctx_is_type_token(token->kind)) {
    return pdecl_parse_declaration();
  }

  if (pctx_is_tag_keyword(token->kind)) {
    token_kind_t tag_kind = token->kind;
    token = token->next;
    token_ident_t *tag = tk_consume_ident();
    if (!tag) {
      pdiag_missing(token, "タグ名");
    }
    if (tk_consume('{')) {
      tk_error_tok(token, "struct/union/enum のメンバ宣言は未対応です");
    }
    if (tk_consume(';')) {
      pctx_define_tag_type(tag_kind, tag->str, tag->len);
      return pnode_new_num(0);
    }
    if (!pctx_has_tag_type(tag_kind, tag->str, tag->len)) {
      pdiag_undefined_with_name(token, "のタグ型", tag->str, tag->len);
    }
    return pdecl_parse_declaration_after_type(8, TK_FLOAT_KIND_NONE);
  }

  if (token->kind == TK_RETURN) {
    token = token->next;
    node_t *node = calloc(1, sizeof(node_t));
    node->kind = ND_RETURN;
    if (tk_consume(';')) {
      if (pexpr_current_func_ret_token_kind() != TK_VOID) {
        tk_error_tok(token, "void 以外の関数では return に式が必要です");
      }
      node->lhs = NULL;
      node->fp_kind = pexpr_current_func_ret_fp_kind();
      return node;
    }
    node->lhs = expr();
    if (pexpr_current_func_ret_token_kind() == TK_VOID) {
      tk_error_tok(token, "void 関数では return に式を指定できません");
    }
    node->fp_kind = pexpr_current_func_ret_fp_kind();
    tk_expect(';');
    return node;
  }

  if (token->kind == TK_IF) {
    token = token->next;
    tk_expect('(');
    node_ctrl_t *node = calloc(1, sizeof(node_ctrl_t));
    node->base.kind = ND_IF;
    node->base.lhs = expr();
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
    node->base.lhs = expr();
    tk_expect(')');
    ploop_enter();
    node->base.rhs = stmt_internal();
    ploop_leave();
    return (node_t *)node;
  }

  if (token->kind == TK_DO) {
    token = token->next;
    node_ctrl_t *node = calloc(1, sizeof(node_ctrl_t));
    node->base.kind = ND_DO_WHILE;
    ploop_enter();
    node->base.rhs = stmt_internal();
    ploop_leave();
    if (token->kind != TK_WHILE) {
      pdiag_missing(token, "'while'");
    }
    token = token->next;
    tk_expect('(');
    node->base.lhs = expr();
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
      if (pctx_is_type_token(token->kind)) {
        node->init = pdecl_parse_declaration();
      } else {
        node->init = expr();
        tk_expect(';');
      }
    }
    if (!tk_consume(';')) {
      node->base.lhs = expr();
      tk_expect(';');
    }
    if (!tk_consume(')')) {
      node->inc = expr();
      tk_expect(')');
    }
    ploop_enter();
    node->base.rhs = stmt_internal();
    ploop_leave();
    return (node_t *)node;
  }

  if (token->kind == TK_SWITCH) {
    token = token->next;
    tk_expect('(');
    node_ctrl_t *node = calloc(1, sizeof(node_ctrl_t));
    node->base.kind = ND_SWITCH;
    node->base.lhs = expr();
    tk_expect(')');
    psw_push_ctx();
    node->base.rhs = stmt_internal();
    psw_pop_ctx();
    return (node_t *)node;
  }

  if (token->kind == TK_CASE) {
    token = token->next;
    node_case_t *node = calloc(1, sizeof(node_case_t));
    node->base.kind = ND_CASE;
    node->val = tk_expect_number();
    psw_register_case(node->val, token);
    tk_expect(':');
    node->base.rhs = stmt_internal();
    return (node_t *)node;
  }

  if (token->kind == TK_DEFAULT) {
    token = token->next;
    psw_register_default(token);
    node_default_t *node = calloc(1, sizeof(node_default_t));
    node->base.kind = ND_DEFAULT;
    tk_expect(':');
    node->base.rhs = stmt_internal();
    return (node_t *)node;
  }

  if (token->kind == TK_BREAK) {
    if (ploop_depth() == 0 && !psw_has_ctx()) {
      pdiag_only_in(token, "break", "ループまたはswitch内");
    }
    token = token->next;
    node_t *node = calloc(1, sizeof(node_t));
    node->kind = ND_BREAK;
    tk_expect(';');
    return node;
  }

  if (token->kind == TK_CONTINUE) {
    if (ploop_depth() == 0) {
      pdiag_only_in(token, "continue", "ループ内");
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
      pdiag_missing(token, "goto の後のラベル名");
    }
    node_jump_t *node = calloc(1, sizeof(node_jump_t));
    node->base.kind = ND_GOTO;
    node->name = ident->str;
    node->name_len = ident->len;
    pctx_register_goto_ref(ident->str, ident->len, goto_tok);
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
    pctx_register_label_def(ident->str, ident->len, token);
    node->base.rhs = stmt_internal();
    return (node_t *)node;
  }

  node_t *node = expr();
  tk_expect(';');
  return node;
}

node_t *pstmt_stmt(void) {
  return stmt_internal();
}
