#include "expr.h"
#include "../semantic/scope_graph.h"
#include "arena.h"
#include "core.h"
#include "diag.h"
#include "dynarray.h"
#include "runtime_context.h"
#include "syntax_node.h"
#include "type.h"
#include "type_builder.h"
#include "declaration_syntax.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include "../tokenizer/literals.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PS_MAX_EXPR_NEST_DEPTH 1024
#define PS_MAX_PAREN_NEST_DEPTH 1024

typedef struct {
  psx_expression_syntax_context_t syntax;
  psx_parser_runtime_context_t *runtime_context;
  arena_context_t *arena_context;
  tokenizer_context_t *tokenizer_context;
  int expr_nest_depth;
  int paren_nest_depth;
} expr_parse_ctx_t;

static inline token_t *curtok(expr_parse_ctx_t *ctx) {
  return tk_get_current_token_ctx(ctx->tokenizer_context);
}

static inline ag_diagnostic_context_t *diagnostics(
    expr_parse_ctx_t *ctx) {
  return ps_parser_runtime_diagnostics(ctx->runtime_context);
}

static inline void set_curtok(
    expr_parse_ctx_t *ctx, token_t *tok) {
  tk_set_current_token_ctx(ctx->tokenizer_context, tok);
}

typedef struct {
  psx_type_name_ref_t type_name;
  token_t *after_rparen;
} parsed_parenthesized_type_name_t;

static expr_parse_ctx_t expr_parse_ctx_default(
    const psx_expression_syntax_context_t *syntax_context) {
  psx_parser_runtime_context_t *runtime_context =
      syntax_context ? syntax_context->runtime_context : NULL;
  expr_parse_ctx_t ctx = {
      .syntax = syntax_context
                    ? *syntax_context
                    : (psx_expression_syntax_context_t){0},
      .runtime_context = runtime_context,
      .arena_context = ps_parser_runtime_arena(runtime_context),
      .tokenizer_context = ps_parser_runtime_tokenizer(runtime_context),
  };
  return ctx;
}

static int is_type_name_start_token(
    token_t *t, const expr_parse_ctx_t *ctx);
static int capture_type_name_ref_at(
    token_t *start, int runtime_bounds, psx_type_name_ref_t *out,
    token_t **out_end, expr_parse_ctx_t *ctx);
static node_t *apply_postfix(node_t *node, expr_parse_ctx_t *ctx);
static node_t *parse_compound_literal_from_type(
    psx_type_name_ref_t type_name, token_t *after_rparen,
    expr_parse_ctx_t *ctx);

static void enter_expr_nest_or_die(expr_parse_ctx_t *ctx) {
  if (!ctx) return;
  ctx->expr_nest_depth++;
  if (ctx->expr_nest_depth > PS_MAX_EXPR_NEST_DEPTH) {
    ps_diag_ctx_in(
        diagnostics(ctx), curtok(ctx), "expr",
        diag_message_for_in(
            diagnostics(ctx), DIAG_ERR_PARSER_EXPR_NEST_TOO_DEEP),
        PS_MAX_EXPR_NEST_DEPTH);
  }
}

static void leave_expr_nest(expr_parse_ctx_t *ctx) {
  if (ctx && ctx->expr_nest_depth > 0) ctx->expr_nest_depth--;
}

static void enter_paren_nest_or_die(expr_parse_ctx_t *ctx) {
  if (!ctx) return;
  ctx->paren_nest_depth++;
  if (ctx->paren_nest_depth > PS_MAX_PAREN_NEST_DEPTH) {
    ps_diag_ctx_in(
        diagnostics(ctx), curtok(ctx), "paren",
        diag_message_for_in(
            diagnostics(ctx), DIAG_ERR_PARSER_PAREN_NEST_TOO_DEEP),
        PS_MAX_PAREN_NEST_DEPTH);
  }
}

static void leave_paren_nest(expr_parse_ctx_t *ctx) {
  if (ctx && ctx->paren_nest_depth > 0) ctx->paren_nest_depth--;
}

static node_t *new_binary_with_source_op(
    expr_parse_ctx_t *ctx, psx_syntax_node_kind_t kind,
    node_t *lhs, node_t *rhs,
    token_kind_t source_op) {
  node_t *node = psx_node_new_raw_binary_in(
      ctx->arena_context, kind, lhs, rhs);
  if (node) node->source_op = source_op;
  return node;
}

static int is_type_name_start_token(
    token_t *t, const expr_parse_ctx_t *ctx) {
  return psx_token_starts_type_name_syntax(
      t, ctx ? &ctx->syntax.name_classifier : NULL);
}

static void capture_lookup_point(
    expr_parse_ctx_t *ctx, unsigned *scope_seq,
    unsigned *declaration_seq) {
  if (scope_seq) *scope_seq = PSX_SCOPE_ID_INVALID;
  if (declaration_seq) *declaration_seq = 0;
  if (ctx && ctx->syntax.capture_lookup_point)
    ctx->syntax.capture_lookup_point(
        ctx->syntax.context, scope_seq, declaration_seq);
}

static int parse_generic_assoc_type(
    psx_type_name_ref_t *out, expr_parse_ctx_t *ctx) {
  token_t *end = NULL;
  if (!capture_type_name_ref_at(
          curtok(ctx), 0, out, &end, ctx)) return 0;
  set_curtok(ctx, end);
  return 1;
}

