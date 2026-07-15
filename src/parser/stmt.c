#include "stmt.h"
#include "lvar_internal.h"
#include "local_registry.h"
#include "arena.h"
#include "core.h"
#include "decl.h"
#include "diag.h"
#include "dynarray.h"
#include "enum_const.h"
#include "expr.h"
#include "node_utils.h"
#include "parser_recovery.h"
#include "runtime_context.h"
#include "semantic_ctx.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline token_t *curtok(void) {
  return tk_get_current_token();
}

static inline void set_curtok(token_t *tok) {
  tk_set_current_token(tok);
}

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_global_registry_t *global_registry;
  psx_local_registry_t *local_registry;
  psx_parser_runtime_context_t *runtime_context;
  arena_context_t *arena_context;
  const psx_local_declaration_callbacks_t *local_declarations;
} psx_statement_parse_context_t;

static node_t *stmt_internal(psx_statement_parse_context_t *context);
static node_t *parse_stmt_label(psx_statement_parse_context_t *context);
static node_t *block_item(psx_statement_parse_context_t *context);
static int is_decl_like_start_stmt(
    psx_statement_parse_context_t *context);
static node_t *parse_decl_like_stmt(
    psx_statement_parse_context_t *context);
static int is_label_start_stmt(void) {
  return curtok()->kind == TK_IDENT && curtok()->next &&
         curtok()->next->kind == TK_COLON;
}

static int is_decl_like_start_stmt(
    psx_statement_parse_context_t *context) {
  if (curtok()->kind == TK_TYPEDEF) return 1;
  if (curtok()->kind == TK_STATIC_ASSERT) return 1;
  if (psx_ctx_is_type_token(curtok()->kind) || psx_is_decl_prefix_token(curtok()->kind) ||
      psx_ctx_is_typedef_name_token_in(
          context->semantic_context, curtok())) return 1;
  if (psx_ctx_is_tag_keyword(curtok()->kind)) return 1;
  return 0;
}

static node_t *parse_decl_like_stmt(
    psx_statement_parse_context_t *context) {
  return psx_parse_local_declaration_syntax(
      context->local_declarations);
}

