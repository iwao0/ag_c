#include "stmt.h"
#include "arena.h"
#include "ast.h"
#include "core.h"
#include "diag.h"
#include "dynarray.h"
#include "parser_recovery.h"
#include "runtime_context.h"
#include "syntax_node.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  psx_statement_syntax_context_t syntax;
  psx_parser_runtime_context_t *runtime_context;
  arena_context_t *arena_context;
  tokenizer_context_t *tokenizer_context;
} psx_statement_parse_context_t;

static inline token_t *curtok(psx_statement_parse_context_t *context) {
  return tk_get_current_token_ctx(context->tokenizer_context);
}

static inline ag_diagnostic_context_t *diagnostics(
    psx_statement_parse_context_t *context) {
  return ps_parser_runtime_diagnostics(context->runtime_context);
}

static inline void set_curtok(
    psx_statement_parse_context_t *context, token_t *tok) {
  tk_set_current_token_ctx(context->tokenizer_context, tok);
}

static node_t *stmt_internal(psx_statement_parse_context_t *context);
static node_t *parse_stmt_label(psx_statement_parse_context_t *context);
static node_t *block_item(psx_statement_parse_context_t *context);
static int is_decl_like_start_stmt(
    psx_statement_parse_context_t *context);
static node_t *parse_decl_like_stmt(
    psx_statement_parse_context_t *context);
static int is_label_start_stmt(psx_statement_parse_context_t *context) {
  return curtok(context)->kind == TK_IDENT && curtok(context)->next &&
         curtok(context)->next->kind == TK_COLON;
}

static void require_syntax_service(
    psx_statement_parse_context_t *context, const char *name) {
  diag_emit_internalf_in(
      diagnostics(context), DIAG_ERR_INTERNAL_INVARIANT_FAILED,
      "%s: statement syntax service '%s' is unavailable",
      diag_message_for_in(
          diagnostics(context), DIAG_ERR_INTERNAL_INVARIANT_FAILED),
      name);
  ps_parser_mark_recoverable_syntax_error_in(
      context->runtime_context);
}

static int is_decl_like_start_stmt(
    psx_statement_parse_context_t *context) {
  if (curtok(context)->kind == TK_TYPEDEF) return 1;
  if (curtok(context)->kind == TK_STATIC_ASSERT) return 1;
  if (psx_is_type_specifier_token(curtok(context)->kind) ||
      psx_is_decl_prefix_token(curtok(context)->kind) ||
      ps_name_classifier_is_typedef_name(
          &context->syntax.name_classifier, curtok(context))) return 1;
  if (psx_is_tag_keyword_token(curtok(context)->kind)) return 1;
  return 0;
}

static node_t *parse_decl_like_stmt(
    psx_statement_parse_context_t *context) {
  if (!context->syntax.parse_local_declaration) {
    require_syntax_service(context, "parse_local_declaration");
    return NULL;
  }
  return context->syntax.parse_local_declaration(
      context->syntax.context);
}

static node_t *parse_expression(
    psx_statement_parse_context_t *context) {
  if (!context->syntax.parse_expression) {
    require_syntax_service(context, "parse_expression");
    return NULL;
  }
  return context->syntax.parse_expression(context->syntax.context);
}

static node_t *block_item(psx_statement_parse_context_t *context) {
  if (is_label_start_stmt(context)) {
    return parse_stmt_label(context);
  }
  if (is_decl_like_start_stmt(context)) {
    return parse_decl_like_stmt(context);
  }

  return stmt_internal(context);
}

/* 文 (statement) 分岐ヘルパ群: stmt_internal の dispatch から呼ばれる。
 * 各ヘルパは対応するキーワードトークンを消費して文を構築する。
 * (block / return / if / while / do-while / for / switch / case /
 *  default / break / continue / goto / label) */
static node_t *parse_stmt_block(psx_statement_parse_context_t *context);
static node_t *parse_stmt_return(psx_statement_parse_context_t *context);
static node_t *parse_stmt_if(psx_statement_parse_context_t *context);
static node_t *parse_stmt_while(psx_statement_parse_context_t *context);
static node_t *parse_stmt_do_while(psx_statement_parse_context_t *context);
static node_t *parse_stmt_for(psx_statement_parse_context_t *context);
static node_t *parse_stmt_switch(psx_statement_parse_context_t *context);
static node_t *parse_stmt_case(psx_statement_parse_context_t *context);
static node_t *parse_stmt_default(psx_statement_parse_context_t *context);
static node_t *parse_stmt_break(
    psx_statement_parse_context_t *context);
static node_t *parse_stmt_continue(
    psx_statement_parse_context_t *context);
static node_t *parse_stmt_goto(psx_statement_parse_context_t *context);
static node_t *parse_stmt_label(psx_statement_parse_context_t *context);

