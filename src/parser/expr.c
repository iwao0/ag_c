#include "expr.h"
#include "../semantic/scope_graph.h"
#include "arena.h"
#include "core.h"
#include "diag.h"
#include "dynarray.h"
#include "runtime_context.h"
#include "syntax_node.h"
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
#define PS_DEEP_CALL_CHAIN_MIN 32

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
  token_t *type_name_token;
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
    psx_type_name_ref_t type_name, token_t *type_name_token,
    token_t *after_rparen,
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
  syntax->member_tok = (token_t *)member;
  syntax->from_pointer = from_ptr ? 1 : 0;
  return (node_t *)syntax;
}

static node_t *parse_compound_literal_from_type(
    psx_type_name_ref_t type_name, token_t *type_name_token,
    token_t *after_rparen,
    expr_parse_ctx_t *ctx) {
  set_curtok(ctx, after_rparen);
  token_t *initializer_tok = curtok(ctx);
  node_t *initializer = ctx->syntax.parse_initializer_list
                            ? ctx->syntax.parse_initializer_list(
                                  ctx->syntax.context)
                            : NULL;
  node_t *syntax = psx_node_new_compound_literal_in(
      ctx->arena_context, type_name, type_name_token,
      initializer, initializer_tok);
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
      .type_name_token = tok,
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
static node_t *cast_ctx(expr_parse_ctx_t *ctx);
static node_t *unary_ctx(expr_parse_ctx_t *ctx);
static node_t *primary_ctx(expr_parse_ctx_t *ctx);
static node_t *parse_identifier_syntax(
    token_ident_t *tok, expr_parse_ctx_t *ctx);
static node_t *apply_postfix(node_t *node, expr_parse_ctx_t *ctx);

static node_t *parse_call_postfix(node_t *callee, expr_parse_ctx_t *ctx);
static node_t *parse_deep_first_argument_call_chain(
    expr_parse_ctx_t *ctx, size_t depth);
static node_t *parse_call_argument(expr_parse_ctx_t *ctx);

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
    token_t *comma_tok = curtok(ctx);
    set_curtok(ctx, comma_tok->next);
    node_t *rhs = assign_ctx(ctx);
    node_t *comma = psx_node_new_raw_binary_in(
        ctx->arena_context, ND_COMMA, node, rhs);
    comma->tok = comma_tok;
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
    token_t *question_tok = curtok(ctx);
    set_curtok(ctx, question_tok->next);
    node_ctrl_t *ternary = arena_alloc_in(
        ctx->arena_context, sizeof(node_ctrl_t));
    ternary->base.kind = ND_TERNARY;
    ternary->base.tok = question_tok;
    ternary->base.lhs = node;
    ternary->then_token = curtok(ctx);
    ternary->base.rhs = expr_internal_ctx(ctx);
    tk_expect_ctx(ctx->tokenizer_context, ':');
    ternary->else_token = curtok(ctx);
    ternary->els = conditional_ctx(ctx);
    return (node_t *)ternary;
  }
  return node;
}

typedef struct {
  int precedence;
  psx_syntax_node_kind_t node_kind;
  int records_source_op;
} binary_operator_spec_t;

static int binary_operator_spec(
    token_kind_t token_kind, binary_operator_spec_t *spec) {
  binary_operator_spec_t result = {0};
  switch (token_kind) {
    case TK_OROR: result = (binary_operator_spec_t){1, ND_LOGOR, 1}; break;
    case TK_ANDAND: result = (binary_operator_spec_t){2, ND_LOGAND, 1}; break;
    case TK_PIPE: result = (binary_operator_spec_t){3, ND_BITOR, 0}; break;
    case TK_CARET: result = (binary_operator_spec_t){4, ND_BITXOR, 0}; break;
    case TK_AMP: result = (binary_operator_spec_t){5, ND_BITAND, 0}; break;
    case TK_EQEQ: result = (binary_operator_spec_t){6, ND_EQ, 1}; break;
    case TK_NEQ: result = (binary_operator_spec_t){6, ND_NE, 1}; break;
    case TK_LT: result = (binary_operator_spec_t){7, ND_LT, 1}; break;
    case TK_LE: result = (binary_operator_spec_t){7, ND_LE, 1}; break;
    case TK_GT: result = (binary_operator_spec_t){7, ND_GT, 1}; break;
    case TK_GE: result = (binary_operator_spec_t){7, ND_GE, 1}; break;
    case TK_SHL: result = (binary_operator_spec_t){8, ND_SHL, 1}; break;
    case TK_SHR: result = (binary_operator_spec_t){8, ND_SHR, 1}; break;
    case TK_PLUS: result = (binary_operator_spec_t){9, ND_ADD, 1}; break;
    case TK_MINUS: result = (binary_operator_spec_t){9, ND_SUB, 1}; break;
    case TK_MUL: result = (binary_operator_spec_t){10, ND_MUL, 1}; break;
    case TK_DIV: result = (binary_operator_spec_t){10, ND_DIV, 1}; break;
    case TK_MOD: result = (binary_operator_spec_t){10, ND_MOD, 1}; break;
    default: return 0;
  }
  if (spec) *spec = result;
  return 1;
}