static node_t *build_member_access(
    node_t *base, int from_ptr, token_t *op_tok, expr_parse_ctx_t *ctx) {
  token_ident_t *member = tk_consume_ident_ctx(ctx->tokenizer_context);
  if (!member) {
    ps_diag_missing_in(
        diagnostics(ctx), curtok(ctx),
        diag_text_for_in(diagnostics(ctx), DIAG_TEXT_MEMBER_NAME));
  }
  node_member_access_t *syntax = arena_alloc_in(
      ctx->arena_context, sizeof(*syntax));
  syntax->base.kind = ND_MEMBER_ACCESS;
  syntax->base.lhs = base;
  syntax->base.tok = op_tok;
  syntax->member_name = member->str;
  syntax->member_name_len = member->len;
  syntax->from_pointer = from_ptr ? 1 : 0;
  return (node_t *)syntax;
}

static node_t *parse_compound_literal_from_type(
    psx_type_name_ref_t type_name, token_t *after_rparen,
    expr_parse_ctx_t *ctx) {
  set_curtok(ctx, after_rparen);
  token_t *initializer_tok = curtok(ctx);
  node_t *initializer = ctx->syntax.parse_initializer_list
                            ? ctx->syntax.parse_initializer_list(
                                  ctx->syntax.context)
                            : NULL;
  node_t *syntax = psx_node_new_compound_literal_in(
      ctx->arena_context, type_name, initializer, initializer_tok);
  return apply_postfix(syntax, ctx);
}

static int parse_parenthesized_type_name(
    token_t *tok, parsed_parenthesized_type_name_t *out,
    expr_parse_ctx_t *ctx) {
  if (!out || !tok || tok->kind != TK_LPAREN ||
      !is_type_name_start_token(tok->next, ctx))
    return 0;
  tk_ensure_lookahead_ctx(ctx->tokenizer_context);
  token_t *end = NULL;
  psx_type_name_ref_t type_name = {0};
  if (!capture_type_name_ref_at(
          tok->next, 0, &type_name, &end, ctx) ||
      !end || end->kind != TK_RPAREN || !end->next)
    return 0;
  *out = (parsed_parenthesized_type_name_t){
      .type_name = type_name,
      .after_rparen = end->next,
  };
  return 1;
}

static int parenthesized_type_name_is_compound_literal(
    token_t *tok, expr_parse_ctx_t *ctx) {
  if (!tok || tok->kind != TK_LPAREN ||
      !is_type_name_start_token(tok->next, ctx)) {
    return 0;
  }
  psx_parsed_type_name_t syntax;
  if (!ctx->syntax.parse_type_name ||
      !ctx->syntax.parse_type_name(
          ctx->syntax.context, tok->next, 0, &syntax)) {
    return 0;
  }
  token_t *end = syntax.end;
  int is_compound = end && end->kind == TK_RPAREN && end->next &&
                    end->next->kind == TK_LBRACE;
  psx_dispose_type_name_syntax(&syntax);
  return is_compound;
}

static int capture_type_name_ref_at(
    token_t *start, int runtime_bounds, psx_type_name_ref_t *out,
    token_t **out_end, expr_parse_ctx_t *ctx) {
  if (!start || !out || !is_type_name_start_token(start, ctx)) return 0;
  psx_parsed_type_name_t *syntax =
      arena_alloc_in(ctx->arena_context, sizeof(psx_parsed_type_name_t));
  int parsed = ctx->syntax.parse_type_name &&
               ctx->syntax.parse_type_name(
                   ctx->syntax.context, start, runtime_bounds,
                   syntax);
  if (!parsed) {
    return 0;
  }
  unsigned scope_seq = 0;
  unsigned declaration_seq = 0;
  capture_lookup_point(ctx, &scope_seq, &declaration_seq);
  *out = (psx_type_name_ref_t){
      .syntax = syntax,
      .scope_seq = scope_seq,
      .declaration_seq = declaration_seq,
  };
  if (out_end) *out_end = syntax->end;
  return 1;
}

static node_t *expr_internal_ctx(expr_parse_ctx_t *ctx);
static node_t *assign_ctx(expr_parse_ctx_t *ctx);
static node_t *conditional_ctx(expr_parse_ctx_t *ctx);
static node_t *logical_or_ctx(expr_parse_ctx_t *ctx);
static node_t *logical_and_ctx(expr_parse_ctx_t *ctx);
static node_t *bit_or_ctx(expr_parse_ctx_t *ctx);
static node_t *bit_xor_ctx(expr_parse_ctx_t *ctx);
static node_t *bit_and_ctx(expr_parse_ctx_t *ctx);
static node_t *equality_ctx(expr_parse_ctx_t *ctx);
static node_t *relational_ctx(expr_parse_ctx_t *ctx);
static node_t *shift_ctx(expr_parse_ctx_t *ctx);
static node_t *add_ctx(expr_parse_ctx_t *ctx);
static node_t *mul_ctx(expr_parse_ctx_t *ctx);
static node_t *cast_ctx(expr_parse_ctx_t *ctx);
static node_t *unary_ctx(expr_parse_ctx_t *ctx);
static node_t *primary_ctx(expr_parse_ctx_t *ctx);
static node_t *apply_postfix(node_t *node, expr_parse_ctx_t *ctx);

static node_t *parse_call_postfix(node_t *callee, expr_parse_ctx_t *ctx);