static node_t *stmt_internal(psx_statement_parse_context_t *context) {
  // 空文（null statement）: C11 6.8.3 — セミコロンだけの文
  if (tk_consume_ctx(context->tokenizer_context, ';'))
    return psx_node_new_syntax_int_in(
        context->arena_context, 0, NULL);
  if (curtok(context)->kind == TK_LBRACE) return parse_stmt_block(context);
  if (is_label_start_stmt(context)) return parse_stmt_label(context);
  if (is_decl_like_start_stmt(context)) return parse_decl_like_stmt(context);
  switch (curtok(context)->kind) {
    case TK_RETURN:   return parse_stmt_return(context);
    case TK_IF:       return parse_stmt_if(context);
    case TK_WHILE:    return parse_stmt_while(context);
    case TK_DO:       return parse_stmt_do_while(context);
    case TK_FOR:      return parse_stmt_for(context);
    case TK_SWITCH:   return parse_stmt_switch(context);
    case TK_CASE:     return parse_stmt_case(context);
    case TK_DEFAULT:  return parse_stmt_default(context);
    case TK_BREAK:    return parse_stmt_break(context);
    case TK_CONTINUE: return parse_stmt_continue(context);
    case TK_GOTO:     return parse_stmt_goto(context);
    default: break;
  }
  /* 式文 (式を評価して結果を捨てる) */
  node_t *node = parse_expression(context);
  tk_expect_ctx(context->tokenizer_context, ';');
  return node;
}

static node_t *parse_stmt_block(psx_statement_parse_context_t *context) {
  tk_consume_ctx(context->tokenizer_context, '{');
  if (context->syntax.enter_block_scope)
    context->syntax.enter_block_scope(context->syntax.context);
  ps_parser_enter_recovery_block_in(context->runtime_context);
  node_block_t *node = arena_alloc_in(
      context->arena_context, sizeof(node_block_t));
  node->base.kind = ND_BLOCK;
  int i = 0;
  int cap = 16;
  node->body = calloc(cap, sizeof(node_t*));
  while (!tk_consume_ctx(context->tokenizer_context, '}')) {
    // #pragma pack マーカーはブロック内でも透過的に処理（AST には載せない）。
    if (psx_try_consume_pragma_pack_marker_in(context->runtime_context))
      continue;
    if (i >= cap - 1) {
      cap = pda_next_cap_in(diagnostics(context), cap, i + 2);
      node->body = pda_xreallocarray_in(
          diagnostics(context), node->body, (size_t)cap,
          sizeof(node_t *));
    }
    token_t *stmt_tok = curtok(context);
    psx_lvar_usage_region_t *region =
        context->syntax.begin_usage_region
            ? context->syntax.begin_usage_region(
                  context->syntax.context)
            : NULL;
    node->body[i] = block_item(context);
    if (context->syntax.end_usage_region)
      context->syntax.end_usage_region(
          context->syntax.context, region);
    if (ps_parser_has_recoverable_syntax_error_in(
            context->runtime_context)) {
      node->body[i] = NULL;
      ps_parser_leave_recovery_block_in(context->runtime_context);
      if (context->syntax.leave_block_scope)
        context->syntax.leave_block_scope(context->syntax.context);
      return NULL;
    }
    if (node->body[i]) {
      node->body[i]->tok = stmt_tok;
      node->body[i]->usage_region = region;
    }
    i++;
  }
  node->body[i] = NULL;
  ps_parser_leave_recovery_block_in(context->runtime_context);
  if (context->syntax.leave_block_scope)
    context->syntax.leave_block_scope(context->syntax.context);
  return (node_t *)node;
}

static int is_stmt_expr_value_stmt(node_t *s) {
  if (!s || s->kind == ND_NUM) return 0;
  switch (s->kind) {
    case ND_IF:
    case ND_WHILE:
    case ND_DO_WHILE:
    case ND_FOR:
    case ND_SWITCH:
    case ND_CASE:
    case ND_DEFAULT:
    case ND_BREAK:
    case ND_CONTINUE:
    case ND_GOTO:
    case ND_LABEL:
    case ND_RETURN:
    case ND_BLOCK:
      return 0;
    default:
      return 1;
  }
}

node_t *psx_parse_statement_expression_syntax(
    const psx_statement_syntax_context_t *syntax_context) {
  if (!syntax_context || !syntax_context->runtime_context)
    return NULL;
  psx_parser_runtime_context_t *runtime_context =
      syntax_context->runtime_context;
  psx_statement_parse_context_t context = {
      .syntax = *syntax_context,
      .runtime_context = runtime_context,
      .arena_context = ps_parser_runtime_arena(runtime_context),
      .tokenizer_context = ps_parser_runtime_tokenizer(runtime_context),
  };
  if (!context.tokenizer_context) return NULL;
  tk_expect_ctx(context.tokenizer_context, '(');
  node_t *block = parse_stmt_block(&context);
  tk_expect_ctx(context.tokenizer_context, ')');
  node_block_t *b = (node_block_t *)block;
  node_t *value = NULL;
  if (b->body) {
    for (int i = 0; b->body[i]; i++) {
      if (is_stmt_expr_value_stmt(b->body[i])) value = b->body[i];
    }
  }
  if (!value)
    value = psx_node_new_syntax_int_in(
        context.arena_context, 0, NULL);
  node_t *node = arena_alloc_in(context.arena_context, sizeof(node_t));
  node->kind = ND_STMT_EXPR;
  node->lhs = block;
  node->rhs = value;
  return node;
}