static node_t *binary_ctx(expr_parse_ctx_t *ctx, int min_precedence) {
  node_t *node = cast_ctx(ctx);
  for (;;) {
    token_t *operator_tok = curtok(ctx);
    token_kind_t token_kind = operator_tok->kind;
    binary_operator_spec_t spec;
    if (!binary_operator_spec(token_kind, &spec) ||
        spec.precedence < min_precedence)
      return node;
    set_curtok(ctx, operator_tok->next);
    node_t *rhs = binary_ctx(ctx, spec.precedence + 1);
    node_t *binary = psx_node_new_raw_binary_in(
        ctx->arena_context, spec.node_kind, node, rhs);
    binary->tok = operator_tok;
    if (spec.records_source_op)
      binary->source_op = token_kind;
    node = binary;
  }
}

static node_t *logical_or_ctx(expr_parse_ctx_t *ctx) {
  return binary_ctx(ctx, 1);
}

static node_t *cast_ctx(expr_parse_ctx_t *ctx) {
  token_t *cast_tok = curtok(ctx);
  if (parenthesized_type_name_is_compound_literal(curtok(ctx), ctx)) {
    return unary_ctx(ctx);
  }
  parsed_parenthesized_type_name_t parsed_type;
  if (parse_parenthesized_type_name(curtok(ctx), &parsed_type, ctx)) {
    set_curtok(ctx, parsed_type.after_rparen);
    token_t *operand_token = curtok(ctx);
    node_t *operand = cast_ctx(ctx);
    node_t *source_cast =
        psx_node_new_source_cast_in(
            ctx->arena_context, operand, parsed_type.type_name);
    source_cast->tok = cast_tok;
    ((node_source_cast_t *)source_cast)->operand_token = operand_token;
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
    token_t *lparen_tok = curtok(ctx);
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

    query->operand_token = lparen_tok->next;
    query->invalid_type_token = lparen_tok;
    set_curtok(ctx, query->operand_token);
    query->operand = expr_internal_ctx(ctx);
    tk_expect_ctx(ctx->tokenizer_context, ')');
    query->operand = apply_postfix(query->operand, ctx);
    return (node_t *)query;
  }

  query->operand_token = curtok(ctx);
  query->invalid_type_token = query->operand_token;
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

static token_t *find_call_closing_paren(token_t *token) {
  size_t paren_depth = 0;
  size_t bracket_depth = 0;
  size_t brace_depth = 0;
  for (; token; token = token->next) {
    if (token->kind == TK_EOF) return NULL;
    switch (token->kind) {
      case TK_LPAREN:
        paren_depth++;
        break;
      case TK_RPAREN:
        if (paren_depth) {
          paren_depth--;
          break;
        }
        if (bracket_depth || brace_depth) return NULL;
        return token;
      case TK_LBRACKET:
        bracket_depth++;
        break;
      case TK_RBRACKET:
        if (!bracket_depth) return NULL;
        bracket_depth--;
        break;
      case TK_LBRACE:
        brace_depth++;
        break;
      case TK_RBRACE:
        if (!brace_depth) return NULL;
        brace_depth--;
        break;
      default:
        break;
    }
  }
  return NULL;
}

static size_t deep_first_argument_call_chain_depth(
    expr_parse_ctx_t *ctx) {
  if (!ctx || !curtok(ctx)) return 0;
  token_t *token = curtok(ctx);
  size_t depth = 0;
  while (token && token->kind == TK_IDENT && token->next &&
         token->next->kind == TK_LPAREN) {
    depth++;
    token = token->next->next;
  }
  if (depth < PS_DEEP_CALL_CHAIN_MIN) return 0;

  tk_ensure_lookahead_ctx(ctx->tokenizer_context);
  token = curtok(ctx);
  depth = 0;
  while (token && token->kind == TK_IDENT && token->next &&
         token->next->kind == TK_LPAREN) {
    depth++;
    token = token->next->next;
  }

  token_t *closing = find_call_closing_paren(token);
  for (size_t remaining = depth; remaining > 1; remaining--) {
    if (!closing || !closing->next) return 0;
    token = closing->next;
    if (token->kind == TK_RPAREN) {
      closing = token;
      continue;
    }
    if (token->kind != TK_COMMA || !token->next ||
        token->next->kind == TK_COMMA ||
        token->next->kind == TK_RPAREN)
      return 0;
    closing = find_call_closing_paren(token->next);
  }
  if (!closing) return 0;
  token = closing->next;
  if (!token ||
      (token->kind != TK_RPAREN && token->kind != TK_COMMA))
    return 0;
  return depth;
}

typedef struct {
  node_t *callee;
  token_t *call_token;
  token_t *first_argument_token;
} deep_call_syntax_frame_t;

static node_t *finish_deep_call_syntax_frame(
    expr_parse_ctx_t *ctx,
    const deep_call_syntax_frame_t *frame,
    node_t *first_argument) {
  node_function_call_t *call = arena_alloc_in(
      ctx->arena_context, sizeof(*call));
  call->base.kind = ND_FUNCALL;
  call->base.tok = frame->call_token;
  call->callee = frame->callee;
  int argument_count = 0;
  int argument_capacity = 16;
  call->arguments = calloc(
      (size_t)argument_capacity, sizeof(*call->arguments));
  call->argument_tokens = calloc(
      (size_t)argument_capacity, sizeof(*call->argument_tokens));
  if (!call->arguments || !call->argument_tokens) {
    diag_emit_internalf_in(
        diagnostics(ctx), DIAG_ERR_INTERNAL_OOM, "%s",
        diag_message_for_in(diagnostics(ctx), DIAG_ERR_INTERNAL_OOM));
  }
  if (first_argument) {
    call->argument_tokens[argument_count] =
        frame->first_argument_token;
    call->arguments[argument_count++] = first_argument;
  } else if (curtok(ctx)->kind != TK_RPAREN) {
    call->argument_tokens[argument_count] = curtok(ctx);
    call->arguments[argument_count++] = parse_call_argument(ctx);
  }
  while (curtok(ctx)->kind == TK_COMMA) {
    set_curtok(ctx, curtok(ctx)->next);
    if (argument_count >= argument_capacity) {
      argument_capacity = pda_next_cap_in(
          diagnostics(ctx), argument_capacity,
          argument_count + 1);
      call->arguments = pda_xreallocarray_in(
          diagnostics(ctx), call->arguments,
          (size_t)argument_capacity, sizeof(*call->arguments));
      call->argument_tokens = pda_xreallocarray_in(
          diagnostics(ctx), call->argument_tokens,
          (size_t)argument_capacity, sizeof(*call->argument_tokens));
    }
    call->argument_tokens[argument_count] = curtok(ctx);
    call->arguments[argument_count++] = parse_call_argument(ctx);
  }
  call->closing_token = curtok(ctx);
  tk_expect_ctx(ctx->tokenizer_context, ')');
  call->argument_count = argument_count;
  return (node_t *)call;
}

static node_t *parse_deep_first_argument_call_chain(
    expr_parse_ctx_t *ctx, size_t depth) {
  deep_call_syntax_frame_t *frames =
      calloc(depth, sizeof(*frames));
  if (!frames) {
    diag_emit_internalf_in(
        diagnostics(ctx), DIAG_ERR_INTERNAL_OOM, "%s",
        diag_message_for_in(diagnostics(ctx), DIAG_ERR_INTERNAL_OOM));
  }

  for (size_t index = 0; index < depth; index++) {
    token_ident_t *identifier =
        tk_consume_ident_ctx(ctx->tokenizer_context);
    frames[index].callee = parse_identifier_syntax(identifier, ctx);
    frames[index].call_token = curtok(ctx);
    tk_expect_ctx(ctx->tokenizer_context, '(');
    frames[index].first_argument_token = curtok(ctx);
  }

  node_t *argument = finish_deep_call_syntax_frame(
      ctx, &frames[depth - 1], NULL);
  for (size_t index = depth - 1; index > 0; index--) {
    argument = finish_deep_call_syntax_frame(
        ctx, &frames[index - 1], argument);
  }
  free(frames);
  return argument;
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
  node->argument_tokens = calloc(
      (size_t)arg_cap, sizeof(*node->argument_tokens));
  if (!node->arguments || !node->argument_tokens) {
    diag_emit_internalf_in(
        diagnostics(ctx), DIAG_ERR_INTERNAL_OOM, "%s",
        diag_message_for_in(diagnostics(ctx), DIAG_ERR_INTERNAL_OOM));
  }
  if (curtok(ctx)->kind == TK_RPAREN) {
    node->closing_token = curtok(ctx);
    set_curtok(ctx, curtok(ctx)->next);
  } else {
    node->argument_tokens[nargs] = curtok(ctx);
    node->arguments[nargs++] = parse_call_argument(ctx);
    while (curtok(ctx)->kind == TK_COMMA) {
      set_curtok(ctx, curtok(ctx)->next);
      if (nargs >= arg_cap) {
        arg_cap = pda_next_cap_in(
            diagnostics(ctx), arg_cap, nargs + 1);
        node->arguments = pda_xreallocarray_in(
            diagnostics(ctx), node->arguments,
            (size_t)arg_cap, sizeof(node_t *));
        node->argument_tokens = pda_xreallocarray_in(
            diagnostics(ctx), node->argument_tokens,
            (size_t)arg_cap, sizeof(*node->argument_tokens));
      }
      node->argument_tokens[nargs] = curtok(ctx);
      node->arguments[nargs++] = parse_call_argument(ctx);
    }
    node->closing_token = curtok(ctx);
    tk_expect_ctx(ctx->tokenizer_context, ')');
  }
  node->argument_count = nargs;
  return (node_t *)node;
}

static node_t *parse_call_argument(expr_parse_ctx_t *ctx) {
  size_t deep_call_depth =
      deep_first_argument_call_chain_depth(ctx);
  return deep_call_depth
             ? parse_deep_first_argument_call_chain(
                   ctx, deep_call_depth)
             : assign_ctx(ctx);
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
        parsed_type.type_name, parsed_type.type_name_token,
        parsed_type.after_rparen, ctx);
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
  bool saw_literal = false;
  size_t total_len = 0;
  token_t *t = curtok(ctx);
  while (t && t->kind == TK_STRING) {
    token_string_t *st = (token_string_t *)t;
    /* char_width 0 は接頭辞なし (通常の char 文字列) として扱う。stringize `#x` の
     * 結果トークンは char_width を 0 のままにするため、`"a" S(b)` のように 2 番目以降に
     * 来ると CHAR(1) と不一致になり E3002 で誤って弾かれていた (先頭に来る `S(a) "b"`
     * は下の正規化で通っていた)。比較側も 0→CHAR に正規化する。 */
    tk_char_width_t tw = st->char_width ? st->char_width : TK_CHAR_WIDTH_CHAR;
    tk_string_prefix_kind_t prefix_kind =
        (tk_string_prefix_kind_t)st->str_prefix_kind;
    if (!saw_literal) {
      merged_width = tw;
      merged_prefix_kind = prefix_kind;
      saw_literal = true;
    } else if (prefix_kind != TK_STR_PREFIX_NONE &&
               merged_prefix_kind == TK_STR_PREFIX_NONE) {
      /* C11 6.4.5p5: 無接頭辞tokenは、同じsequence内の接頭辞付きtokenと
       * 同じ接頭辞として扱う。接頭辞付きtokenが後から現れる場合も、最終的な
       * 文字幅とprefix metadataをそちらへ昇格させる。 */
      merged_width = tw;
      merged_prefix_kind = prefix_kind;
    } else if (prefix_kind != TK_STR_PREFIX_NONE &&
               merged_prefix_kind != prefix_kind) {
      diag_emit_tokf_in(
          diagnostics(ctx), DIAG_ERR_PARSER_STRING_PREFIX_MISMATCH, t, "%s",
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

static int token_is_builtin_offsetof(const token_t *token) {
  if (!token || token->kind != TK_IDENT) return 0;
  const token_ident_t *identifier = (const token_ident_t *)token;
  static const char name[] = "__builtin_offsetof";
  return identifier->len == (int)(sizeof(name) - 1) &&
         memcmp(identifier->str, name, sizeof(name) - 1) == 0;
}

static psx_offsetof_designator_t *append_offsetof_designator(
    psx_offsetof_designator_t **designators, int *count, int *capacity,
    expr_parse_ctx_t *ctx) {
  if (!designators || !count || !capacity || !ctx) return NULL;
  if (*count >= *capacity) {
    *capacity = pda_next_cap_in(
        diagnostics(ctx), *capacity, *count + 1);
    *designators = pda_xreallocarray_in(
        diagnostics(ctx), *designators, (size_t)*capacity,
        sizeof(**designators));
  }
  return &(*designators)[(*count)++];
}

static node_t *parse_builtin_offsetof(expr_parse_ctx_t *ctx) {
  token_t *builtin_token = curtok(ctx);
  set_curtok(ctx, curtok(ctx)->next);
  tk_expect_ctx(ctx->tokenizer_context, '(');

  psx_type_name_ref_t type_name = {0};
  token_t *type_end = NULL;
  if (!capture_type_name_ref_at(
          curtok(ctx), 0, &type_name, &type_end, ctx) ||
      !type_end || type_end->kind != TK_COMMA) {
    ps_diag_ctx_in(
        diagnostics(ctx), curtok(ctx), "offsetof", "%s",
        diag_message_for_in(
            diagnostics(ctx), DIAG_ERR_PARSER_TYPE_NAME_REQUIRED));
  }
  set_curtok(ctx, type_end->next);

  int count = 0;
  int capacity = 4;
  psx_offsetof_designator_t *designators =
      calloc((size_t)capacity, sizeof(*designators));
  token_ident_t *member =
      tk_consume_ident_ctx(ctx->tokenizer_context);
  if (!member) {
    ps_diag_missing_in(
        diagnostics(ctx), curtok(ctx),
        diag_text_for_in(diagnostics(ctx), DIAG_TEXT_MEMBER_NAME));
  }
  psx_offsetof_designator_t *designator =
      append_offsetof_designator(
          &designators, &count, &capacity, ctx);
  *designator = (psx_offsetof_designator_t){
      .kind = PSX_OFFSETOF_DESIGNATOR_MEMBER,
      .member_name = member->str,
      .member_name_len = member->len,
      .tok = (token_t *)member,
      .member_tok = (token_t *)member,
  };

  while (curtok(ctx)->kind == TK_DOT ||
         curtok(ctx)->kind == TK_LBRACKET) {
    if (curtok(ctx)->kind == TK_DOT) {
      token_t *dot = curtok(ctx);
      set_curtok(ctx, curtok(ctx)->next);
      member = tk_consume_ident_ctx(ctx->tokenizer_context);
      if (!member) {
        ps_diag_missing_in(
            diagnostics(ctx), curtok(ctx),
            diag_text_for_in(diagnostics(ctx), DIAG_TEXT_MEMBER_NAME));
      }
      designator = append_offsetof_designator(
          &designators, &count, &capacity, ctx);
      *designator = (psx_offsetof_designator_t){
          .kind = PSX_OFFSETOF_DESIGNATOR_MEMBER,
          .member_name = member->str,
          .member_name_len = member->len,
          .tok = dot,
          .member_tok = (token_t *)member,
      };
      continue;
    }

    token_t *bracket = curtok(ctx);
    set_curtok(ctx, curtok(ctx)->next);
    node_t *index = conditional_ctx(ctx);
    tk_expect_ctx(ctx->tokenizer_context, ']');
    designator = append_offsetof_designator(
        &designators, &count, &capacity, ctx);
    *designator = (psx_offsetof_designator_t){
        .kind = PSX_OFFSETOF_DESIGNATOR_INDEX,
        .index_expression = index,
        .tok = bracket,
    };
  }
  tk_expect_ctx(ctx->tokenizer_context, ')');

  node_offsetof_query_t *query = arena_alloc_in(
      ctx->arena_context, sizeof(*query));
  query->base.kind = ND_OFFSETOF_QUERY;
  query->base.tok = builtin_token;
  query->type_name = type_name;
  query->designators = designators;
  query->designator_count = count;
  return (node_t *)query;
}

static node_t *primary_ctx(expr_parse_ctx_t *ctx) {
  node_t *cl = try_parse_compound_literal(ctx);
  if (cl) return cl;

  if (curtok(ctx)->kind == TK_GENERIC) return parse_generic_selection(ctx);

  if (curtok(ctx)->kind == TK_NUM) return parse_num_literal(ctx);

  if (token_is_builtin_offsetof(curtok(ctx)))
    return parse_builtin_offsetof(ctx);

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