node_t *psx_expr_expr_syntax(
    const psx_expression_syntax_context_t *syntax_context) {
  if (!syntax_context || !syntax_context->runtime_context)
    return NULL;
  expr_parse_ctx_t ctx = expr_parse_ctx_default(syntax_context);
  return expr_internal_ctx(&ctx);
}

node_t *psx_expr_assign_syntax(
    const psx_expression_syntax_context_t *syntax_context) {
  if (!syntax_context || !syntax_context->runtime_context)
    return NULL;
  expr_parse_ctx_t ctx = expr_parse_ctx_default(syntax_context);
  return assign_ctx(&ctx);
}

node_t *psx_expr_conditional_syntax(
    const psx_expression_syntax_context_t *syntax_context) {
  if (!syntax_context || !syntax_context->runtime_context)
    return NULL;
  expr_parse_ctx_t ctx = expr_parse_ctx_default(syntax_context);
  return conditional_ctx(&ctx);
}

static node_t *expr_internal_ctx(expr_parse_ctx_t *ctx) {
  enter_expr_nest_or_die(ctx);
  node_t *node = assign_ctx(ctx);
  while (curtok(ctx)->kind == TK_COMMA) {
    set_curtok(ctx, curtok(ctx)->next);
    node_t *rhs = assign_ctx(ctx);
    node_t *comma = psx_node_new_raw_binary_in(
        ctx->arena_context, ND_COMMA, node, rhs);
    node = comma;
  }
  leave_expr_nest(ctx);
  return node;
}

static node_t *assign_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = conditional_ctx(ctx);
  switch (curtok(ctx)->kind) {
    case TK_ASSIGN: {
      token_t *assign_tok = curtok(ctx);
      set_curtok(ctx, curtok(ctx)->next);
      node_t *rhs = assign_ctx(ctx);
      node_t *assign_node = psx_node_new_raw_assign_in(
          ctx->arena_context, node, rhs);
      assign_node->is_source_assignment = 1;
      assign_node->tok = assign_tok;
      node = (node_t *)assign_node;
      break;
    }
    case TK_PLUSEQ:
    case TK_MINUSEQ:
    case TK_MULEQ:
    case TK_DIVEQ:
    case TK_MODEQ:
    case TK_SHLEQ:
    case TK_SHREQ:
    case TK_ANDEQ:
    case TK_XOREQ:
    case TK_OREQ: {
      token_t *op_tok = curtok(ctx);
      set_curtok(ctx, curtok(ctx)->next);
      node_t *compound = psx_node_new_raw_binary_in(
          ctx->arena_context, ND_COMPOUND_ASSIGN,
          node, assign_ctx(ctx));
      compound->source_op = op_tok->kind;
      compound->tok = op_tok;
      node = compound;
      break;
    }
    default: break;
  }
  return node;
}

static node_t *conditional_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = logical_or_ctx(ctx);
  if (curtok(ctx)->kind == TK_QUESTION) {
    set_curtok(ctx, curtok(ctx)->next);
    node_ctrl_t *ternary = arena_alloc_in(
        ctx->arena_context, sizeof(node_ctrl_t));
    ternary->base.kind = ND_TERNARY;
    ternary->base.lhs = node;
    ternary->base.rhs = expr_internal_ctx(ctx);
    tk_expect_ctx(ctx->tokenizer_context, ':');
    ternary->els = conditional_ctx(ctx);
    return (node_t *)ternary;
  }
  return node;
}

static node_t *logical_or_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = logical_and_ctx(ctx);
  while (curtok(ctx)->kind == TK_OROR) {
    set_curtok(ctx, curtok(ctx)->next);
    node_t *rhs = logical_and_ctx(ctx);
    node = new_binary_with_source_op(
        ctx, ND_LOGOR, node, rhs, TK_OROR);
  }
  return node;
}

static node_t *logical_and_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = bit_or_ctx(ctx);
  while (curtok(ctx)->kind == TK_ANDAND) {
    set_curtok(ctx, curtok(ctx)->next);
    node_t *rhs = bit_or_ctx(ctx);
    node = new_binary_with_source_op(
        ctx, ND_LOGAND, node, rhs, TK_ANDAND);
  }
  return node;
}

static node_t *bit_or_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = bit_xor_ctx(ctx);
  while (curtok(ctx)->kind == TK_PIPE) {
    set_curtok(ctx, curtok(ctx)->next);
    node = psx_node_new_raw_binary_in(
        ctx->arena_context, ND_BITOR, node, bit_xor_ctx(ctx));
  }
  return node;
}

static node_t *bit_xor_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = bit_and_ctx(ctx);
  while (curtok(ctx)->kind == TK_CARET) {
    set_curtok(ctx, curtok(ctx)->next);
    node = psx_node_new_raw_binary_in(
        ctx->arena_context, ND_BITXOR, node, bit_and_ctx(ctx));
  }
  return node;
}

static node_t *bit_and_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = equality_ctx(ctx);
  while (curtok(ctx)->kind == TK_AMP) {
    set_curtok(ctx, curtok(ctx)->next);
    node = psx_node_new_raw_binary_in(
        ctx->arena_context, ND_BITAND, node, equality_ctx(ctx));
  }
  return node;
}

static node_t *equality_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = relational_ctx(ctx);
  for (;;) {
    if (curtok(ctx)->kind == TK_EQEQ) {
      set_curtok(ctx, curtok(ctx)->next);
      node_t *rhs = relational_ctx(ctx);
      node = new_binary_with_source_op(ctx, ND_EQ, node, rhs, TK_EQEQ);
    } else if (curtok(ctx)->kind == TK_NEQ) {
      set_curtok(ctx, curtok(ctx)->next);
      node_t *rhs = relational_ctx(ctx);
      node = new_binary_with_source_op(ctx, ND_NE, node, rhs, TK_NEQ);
    }
    else return node;
  }
}