static node_t *parse_stmt_return(
    psx_statement_parse_context_t *context) {
  token_t *return_tok = curtok(context);
  set_curtok(context, curtok(context)->next);
  node_t *node = arena_alloc_in(context->arena_context, sizeof(node_t));
  node->kind = ND_RETURN;
  node->tok = return_tok;
  if (tk_consume_ctx(context->tokenizer_context, ';')) {
    node->lhs = NULL;
    return node;
  }
  node->lhs = parse_expression(context);
  tk_expect_ctx(context->tokenizer_context, ';');
  return node;
}

static node_t *parse_stmt_if(psx_statement_parse_context_t *context) {
  set_curtok(context, curtok(context)->next);
  tk_expect_ctx(context->tokenizer_context, '(');
  node_ctrl_t *node = arena_alloc_in(
      context->arena_context, sizeof(node_ctrl_t));
  node->base.kind = ND_IF;
  node->base.lhs = parse_expression(context);
  tk_expect_ctx(context->tokenizer_context, ')');
  /* `if (cond);` のように `)` の直後に `;` が来たら空本体を警告
   * (clang -Wempty-body 相当)。 */
  if (curtok(context)->kind == TK_SEMI) node->base.has_empty_body = 1;
  node->base.rhs = stmt_internal(context);
  if (curtok(context)->kind == TK_ELSE) {
    set_curtok(context, curtok(context)->next);
    node->els = stmt_internal(context);
  }
  return (node_t *)node;
}

static node_t *parse_stmt_while(psx_statement_parse_context_t *context) {
  set_curtok(context, curtok(context)->next);
  tk_expect_ctx(context->tokenizer_context, '(');
  node_ctrl_t *node = arena_alloc_in(
      context->arena_context, sizeof(node_ctrl_t));
  node->base.kind = ND_WHILE;
  node->base.lhs = parse_expression(context);
  tk_expect_ctx(context->tokenizer_context, ')');
  node->base.rhs = stmt_internal(context);
  return (node_t *)node;
}

static node_t *parse_stmt_do_while(psx_statement_parse_context_t *context) {
  set_curtok(context, curtok(context)->next);
  node_ctrl_t *node = arena_alloc_in(
      context->arena_context, sizeof(node_ctrl_t));
  node->base.kind = ND_DO_WHILE;
  node->base.rhs = stmt_internal(context);
  if (curtok(context)->kind != TK_WHILE) {
    ps_diag_missing_in(
        diagnostics(context), curtok(context),
        diag_text_for_in(diagnostics(context), DIAG_TEXT_WHILE));
  }
  set_curtok(context, curtok(context)->next);
  tk_expect_ctx(context->tokenizer_context, '(');
  node->base.lhs = parse_expression(context);
  tk_expect_ctx(context->tokenizer_context, ')');
  tk_expect_ctx(context->tokenizer_context, ';');
  return (node_t *)node;
}

static node_t *parse_stmt_for(psx_statement_parse_context_t *context) {
  set_curtok(context, curtok(context)->next);
  tk_expect_ctx(context->tokenizer_context, '(');
  node_ctrl_t *node = arena_alloc_in(
      context->arena_context, sizeof(node_ctrl_t));
  node->base.kind = ND_FOR;
  int for_has_decl = 0;
  if (!tk_consume_ctx(context->tokenizer_context, ';')) {
    if (is_decl_like_start_stmt(context)) {
      for_has_decl = 1;
      if (context->syntax.enter_local_scope)
        context->syntax.enter_local_scope(context->syntax.context);
      node->init = parse_decl_like_stmt(context);
    } else {
      node->init = parse_expression(context);
      tk_expect_ctx(context->tokenizer_context, ';');
    }
  }
  if (!tk_consume_ctx(context->tokenizer_context, ';')) {
    node->base.lhs = parse_expression(context);
    tk_expect_ctx(context->tokenizer_context, ';');
  }
  if (!tk_consume_ctx(context->tokenizer_context, ')')) {
    node->inc = parse_expression(context);
    tk_expect_ctx(context->tokenizer_context, ')');
  }
  node->base.rhs = stmt_internal(context);
  if (for_has_decl && context->syntax.leave_local_scope)
    context->syntax.leave_local_scope(context->syntax.context);
  return (node_t *)node;
}