static node_t *block_item(psx_statement_parse_context_t *context) {
  if (is_label_start_stmt()) {
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
  if (tk_consume(';'))
    return ps_node_new_num_in(context->arena_context, 0);
  if (curtok()->kind == TK_LBRACE) return parse_stmt_block(context);
  if (is_label_start_stmt()) return parse_stmt_label(context);
  if (is_decl_like_start_stmt(context)) return parse_decl_like_stmt(context);
  switch (curtok()->kind) {
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
  node_t *node = psx_expr_expr_in_contexts(
      context->semantic_context, context->global_registry,
      context->local_registry, context->runtime_context,
      context->local_declarations);
  tk_expect(';');
  return node;
}

static node_t *parse_stmt_block(psx_statement_parse_context_t *context) {
  tk_consume('{');
  ps_ctx_enter_block_scope_in(context->semantic_context);
  ps_decl_enter_scope_in(context->local_registry);
  ps_parser_enter_recovery_block_in(context->runtime_context);
  node_block_t *node = arena_alloc_in(
      context->arena_context, sizeof(node_block_t));
  node->base.kind = ND_BLOCK;
  int i = 0;
  int cap = 16;
  node->body = calloc(cap, sizeof(node_t*));
  while (!tk_consume('}')) {
    // #pragma pack マーカーはブロック内でも透過的に処理（AST には載せない）。
    if (psx_try_consume_pragma_pack_marker_in(context->runtime_context))
      continue;
    if (i >= cap - 1) {
      cap = pda_next_cap(cap, i + 2);
      node->body = pda_xreallocarray(node->body, (size_t)cap, sizeof(node_t *));
    }
    token_t *stmt_tok = curtok();
    psx_lvar_usage_region_t *region =
        psx_decl_begin_lvar_usage_region_in(context->local_registry);
    node->body[i] = block_item(context);
    psx_decl_end_lvar_usage_region_in(context->local_registry, region);
    if (ps_parser_has_recoverable_syntax_error_in(
            context->runtime_context)) {
      node->body[i] = NULL;
      ps_parser_leave_recovery_block_in(context->runtime_context);
      ps_decl_leave_scope_in(context->local_registry);
      ps_ctx_leave_block_scope_in(context->semantic_context);
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
  ps_decl_leave_scope_in(context->local_registry);
  ps_ctx_leave_block_scope_in(context->semantic_context);
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

node_t *psx_parse_statement_expression_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    const psx_local_declaration_callbacks_t *local_declarations) {
  if (!semantic_context || !global_registry || !local_registry ||
      !runtime_context)
    return NULL;
  psx_statement_parse_context_t context = {
      .semantic_context = semantic_context,
      .global_registry = global_registry,
      .local_registry = local_registry,
      .runtime_context = runtime_context,
      .arena_context = ps_parser_runtime_arena(runtime_context),
      .local_declarations = local_declarations,
  };
  tk_expect('(');
  node_t *block = parse_stmt_block(&context);
  tk_expect(')');
  node_block_t *b = (node_block_t *)block;
  node_t *value = NULL;
  if (b->body) {
    for (int i = 0; b->body[i]; i++) {
      if (is_stmt_expr_value_stmt(b->body[i])) value = b->body[i];
    }
  }
  if (!value) value = ps_node_new_num_in(context.arena_context, 0);
  node_t *node = arena_alloc_in(context.arena_context, sizeof(node_t));
  node->kind = ND_STMT_EXPR;
  node->lhs = block;
  node->rhs = value;
  return node;
}

static node_t *parse_stmt_return(
    psx_statement_parse_context_t *context) {
  token_t *return_tok = curtok();
  set_curtok(curtok()->next);
  node_t *node = arena_alloc_in(context->arena_context, sizeof(node_t));
  node->kind = ND_RETURN;
  node->tok = return_tok;
  if (tk_consume(';')) {
    node->lhs = NULL;
    return node;
  }
  node->lhs = psx_expr_expr_in_contexts(
      context->semantic_context, context->global_registry,
      context->local_registry, context->runtime_context,
      context->local_declarations);
  tk_expect(';');
  return node;
}

static node_t *parse_stmt_if(psx_statement_parse_context_t *context) {
  set_curtok(curtok()->next);
  tk_expect('(');
  node_ctrl_t *node = arena_alloc_in(
      context->arena_context, sizeof(node_ctrl_t));
  node->base.kind = ND_IF;
  node->base.lhs = psx_expr_expr_in_contexts(
      context->semantic_context, context->global_registry,
      context->local_registry, context->runtime_context,
      context->local_declarations);
  tk_expect(')');
  /* `if (cond);` のように `)` の直後に `;` が来たら空本体を警告
   * (clang -Wempty-body 相当)。 */
  if (curtok()->kind == TK_SEMI) node->base.has_empty_body = 1;
  node->base.rhs = stmt_internal(context);
  if (curtok()->kind == TK_ELSE) {
    set_curtok(curtok()->next);
    node->els = stmt_internal(context);
  }
  return (node_t *)node;
}

static node_t *parse_stmt_while(psx_statement_parse_context_t *context) {
  set_curtok(curtok()->next);
  tk_expect('(');
  node_ctrl_t *node = arena_alloc_in(
      context->arena_context, sizeof(node_ctrl_t));
  node->base.kind = ND_WHILE;
  node->base.lhs = psx_expr_expr_in_contexts(
      context->semantic_context, context->global_registry,
      context->local_registry, context->runtime_context,
      context->local_declarations);
  tk_expect(')');
  node->base.rhs = stmt_internal(context);
  return (node_t *)node;
}

static node_t *parse_stmt_do_while(psx_statement_parse_context_t *context) {
  set_curtok(curtok()->next);
  node_ctrl_t *node = arena_alloc_in(
      context->arena_context, sizeof(node_ctrl_t));
  node->base.kind = ND_DO_WHILE;
  node->base.rhs = stmt_internal(context);
  if (curtok()->kind != TK_WHILE) {
    ps_diag_missing(curtok(), diag_text_for(DIAG_TEXT_WHILE));
  }
  set_curtok(curtok()->next);
  tk_expect('(');
  node->base.lhs = psx_expr_expr_in_contexts(
      context->semantic_context, context->global_registry,
      context->local_registry, context->runtime_context,
      context->local_declarations);
  tk_expect(')');
  tk_expect(';');
  return (node_t *)node;
}

static node_t *parse_stmt_for(psx_statement_parse_context_t *context) {
  set_curtok(curtok()->next);
  tk_expect('(');
  node_ctrl_t *node = arena_alloc_in(
      context->arena_context, sizeof(node_ctrl_t));
  node->base.kind = ND_FOR;
  int for_has_decl = 0;
  if (!tk_consume(';')) {
    if (is_decl_like_start_stmt(context)) {
      for_has_decl = 1;
      ps_decl_enter_scope_in(context->local_registry);
      node->init = parse_decl_like_stmt(context);
    } else {
      node->init = psx_expr_expr_in_contexts(
          context->semantic_context, context->global_registry,
          context->local_registry, context->runtime_context,
          context->local_declarations);
      tk_expect(';');
    }
  }
  if (!tk_consume(';')) {
    node->base.lhs = psx_expr_expr_in_contexts(
        context->semantic_context, context->global_registry,
        context->local_registry, context->runtime_context,
        context->local_declarations);
    tk_expect(';');
  }
  if (!tk_consume(')')) {
    node->inc = psx_expr_expr_in_contexts(
        context->semantic_context, context->global_registry,
        context->local_registry, context->runtime_context,
        context->local_declarations);
    tk_expect(')');
  }
  node->base.rhs = stmt_internal(context);
  if (for_has_decl) ps_decl_leave_scope_in(context->local_registry);
  return (node_t *)node;
}

static node_t *parse_stmt_switch(psx_statement_parse_context_t *context) {
  token_t *switch_tok = curtok();
  set_curtok(curtok()->next);
  tk_expect('(');
  node_ctrl_t *node = arena_alloc_in(
      context->arena_context, sizeof(node_ctrl_t));
  node->base.kind = ND_SWITCH;
  node->base.tok = switch_tok;
  node->base.lhs = psx_expr_expr_in_contexts(
      context->semantic_context, context->global_registry,
      context->local_registry, context->runtime_context,
      context->local_declarations);
  tk_expect(')');
  node->base.rhs = stmt_internal(context);
  return (node_t *)node;
}

static node_t *parse_stmt_case(psx_statement_parse_context_t *context) {
  token_t *case_tok = curtok();
  set_curtok(curtok()->next);
  node_case_t *node = arena_alloc_in(
      context->arena_context, sizeof(node_case_t));
  node->base.kind = ND_CASE;
  node->base.tok = case_tok;
  node->val = psx_parse_case_const_expr_in_context(
      context->semantic_context);
  tk_expect(':');
  node->base.rhs = stmt_internal(context);
  return (node_t *)node;
}

static node_t *parse_stmt_default(psx_statement_parse_context_t *context) {
  token_t *default_tok = curtok();
  set_curtok(curtok()->next);
  node_default_t *node = arena_alloc_in(
      context->arena_context, sizeof(node_default_t));
  node->base.kind = ND_DEFAULT;
  node->base.tok = default_tok;
  tk_expect(':');
  node->base.rhs = stmt_internal(context);
  return (node_t *)node;
}

static node_t *parse_stmt_break(
    psx_statement_parse_context_t *context) {
  set_curtok(curtok()->next);
  node_t *node = arena_alloc_in(context->arena_context, sizeof(node_t));
  node->kind = ND_BREAK;
  tk_expect(';');
  return node;
}

static node_t *parse_stmt_continue(
    psx_statement_parse_context_t *context) {
  set_curtok(curtok()->next);
  node_t *node = arena_alloc_in(context->arena_context, sizeof(node_t));
  node->kind = ND_CONTINUE;
  tk_expect(';');
  return node;
}

static node_t *parse_stmt_goto(psx_statement_parse_context_t *context) {
  token_t *goto_tok = curtok();
  set_curtok(curtok()->next);
  token_ident_t *ident = tk_consume_ident();
  if (!ident) {
    ps_diag_missing(curtok(), diag_text_for(DIAG_TEXT_GOTO_LABEL_AFTER));
  }
  node_jump_t *node = arena_alloc_in(
      context->arena_context, sizeof(node_jump_t));
  node->base.kind = ND_GOTO;
  node->name = ident->str;
  node->name_len = ident->len;
  psx_ctx_register_goto_ref_in(
      context->semantic_context, ident->str, ident->len, goto_tok);
  tk_expect(';');
  return (node_t *)node;
}

static node_t *parse_stmt_label(psx_statement_parse_context_t *context) {
  token_ident_t *ident = tk_consume_ident();
  tk_expect(':');
  node_jump_t *node = arena_alloc_in(
      context->arena_context, sizeof(node_jump_t));
  node->base.kind = ND_LABEL;
  node->name = ident->str;
  node->name_len = ident->len;
  psx_ctx_register_label_def_in(
      context->semantic_context, ident->str, ident->len, curtok());
  node->base.rhs = stmt_internal(context);
  return (node_t *)node;
}

node_t *psx_stmt_stmt_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    const psx_local_declaration_callbacks_t *local_declarations) {
  if (!semantic_context || !global_registry || !local_registry ||
      !runtime_context)
    return NULL;
  psx_statement_parse_context_t context = {
      .semantic_context = semantic_context,
      .global_registry = global_registry,
      .local_registry = local_registry,
      .runtime_context = runtime_context,
      .arena_context = ps_parser_runtime_arena(runtime_context),
      .local_declarations = local_declarations,
  };
  node_t *result = stmt_internal(&context);
  return result;
}