static node_t *relational_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = shift_ctx(ctx);
  for (;;) {
    if (curtok(ctx)->kind == TK_LT) {
      set_curtok(ctx, curtok(ctx)->next);
      node_t *rhs = shift_ctx(ctx);
      node = new_binary_with_source_op(ctx, ND_LT, node, rhs, TK_LT);
    } else if (curtok(ctx)->kind == TK_LE) {
      set_curtok(ctx, curtok(ctx)->next);
      node_t *rhs = shift_ctx(ctx);
      node = new_binary_with_source_op(ctx, ND_LE, node, rhs, TK_LE);
    } else if (curtok(ctx)->kind == TK_GT) {
      set_curtok(ctx, curtok(ctx)->next);
      node_t *rhs = shift_ctx(ctx);
      node = new_binary_with_source_op(ctx, ND_GT, node, rhs, TK_GT);
    } else if (curtok(ctx)->kind == TK_GE) {
      set_curtok(ctx, curtok(ctx)->next);
      node_t *rhs = shift_ctx(ctx);
      node = new_binary_with_source_op(ctx, ND_GE, node, rhs, TK_GE);
    }
    else return node;
  }
}

static node_t *shift_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = add_ctx(ctx);
  for (;;) {
    if (curtok(ctx)->kind == TK_SHL) {
      set_curtok(ctx, curtok(ctx)->next);
      node_t *rhs = add_ctx(ctx);
      node = new_binary_with_source_op(ctx, ND_SHL, node, rhs, TK_SHL);
    } else if (curtok(ctx)->kind == TK_SHR) {
      set_curtok(ctx, curtok(ctx)->next);
      node_t *rhs = add_ctx(ctx);
      node = new_binary_with_source_op(ctx, ND_SHR, node, rhs, TK_SHR);
    }
    else return node;
  }
}

static node_t *add_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = mul_ctx(ctx);
  for (;;) {
    if (curtok(ctx)->kind == TK_PLUS) {
      set_curtok(ctx, curtok(ctx)->next);
      node_t *rhs = mul_ctx(ctx);
      node = new_binary_with_source_op(ctx, ND_ADD, node, rhs, TK_PLUS);
    } else if (curtok(ctx)->kind == TK_MINUS) {
      set_curtok(ctx, curtok(ctx)->next);
      node_t *rhs = mul_ctx(ctx);
      node = new_binary_with_source_op(ctx, ND_SUB, node, rhs, TK_MINUS);
    }
    else return node;
  }
}

static node_t *mul_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = cast_ctx(ctx);
  for (;;) {
    if (curtok(ctx)->kind == TK_MUL) {
      set_curtok(ctx, curtok(ctx)->next);
      node_t *rhs = cast_ctx(ctx);
      node = new_binary_with_source_op(ctx, ND_MUL, node, rhs, TK_MUL);
    } else if (curtok(ctx)->kind == TK_DIV) {
      set_curtok(ctx, curtok(ctx)->next);
      node_t *rhs = cast_ctx(ctx);
      node = new_binary_with_source_op(ctx, ND_DIV, node, rhs, TK_DIV);
    } else if (curtok(ctx)->kind == TK_MOD) {
      set_curtok(ctx, curtok(ctx)->next);
      node_t *rhs = cast_ctx(ctx);
      node = new_binary_with_source_op(ctx, ND_MOD, node, rhs, TK_MOD);
    }
    else return node;
  }
}

static node_t *cast_ctx(expr_parse_ctx_t *ctx) {
  token_t *cast_tok = curtok(ctx);
  if (parenthesized_type_name_is_compound_literal(curtok(ctx), ctx)) {
    return unary_ctx(ctx);
  }
  parsed_parenthesized_type_name_t parsed_type;
  if (parse_parenthesized_type_name(curtok(ctx), &parsed_type, ctx)) {
    set_curtok(ctx, parsed_type.after_rparen);
    node_t *operand = cast_ctx(ctx);
    node_t *source_cast =
        psx_node_new_source_cast_in(
            ctx->arena_context, operand, parsed_type.type_name);
    source_cast->tok = cast_tok;
    return apply_postfix(source_cast, ctx);
  }
  return unary_ctx(ctx);
}

static node_t *parse_sizeof_operand(expr_parse_ctx_t *ctx, token_t *op_tok) {
  node_sizeof_query_t *query = arena_alloc_in(
      ctx->arena_context, sizeof(node_sizeof_query_t));
  query->base.kind = ND_SIZEOF_QUERY;
  query->base.tok = op_tok;

  if (curtok(ctx)->kind == TK_LPAREN) {
    psx_type_name_ref_t captured = {0};
    token_t *type_end = NULL;
    if (capture_type_name_ref_at(
            curtok(ctx)->next, 1, &captured, &type_end, ctx) &&
        type_end && type_end->kind == TK_RPAREN && type_end->next &&
        type_end->next->kind != TK_LBRACE) {
      query->is_type_name = 1;
      query->type_name = captured;
      set_curtok(ctx, type_end->next);
      return (node_t *)query;
    }
    token_t *outer = curtok(ctx);
    captured = (psx_type_name_ref_t){0};
    type_end = NULL;
    if (outer->next && outer->next->kind == TK_LPAREN &&
        capture_type_name_ref_at(
            outer->next->next, 1, &captured, &type_end, ctx) &&
        type_end && type_end->kind == TK_RPAREN && type_end->next &&
        type_end->next->kind == TK_RPAREN) {
      query->is_type_name = 1;
      query->type_name = captured;
      set_curtok(ctx, type_end->next->next);
      return (node_t *)query;
    }

    set_curtok(ctx, curtok(ctx)->next);
    query->operand = expr_internal_ctx(ctx);
    tk_expect_ctx(ctx->tokenizer_context, ')');
    return (node_t *)query;
  }

  query->operand = unary_ctx(ctx);
  return (node_t *)query;
}