static node_t *parse_stmt_switch(psx_statement_parse_context_t *context) {
  token_t *switch_tok = curtok(context);
  set_curtok(context, curtok(context)->next);
  tk_expect_ctx(context->tokenizer_context, '(');
  node_ctrl_t *node = arena_alloc_in(
      context->arena_context, sizeof(node_ctrl_t));
  node->base.kind = ND_SWITCH;
  node->base.tok = switch_tok;
  node->base.lhs = parse_expression(context);
  tk_expect_ctx(context->tokenizer_context, ')');
  node->base.rhs = stmt_internal(context);
  return (node_t *)node;
}

static node_t *parse_stmt_case(psx_statement_parse_context_t *context) {
  token_t *case_tok = curtok(context);
  set_curtok(context, curtok(context)->next);
  node_case_t *node = arena_alloc_in(
      context->arena_context, sizeof(node_case_t));
  node->base.kind = ND_CASE;
  node->base.tok = case_tok;
  if (!context->syntax.parse_case_constant) {
    require_syntax_service(context, "parse_case_constant");
    return NULL;
  }
  node->val = context->syntax.parse_case_constant(
      context->syntax.context);
  tk_expect_ctx(context->tokenizer_context, ':');
  node->base.rhs = stmt_internal(context);
  return (node_t *)node;
}

static node_t *parse_stmt_default(psx_statement_parse_context_t *context) {
  token_t *default_tok = curtok(context);
  set_curtok(context, curtok(context)->next);
  node_default_t *node = arena_alloc_in(
      context->arena_context, sizeof(node_default_t));
  node->base.kind = ND_DEFAULT;
  node->base.tok = default_tok;
  tk_expect_ctx(context->tokenizer_context, ':');
  node->base.rhs = stmt_internal(context);
  return (node_t *)node;
}

static node_t *parse_stmt_break(
    psx_statement_parse_context_t *context) {
  set_curtok(context, curtok(context)->next);
  node_t *node = arena_alloc_in(context->arena_context, sizeof(node_t));
  node->kind = ND_BREAK;
  tk_expect_ctx(context->tokenizer_context, ';');
  return node;
}

static node_t *parse_stmt_continue(
    psx_statement_parse_context_t *context) {
  set_curtok(context, curtok(context)->next);
  node_t *node = arena_alloc_in(context->arena_context, sizeof(node_t));
  node->kind = ND_CONTINUE;
  tk_expect_ctx(context->tokenizer_context, ';');
  return node;
}

static node_t *parse_stmt_goto(psx_statement_parse_context_t *context) {
  token_t *goto_tok = curtok(context);
  set_curtok(context, curtok(context)->next);
  token_ident_t *ident = tk_consume_ident_ctx(context->tokenizer_context);
  if (!ident) {
    ps_diag_missing_in(
        diagnostics(context), curtok(context),
        diag_text_for_in(
            diagnostics(context), DIAG_TEXT_GOTO_LABEL_AFTER));
  }
  node_jump_t *node = arena_alloc_in(
      context->arena_context, sizeof(node_jump_t));
  node->base.kind = ND_GOTO;
  node->name = ident->str;
  node->name_len = ident->len;
  if (context->syntax.register_goto)
    context->syntax.register_goto(
        context->syntax.context, ident->str, ident->len, goto_tok);
  tk_expect_ctx(context->tokenizer_context, ';');
  return (node_t *)node;
}

static node_t *parse_stmt_label(psx_statement_parse_context_t *context) {
  token_ident_t *ident = tk_consume_ident_ctx(context->tokenizer_context);
  tk_expect_ctx(context->tokenizer_context, ':');
  node_jump_t *node = arena_alloc_in(
      context->arena_context, sizeof(node_jump_t));
  node->base.kind = ND_LABEL;
  node->name = ident->str;
  node->name_len = ident->len;
  if (context->syntax.register_label)
    context->syntax.register_label(
        context->syntax.context, ident->str, ident->len,
        curtok(context));
  node->base.rhs = stmt_internal(context);
  return (node_t *)node;
}

node_t *psx_stmt_stmt_syntax(
    const psx_statement_syntax_context_t *syntax_context) {
  if (!syntax_context || !syntax_context->runtime_context)
    return NULL;
  psx_parser_runtime_context_t *runtime_context =
      syntax_context->runtime_context;
  psx_statement_parse_context_t context = {
      .syntax = *syntax_context,
      .runtime_context = runtime_context,
      .arena_context = ps_parser_runtime_arena(runtime_context),
      .tokenizer_context = ps_parser_runtime_tokenizer(runtime_context),
  };
  if (!context.tokenizer_context) return NULL;
  node_t *result = stmt_internal(&context);
  return result;
}