static node_t *parse_alignof_type_name(
    token_t *op_tok, expr_parse_ctx_t *ctx) {
  node_alignof_query_t *query = arena_alloc_in(
      ctx->arena_context, sizeof(node_alignof_query_t));
  query->base.kind = ND_ALIGNOF_QUERY;
  query->base.tok = op_tok;

  psx_type_name_ref_t captured = {0};
  token_t *type_end = NULL;
  if (capture_type_name_ref_at(
          curtok(ctx), 0, &captured, &type_end, ctx) &&
      type_end && type_end->kind == TK_RPAREN) {
    query->type_name = captured;
    set_curtok(ctx, type_end->next);
    return (node_t *)query;
  }
  token_t *outer = curtok(ctx);
  captured = (psx_type_name_ref_t){0};
  type_end = NULL;
  if (outer && outer->kind == TK_LPAREN &&
      capture_type_name_ref_at(
          outer->next, 0, &captured, &type_end, ctx) &&
      type_end && type_end->kind == TK_RPAREN && type_end->next &&
      type_end->next->kind == TK_RPAREN) {
    query->type_name = captured;
    set_curtok(ctx, type_end->next->next);
    return (node_t *)query;
  }
  ps_diag_ctx_in(
      diagnostics(ctx), curtok(ctx), "alignof", "%s",
      diag_message_for_in(
          diagnostics(ctx),
          DIAG_ERR_PARSER_ALIGNOF_TYPE_NAME_REQUIRED));
  return (node_t *)query;
}

static node_t *build_pre_inc_dec_node(
    psx_syntax_node_kind_t kind, token_t *op_tok,
    expr_parse_ctx_t *ctx) {
  node_t *target = unary_ctx(ctx);
  node_t *node = arena_alloc_in(ctx->arena_context, sizeof(node_t));
  node->kind = kind;
  node->lhs = target;
  node->tok = op_tok;
  return node;
}

static node_t *build_unary_deref_syntax(
    node_t *operand, token_t *op_tok, expr_parse_ctx_t *ctx) {
  node_t *syntax = psx_node_new_unary_deref_syntax_for_in(
      ctx->arena_context, operand);
  syntax->tok = op_tok;
  return syntax;
}

static node_t *unary_ctx(expr_parse_ctx_t *ctx) {
  token_kind_t k = curtok(ctx)->kind;
  if (k == TK_SIZEOF) {
    token_t *op_tok = curtok(ctx);
    set_curtok(ctx, curtok(ctx)->next);
    return parse_sizeof_operand(ctx, op_tok);
  }
  /* GNU 拡張 __real__ / __imag__: 複素数の実部/虚部を取り出す単項演算子
   * (実数オペランドでは __real__ x = x, __imag__ x = 0)。キーワードではなく
   * 特殊識別子として扱う (__func__ と同様)。creal/cimag を rvalue にも効かせる。 */
  if (k == TK_IDENT) {
    token_ident_t *kid = (token_ident_t *)curtok(ctx);
    if (kid->len == 8 && (memcmp(kid->str, "__real__", 8) == 0 ||
                          memcmp(kid->str, "__imag__", 8) == 0)) {
      int is_real = (kid->str[2] == 'r');
      set_curtok(ctx, curtok(ctx)->next);
      node_t *operand = cast_ctx(ctx);
      node_t *n = arena_alloc_in(ctx->arena_context, sizeof(node_t));
      n->kind = is_real ? ND_CREAL : ND_CIMAG;
      n->lhs = operand;
      n->tok = (token_t *)kid;
      return n;
    }
  }
  if (k == TK_ALIGNOF) {
    token_t *op_tok = curtok(ctx);
    set_curtok(ctx, curtok(ctx)->next);
    tk_expect_ctx(ctx->tokenizer_context, '(');
    return parse_alignof_type_name(op_tok, ctx);
  }
  if (k == TK_INC || k == TK_DEC) {
    token_t *op_tok = curtok(ctx);
    set_curtok(ctx, curtok(ctx)->next);
    return build_pre_inc_dec_node(
        k == TK_INC ? ND_PRE_INC : ND_PRE_DEC, op_tok, ctx);
  }
  if (k == TK_PLUS) {
    token_t *op_tok = curtok(ctx);
    set_curtok(ctx, curtok(ctx)->next);
    node_t *unary_plus = arena_alloc_in(
        ctx->arena_context, sizeof(node_t));
    unary_plus->kind = ND_UNARY_PLUS;
    unary_plus->lhs = cast_ctx(ctx);
    unary_plus->tok = op_tok;
    return unary_plus;
  }
  if (k == TK_MINUS) {
    token_t *op_tok = curtok(ctx);
    set_curtok(ctx, curtok(ctx)->next);
    node_t *operand = cast_ctx(ctx);
    node_t *negate = arena_alloc_in(ctx->arena_context, sizeof(node_t));
    negate->kind = ND_UNARY_NEGATE;
    negate->lhs = operand;
    negate->tok = op_tok;
    return negate;
  }
  if (k == TK_BANG)  {
    token_t *op_tok = curtok(ctx);
    set_curtok(ctx, curtok(ctx)->next);
    node_t *logical_not = arena_alloc_in(
        ctx->arena_context, sizeof(node_t));
    logical_not->kind = ND_LOGICAL_NOT;
    logical_not->lhs = cast_ctx(ctx);
    logical_not->tok = op_tok;
    return logical_not;
  }
  if (k == TK_TILDE) {
    token_t *op_tok = curtok(ctx);
    set_curtok(ctx, curtok(ctx)->next);
    node_t *bitwise_not = arena_alloc_in(
        ctx->arena_context, sizeof(node_t));
    bitwise_not->kind = ND_BITWISE_NOT;
    bitwise_not->lhs = cast_ctx(ctx);
    bitwise_not->tok = op_tok;
    return bitwise_not;
  }
  if (k == TK_MUL) {
    token_t *op_tok = curtok(ctx);
    set_curtok(ctx, curtok(ctx)->next);
    return build_unary_deref_syntax(cast_ctx(ctx), op_tok, ctx);
  }
  if (k == TK_AMP) {
    token_t *op_tok = curtok(ctx);
    set_curtok(ctx, curtok(ctx)->next);
    node_t *operand = cast_ctx(ctx);
    node_t *address = psx_node_new_unary_addr_syntax_for_in(
        ctx->arena_context, operand);
    address->tok = op_tok;
    return address;
  }
  return apply_postfix(primary_ctx(ctx), ctx);
}


// `left[right]` の構文をそのまま保持する。operand の判定と正規化は semantic pass が行う。
static node_t *build_subscript_syntax(node_t *node, node_t *idx,
                                      token_t *op_tok,
                                      expr_parse_ctx_t *ctx) {
  node_t *syntax = psx_node_new_subscript_syntax_for_in(
      ctx->arena_context, node, idx);
  syntax->tok = op_tok;
  return syntax;
}

static node_t *build_post_inc_dec_node(
    psx_syntax_node_kind_t kind, node_t *operand, token_t *op_tok,
    expr_parse_ctx_t *ctx) {
  node_t *n = arena_alloc_in(ctx->arena_context, sizeof(node_t));
  n->kind = kind;
  n->lhs = operand;
  n->tok = op_tok;
  return n;
}

static node_t *apply_postfix(node_t *node, expr_parse_ctx_t *ctx) {
  for (;;) {
    token_kind_t k = curtok(ctx)->kind;
    if (k == TK_LBRACKET) {
      token_t *op_tok = curtok(ctx);
      set_curtok(ctx, curtok(ctx)->next);
      node_t *idx = expr_internal_ctx(ctx);
      tk_expect_ctx(ctx->tokenizer_context, ']');
      node = build_subscript_syntax(node, idx, op_tok, ctx);
      continue;
    }
    if (k == TK_LPAREN) {
      node = parse_call_postfix(node, ctx);
      continue;
    }
    if (k == TK_DOT || k == TK_ARROW) {
      token_t *op_tok = curtok(ctx);
      set_curtok(ctx, curtok(ctx)->next);
      node = build_member_access(
          node, k == TK_ARROW ? 1 : 0, op_tok, ctx);
      continue;
    }
    if (k == TK_INC) {
      token_t *op_tok = curtok(ctx);
      set_curtok(ctx, curtok(ctx)->next);
      node = build_post_inc_dec_node(ND_POST_INC, node, op_tok, ctx);
      continue;
    }
    if (k == TK_DEC) {
      token_t *op_tok = curtok(ctx);
      set_curtok(ctx, curtok(ctx)->next);
      node = build_post_inc_dec_node(ND_POST_DEC, node, op_tok, ctx);
      continue;
    }
    return node;
  }
}

static node_t *parse_call_postfix(node_t *callee, expr_parse_ctx_t *ctx) {
  token_t *call_tok = curtok(ctx);
  tk_expect_ctx(ctx->tokenizer_context, '(');
  node_function_call_t *node =
      arena_alloc_in(ctx->arena_context, sizeof(node_function_call_t));
  node->base.kind = ND_FUNCALL;
  node->base.tok = call_tok;
  node->callee = callee;
  int nargs = 0;
  int arg_cap = 16;
  node->arguments = calloc(arg_cap, sizeof(node_t *));
  if (curtok(ctx)->kind == TK_RPAREN) {
    set_curtok(ctx, curtok(ctx)->next);
  } else {
    node->arguments[nargs++] = assign_ctx(ctx);
    while (curtok(ctx)->kind == TK_COMMA) {
      set_curtok(ctx, curtok(ctx)->next);
      if (nargs >= arg_cap) {
        arg_cap = pda_next_cap_in(
            diagnostics(ctx), arg_cap, nargs + 1);
        node->arguments = pda_xreallocarray_in(
            diagnostics(ctx), node->arguments,
            (size_t)arg_cap, sizeof(node_t *));
      }
      node->arguments[nargs++] = assign_ctx(ctx);
    }
    tk_expect_ctx(ctx->tokenizer_context, ')');
  }
  node->argument_count = nargs;
  return (node_t *)node;
}

// TK_LPAREN を見たときの compound literal `(T){...}` 試行。
// パースできたら結果ノードを返し、できなければ NULL（呼び出し側は通常の式へ）。
static node_t *try_parse_compound_literal(expr_parse_ctx_t *ctx) {
  parsed_parenthesized_type_name_t parsed_type;
  if (curtok(ctx)->kind == TK_LPAREN &&
      parse_parenthesized_type_name(curtok(ctx), &parsed_type, ctx) &&
      parsed_type.after_rparen &&
      parsed_type.after_rparen->kind == TK_LBRACE) {
    return parse_compound_literal_from_type(
        parsed_type.type_name, parsed_type.after_rparen, ctx);
  }
  return NULL;
}

static node_t *parse_generic_selection(expr_parse_ctx_t *ctx) {
  token_t *generic_tok = curtok(ctx);
  set_curtok(ctx, curtok(ctx)->next);
  tk_expect_ctx(ctx->tokenizer_context, '(');

  node_t *control = assign_ctx(ctx);
  tk_expect_ctx(ctx->tokenizer_context, ',');

  int count = 0;
  int capacity = 4;
  psx_generic_association_t *associations =
      calloc((size_t)capacity, sizeof(psx_generic_association_t));
  for (;;) {
    if (count >= capacity) {
      capacity = pda_next_cap_in(
          diagnostics(ctx), capacity, count + 1);
      associations = pda_xreallocarray_in(
          diagnostics(ctx), associations, (size_t)capacity,
          sizeof(psx_generic_association_t));
    }
    psx_generic_association_t *association = &associations[count++];
    *association = (psx_generic_association_t){0};
    association->tok = curtok(ctx);
    if (curtok(ctx)->kind == TK_DEFAULT) {
      association->is_default = 1;
      set_curtok(ctx, curtok(ctx)->next);
    } else if (!parse_generic_assoc_type(
                   &association->type_name, ctx)) {
      ps_diag_ctx_in(
          diagnostics(ctx), curtok(ctx), "generic", "%s",
          diag_message_for_in(
              diagnostics(ctx),
              DIAG_ERR_PARSER_GENERIC_ASSOC_TYPE_INVALID));
    }
    tk_expect_ctx(ctx->tokenizer_context, ':');
    association->expression = assign_ctx(ctx);
    if (!tk_consume_ctx(ctx->tokenizer_context, ',')) break;
  }
  tk_expect_ctx(ctx->tokenizer_context, ')');

  node_generic_selection_t *selection =
      arena_alloc_in(ctx->arena_context, sizeof(node_generic_selection_t));
  selection->base.kind = ND_GENERIC_SELECTION;
  selection->base.tok = generic_tok;
  selection->control = control;
  selection->associations = associations;
  selection->association_count = count;
  return (node_t *)selection;
}

static node_t *parse_num_literal(expr_parse_ctx_t *ctx) {
  token_t *tok = curtok(ctx);
  token_num_t *num = (token_num_t *)tok;
  node_num_t *node = arena_alloc_in(ctx->arena_context, sizeof(node_num_t));
  node->base.kind = ND_NUM;
  node->base.tok = tok;
  if (num->num_kind == TK_NUM_KIND_INT) {
    node->float_suffix_kind = TK_FLOAT_SUFFIX_NONE;
    node->val = tk_as_num_int(tok)->val;
  } else {
    node->float_suffix_kind = tk_as_num_float(tok)->float_suffix_kind;
    node->fval = tk_as_num_float(tok)->fval;
  }
  set_curtok(ctx, curtok(ctx)->next);
  return (node_t *)node;
}

// 内容文字列・幅・プレフィックスから型なし ND_STRING syntaxを生成する。
static node_string_t *make_string_lit_node(
                                           expr_parse_ctx_t *ctx,
                                           char *str, int len,
                                           tk_char_width_t char_width,
                                           tk_string_prefix_kind_t prefix_kind) {
  node_string_t *snode = arena_alloc_in(
      ctx->arena_context, sizeof(node_string_t));
  snode->base.kind = ND_STRING;
  snode->literal_contents = str;
  snode->literal_length = len;
  /* 文字列リテラルは char (または wchar) 配列で、式中ではポインタに decay する。
   * `"abc"[1]` の subscript チェックや (ptr + n) のスケーリングに使う。 */
  snode->char_width = char_width ? char_width : TK_CHAR_WIDTH_CHAR;
  snode->str_prefix_kind = prefix_kind;
  /* byte_len は「デコード後」の内容長 (要素数)。str はソースのまま (`\t` 等の
   * エスケープシーケンスを含む raw) なので、エスケープを 1 要素に畳んで数える。
   * これがないと sizeof("\t") が raw の 2(+1) を返していた (正しくは 1+1)。 */
  snode->byte_len = tk_count_string_code_units(str, len,
                                               char_width ? (int)char_width
                                                          : TK_CHAR_WIDTH_CHAR);
  return snode;
}

// 連続する TK_STRING リテラルを結合して 1 つの ND_STRING ノードを返す。
static node_t *parse_string_literal_sequence(expr_parse_ctx_t *ctx) {
  tk_char_width_t merged_width = TK_CHAR_WIDTH_CHAR;
  tk_string_prefix_kind_t merged_prefix_kind = TK_STR_PREFIX_NONE;
  size_t total_len = 0;
  token_t *t = curtok(ctx);
  while (t && t->kind == TK_STRING) {
    token_string_t *st = (token_string_t *)t;
    /* char_width 0 は接頭辞なし (通常の char 文字列) として扱う。stringize `#x` の
     * 結果トークンは char_width を 0 のままにするため、`"a" S(b)` のように 2 番目以降に
     * 来ると CHAR(1) と不一致になり E3002 で誤って弾かれていた (先頭に来る `S(a) "b"`
     * は下の正規化で通っていた)。比較側も 0→CHAR に正規化する。 */
    tk_char_width_t tw = st->char_width ? st->char_width : TK_CHAR_WIDTH_CHAR;
    if (total_len == 0) {
      merged_width = tw;
      merged_prefix_kind = st->str_prefix_kind;
    } else if (merged_width != tw) {
      diag_emit_tokf_in(
          diagnostics(ctx), DIAG_ERR_PARSER_UNEXPECTED_TOKEN, t, "%s",
          diag_message_for_in(
              diagnostics(ctx),
              DIAG_ERR_PARSER_STRING_PREFIX_MISMATCH));
    }
    if (st->len < 0 || (size_t)st->len > SIZE_MAX - total_len - 1) {
      diag_emit_tokf_in(
          diagnostics(ctx), DIAG_ERR_PARSER_STRING_LITERAL_TOO_LARGE,
          t, "%s",
          diag_message_for_in(
              diagnostics(ctx),
              DIAG_ERR_PARSER_STRING_LITERAL_TOO_LARGE));
    }
    total_len += (size_t)st->len;
    t = t->next;
  }
  if (total_len > (size_t)INT_MAX) {
    diag_emit_tokf_in(
        diagnostics(ctx), DIAG_ERR_PARSER_STRING_LITERAL_TOO_LARGE,
        curtok(ctx), "%s",
        diag_message_for_in(
            diagnostics(ctx),
            DIAG_ERR_PARSER_STRING_LITERAL_TOO_LARGE));
  }
  char *merged = calloc(total_len + 1, 1);
  if (!merged) {
    diag_emit_internalf_in(
        diagnostics(ctx), DIAG_ERR_INTERNAL_OOM, "%s",
        diag_message_for_in(diagnostics(ctx), DIAG_ERR_INTERNAL_OOM));
  }
  size_t off = 0;
  while (curtok(ctx) && curtok(ctx)->kind == TK_STRING) {
    token_string_t *st = (token_string_t *)curtok(ctx);
    if (st->len < 0 || (size_t)st->len > total_len - off) {
      diag_emit_tokf_in(
          diagnostics(ctx), DIAG_ERR_PARSER_STRING_CONCAT_SIZE_INVALID,
          curtok(ctx), "%s",
          diag_message_for_in(
              diagnostics(ctx),
              DIAG_ERR_PARSER_STRING_CONCAT_SIZE_INVALID));
    }
    memcpy(merged + off, st->str, (size_t)st->len);
    off += (size_t)st->len;
    set_curtok(ctx, curtok(ctx)->next);
  }
  return (node_t *)make_string_lit_node(
      ctx, merged, (int)total_len, merged_width, merged_prefix_kind);
}

static node_t *parse_identifier_syntax(token_ident_t *tok, expr_parse_ctx_t *ctx) {
  unsigned scope_seq = 0;
  unsigned declaration_seq = 0;
  capture_lookup_point(ctx, &scope_seq, &declaration_seq);
  node_identifier_t *identifier = arena_alloc_in(
      ctx->arena_context, sizeof(*identifier));
  identifier->base.kind = ND_IDENTIFIER;
  identifier->base.tok = (token_t *)tok;
  identifier->name = tok->str;
  identifier->name_len = tok->len;
  identifier->scope_seq = scope_seq;
  identifier->declaration_seq = declaration_seq;
  return (node_t *)identifier;
}

static node_t *primary_ctx(expr_parse_ctx_t *ctx) {
  node_t *cl = try_parse_compound_literal(ctx);
  if (cl) return cl;

  if (curtok(ctx)->kind == TK_GENERIC) return parse_generic_selection(ctx);

  if (curtok(ctx)->kind == TK_NUM) return parse_num_literal(ctx);

  if (curtok(ctx)->kind == TK_LPAREN && curtok(ctx)->next &&
      curtok(ctx)->next->kind == TK_LBRACE) {
    return ctx->syntax.parse_statement_expression
               ? ctx->syntax.parse_statement_expression(
                     ctx->syntax.context)
               : NULL;
  }

  if (curtok(ctx)->kind == TK_LPAREN) {
    enter_paren_nest_or_die(ctx);
    set_curtok(ctx, curtok(ctx)->next);
    node_t *node = expr_internal_ctx(ctx);
    tk_expect_ctx(ctx->tokenizer_context, ')');
    leave_paren_nest(ctx);
    return node;
  }

  token_ident_t *tok = tk_consume_ident_ctx(ctx->tokenizer_context);
  if (tok) return parse_identifier_syntax(tok, ctx);

  if (curtok(ctx)->kind == TK_STRING) {
    return parse_string_literal_sequence(ctx);
  }

  ps_diag_ctx_in(
      diagnostics(ctx), curtok(ctx), "primary", "%s",
      diag_message_for_in(
          diagnostics(ctx),
          DIAG_ERR_PARSER_PRIMARY_NUMBER_EXPECTED));
  return NULL;
}
