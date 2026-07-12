#include "expr.h"
#include "arena.h"
#include "core.h"
#include "decl.h"
#include "diag.h"
#include "dynarray.h"
#include "initializer_syntax.h"
#include "lvar_internal.h"
#include "node_utils.h"
#include "ret_pointee_array.h"
#include "semantic_ctx.h"
#include "stmt.h"
#include "config_runtime.h"
#include "type.h"
#include "../semantic/identifier_resolution.h"
#include "../semantic/type_name_resolution.h"
#include "declaration_syntax.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include "../tokenizer/literals.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int string_label_count = 0;
static int float_label_count = 0;

#define PS_MAX_EXPR_NEST_DEPTH 1024
#define PS_MAX_PAREN_NEST_DEPTH 1024

static inline token_t *curtok(void) { return tk_get_current_token(); }
static inline void set_curtok(token_t *tok) { tk_set_current_token(tok); }

typedef struct {
  int unevaluated_operand_depth;
  int expr_nest_depth;
  int paren_nest_depth;
} expr_parse_ctx_t;

typedef struct {
  psx_type_name_ref_t type_name;
  token_t *after_rparen;
} parsed_parenthesized_type_name_t;

static expr_parse_ctx_t expr_parse_ctx_default(void) {
  expr_parse_ctx_t ctx = {0};
  return ctx;
}

static expr_parse_ctx_t expr_parse_ctx_unevaluated_child(const expr_parse_ctx_t *parent) {
  expr_parse_ctx_t ctx = parent ? *parent : expr_parse_ctx_default();
  ctx.unevaluated_operand_depth++;
  return ctx;
}

static int is_type_name_start_token(token_t *t);
static int capture_type_name_ref_at(
    token_t *start, int runtime_bounds, psx_type_name_ref_t *out,
    token_t **out_end);
static int lvar_is_static_local_array(lvar_t *var);
static node_t *apply_postfix(node_t *node, expr_parse_ctx_t *ctx);
static node_t *parse_compound_literal_from_type(
    psx_type_name_ref_t type_name, token_t *after_rparen,
    int compound_addr_context, expr_parse_ctx_t *ctx);

static void enter_expr_nest_or_die(expr_parse_ctx_t *ctx) {
  if (!ctx) return;
  ctx->expr_nest_depth++;
  if (ctx->expr_nest_depth > PS_MAX_EXPR_NEST_DEPTH) {
    ps_diag_ctx(curtok(), "expr",
                 diag_message_for(DIAG_ERR_PARSER_EXPR_NEST_TOO_DEEP),
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
    ps_diag_ctx(curtok(), "paren",
                 diag_message_for(DIAG_ERR_PARSER_PAREN_NEST_TOO_DEEP),
                 PS_MAX_PAREN_NEST_DEPTH);
  }
}

static void leave_paren_nest(expr_parse_ctx_t *ctx) {
  if (ctx && ctx->paren_nest_depth > 0) ctx->paren_nest_depth--;
}

static int in_unevaluated_operand(const expr_parse_ctx_t *ctx) {
  return ctx && ctx->unevaluated_operand_depth > 0;
}

static node_t *annotate_lvar_usage_node(node_t *node, lvar_t *var, const expr_parse_ctx_t *ctx) {
  if (!node || !var) return node;
  node->usage_lvar = var;
  node->records_lvar_usage = 1;
  node->lvar_usage_unevaluated = in_unevaluated_operand(ctx) ? 1 : 0;
  return node;
}

static node_t *new_binary_with_source_op(node_kind_t kind, node_t *lhs, node_t *rhs,
                                         token_kind_t source_op) {
  node_t *node = ps_node_new_raw_binary(kind, lhs, rhs);
  if (node) node->source_op = source_op;
  return node;
}

static int is_type_name_start_token(token_t *t) {
  if (!t) return 0;
  psx_skip_gnu_attributes_at(&t);
  if (!t) return 0;
  if (t->kind == TK_THREAD_LOCAL || t->kind == TK_EXTERN ||
      t->kind == TK_STATIC || t->kind == TK_AUTO ||
      t->kind == TK_REGISTER || t->kind == TK_TYPEDEF ||
      t->kind == TK_ALIGNAS || t->kind == TK_INLINE ||
      t->kind == TK_NORETURN)
    return 1;
  if (t->kind == TK_CONST || t->kind == TK_VOLATILE || t->kind == TK_RESTRICT || t->kind == TK_ATOMIC) return 1;
  if (t->kind == TK_STRUCT || t->kind == TK_UNION || t->kind == TK_ENUM) return 1;
  if (psx_ctx_is_type_token(t->kind)) return 1;
  if (psx_ctx_is_typedef_name_token(t)) return 1;
  return 0;
}

static int expr_type_name_is_typedef(token_t *token, void *context) {
  (void)context;
  return psx_ctx_is_typedef_name_token(token);
}

static void diagnose_type_name_complex_requires_float(
    void *context, token_t *token) {
  (void)context;
  diag_emit_tokf(
      DIAG_ERR_PARSER_INVALID_CONTEXT, token, "%s",
      diag_message_for(
          DIAG_ERR_PARSER_COMPLEX_IMAGINARY_CAST_REQUIRES_FLOAT));
}

static int parse_generic_assoc_type(psx_type_name_ref_t *out) {
  token_t *end = NULL;
  if (!capture_type_name_ref_at(curtok(), 0, out, &end)) return 0;
  if (!psx_bind_type_name_ref(out)) return 0;
  set_curtok(end);
  return 1;
}

static node_t *build_member_access(node_t *base, int from_ptr, token_t *op_tok) {
  token_ident_t *member = tk_consume_ident();
  if (!member) {
    psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_MEMBER_NAME));
  }
  node_member_access_t *syntax = arena_alloc(sizeof(*syntax));
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
    int compound_addr_context, expr_parse_ctx_t *ctx) {
  set_curtok(after_rparen);
  char *current_funcname = NULL;
  int current_funcname_len = 0;
  psx_decl_get_current_funcname(&current_funcname, &current_funcname_len);
  (void)current_funcname_len;
  token_t *initializer_tok = curtok();
  node_t *initializer = psx_parse_initializer_syntax_list();
  node_t *syntax = ps_node_new_compound_literal(
      type_name, initializer, initializer_tok,
      compound_addr_context, current_funcname == NULL);
  return apply_postfix(syntax, ctx);
}

static int parse_parenthesized_type_name(
    token_t *tok, parsed_parenthesized_type_name_t *out) {
  if (!out || !tok || tok->kind != TK_LPAREN ||
      !is_type_name_start_token(tok->next))
    return 0;
  tk_ensure_lookahead();
  token_t *end = NULL;
  psx_type_name_ref_t type_name = {0};
  if (!capture_type_name_ref_at(tok->next, 0, &type_name, &end) ||
      !psx_bind_type_name_ref(&type_name) ||
      !end || end->kind != TK_RPAREN || !end->next)
    return 0;
  *out = (parsed_parenthesized_type_name_t){
      .type_name = type_name,
      .after_rparen = end->next,
  };
  return 1;
}

static int parenthesized_type_name_is_compound_literal(token_t *tok) {
  if (!tok || tok->kind != TK_LPAREN ||
      !is_type_name_start_token(tok->next)) {
    return 0;
  }
  psx_parsed_type_name_t syntax;
  if (!ps_parse_type_name_syntax_at(
          tok->next,
          &(psx_decl_specifier_syntax_options_t){
              .is_typedef_name = expr_type_name_is_typedef,
              .diagnose_complex_requires_float =
                  diagnose_type_name_complex_requires_float,
          },
          &syntax)) {
    return 0;
  }
  token_t *end = syntax.end;
  int is_compound = end && end->kind == TK_RPAREN && end->next &&
                    end->next->kind == TK_LBRACE;
  ps_dispose_type_name_syntax(&syntax);
  return is_compound;
}

static int capture_type_name_ref_at(
    token_t *start, int runtime_bounds, psx_type_name_ref_t *out,
    token_t **out_end) {
  if (!start || !out || !is_type_name_start_token(start)) return 0;
  psx_parsed_type_name_t *syntax =
      arena_alloc(sizeof(psx_parsed_type_name_t));
  int parsed = runtime_bounds
                   ? ps_parse_runtime_type_name_syntax_at(
                         start,
                         &(psx_decl_specifier_syntax_options_t){
                             .is_typedef_name = expr_type_name_is_typedef,
                             .diagnose_complex_requires_float =
                                 diagnose_type_name_complex_requires_float,
                         },
                         syntax)
                   : ps_parse_type_name_syntax_at(
                         start,
                         &(psx_decl_specifier_syntax_options_t){
                             .is_typedef_name = expr_type_name_is_typedef,
                             .diagnose_complex_requires_float =
                                 diagnose_type_name_complex_requires_float,
                         },
                         syntax);
  if (!parsed) {
    return 0;
  }
  if (runtime_bounds)
    ps_parse_runtime_declarator_expressions(&syntax->declarator);
  *out = (psx_type_name_ref_t){.syntax = syntax};
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
static node_t *cast_with_compound_addr_context(int compound_addr_context, expr_parse_ctx_t *ctx);
static node_t *unary_ctx(expr_parse_ctx_t *ctx);
static node_t *unary_with_compound_addr_context(int compound_addr_context, expr_parse_ctx_t *ctx);
static node_t *primary_with_compound_addr_context(int compound_addr_context, expr_parse_ctx_t *ctx);
static node_t *apply_postfix(node_t *node, expr_parse_ctx_t *ctx);

static node_t *parse_call_postfix(node_t *callee, expr_parse_ctx_t *ctx);

void psx_expr_reset_translation_unit_state(void) {
  string_label_count = 0;
  float_label_count = 0;
}

// expr = assign ("," assign)*
node_t *psx_expr_expr(void) {
  expr_parse_ctx_t ctx = expr_parse_ctx_default();
  return expr_internal_ctx(&ctx);
}

// assign = conditional (("=" | "+=" | "-=" | "*=" | "/=" | "%=" | "<<=" | ">>=" | "&=" | "^=" | "|=") assign)?
node_t *ps_expr_assign(void) {
  expr_parse_ctx_t ctx = expr_parse_ctx_default();
  return assign_ctx(&ctx);
}

static node_t *expr_internal_ctx(expr_parse_ctx_t *ctx) {
  enter_expr_nest_or_die(ctx);
  node_t *node = assign_ctx(ctx);
  while (curtok()->kind == TK_COMMA) {
    set_curtok(curtok()->next);
    node_t *rhs = assign_ctx(ctx);
    node_t *comma = ps_node_new_raw_binary(ND_COMMA, node, rhs);
    node = comma;
  }
  leave_expr_nest(ctx);
  return node;
}

static node_t *assign_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = conditional_ctx(ctx);
  node_t *lhs_prefix = NULL;
  node_t *assign_target = node;
  if (node && node->kind == ND_COMMA && node->rhs &&
      (node->rhs->kind == ND_LVAR || node->rhs->kind == ND_UNARY_DEREF ||
       node->rhs->kind == ND_DEREF || node->rhs->kind == ND_GVAR)) {
    lhs_prefix = node->lhs;
    assign_target = node->rhs;
  }
  /* C11 6.5.16p2: 代入演算子の LHS は modifiable lvalue でなければならない。
   * 関数識別子 (ND_FUNCREF) はそうではない (`f = 5;` 等は非合法)。後段の IR builder で
   * "ir build/emit failed" になっていたのを、ここで分かりやすい診断にする。
   * 代入系トークン (`=`/`+=`/`-=`/...) が来ているときだけ check し、それ以外
   * (関数呼び出し `f(...)` や関数アドレス取得 `&f` 等) は素通し。 */
  switch (curtok()->kind) {
    case TK_ASSIGN: {
      token_t *assign_tok = curtok();
      set_curtok(curtok()->next);
      node_t *rhs = assign_ctx(ctx);
      node_t *assign_node = ps_node_new_raw_assign(assign_target, rhs);
      assign_node->is_source_assignment = 1;
      assign_node->tok = assign_tok;
      node = (node_t *)assign_node;
      if (lhs_prefix)
        node = ps_node_new_raw_binary(ND_COMMA, lhs_prefix, node);
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
      token_t *op_tok = curtok();
      set_curtok(curtok()->next);
      node_t *compound = ps_node_new_raw_assign(
          assign_target, assign_ctx(ctx));
      compound->is_source_compound_assignment = 1;
      compound->source_op = op_tok->kind;
      compound->tok = op_tok;
      node = lhs_prefix
                 ? ps_node_new_raw_binary(ND_COMMA, lhs_prefix, compound)
                 : compound;
      break;
    }
    default: break;
  }
  return node;
}

static node_t *conditional_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = logical_or_ctx(ctx);
  if (curtok()->kind == TK_QUESTION) {
    set_curtok(curtok()->next);
    node_ctrl_t *ternary = arena_alloc(sizeof(node_ctrl_t));
    ternary->base.kind = ND_TERNARY;
    ternary->base.lhs = node;
    ternary->base.rhs = expr_internal_ctx(ctx);
    tk_expect(':');
    ternary->els = conditional_ctx(ctx);
    return (node_t *)ternary;
  }
  return node;
}

static node_t *logical_or_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = logical_and_ctx(ctx);
  while (curtok()->kind == TK_OROR) {
    set_curtok(curtok()->next);
    node_t *rhs = logical_and_ctx(ctx);
    node = new_binary_with_source_op(ND_LOGOR, node, rhs, TK_OROR);
  }
  return node;
}

static node_t *logical_and_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = bit_or_ctx(ctx);
  while (curtok()->kind == TK_ANDAND) {
    set_curtok(curtok()->next);
    node_t *rhs = bit_or_ctx(ctx);
    node = new_binary_with_source_op(ND_LOGAND, node, rhs, TK_ANDAND);
  }
  return node;
}

static node_t *bit_or_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = bit_xor_ctx(ctx);
  while (curtok()->kind == TK_PIPE) {
    set_curtok(curtok()->next);
    node = ps_node_new_raw_binary(ND_BITOR, node, bit_xor_ctx(ctx));
  }
  return node;
}

static node_t *bit_xor_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = bit_and_ctx(ctx);
  while (curtok()->kind == TK_CARET) {
    set_curtok(curtok()->next);
    node = ps_node_new_raw_binary(ND_BITXOR, node, bit_and_ctx(ctx));
  }
  return node;
}

static node_t *bit_and_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = equality_ctx(ctx);
  while (curtok()->kind == TK_AMP) {
    set_curtok(curtok()->next);
    node = ps_node_new_raw_binary(ND_BITAND, node, equality_ctx(ctx));
  }
  return node;
}

static node_t *equality_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = relational_ctx(ctx);
  for (;;) {
    if (curtok()->kind == TK_EQEQ) {
      set_curtok(curtok()->next);
      node_t *rhs = relational_ctx(ctx);
      node = new_binary_with_source_op(ND_EQ, node, rhs, TK_EQEQ);
    } else if (curtok()->kind == TK_NEQ) {
      set_curtok(curtok()->next);
      node_t *rhs = relational_ctx(ctx);
      node = new_binary_with_source_op(ND_NE, node, rhs, TK_NEQ);
    }
    else return node;
  }
}

static node_t *relational_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = shift_ctx(ctx);
  for (;;) {
    if (curtok()->kind == TK_LT) {
      set_curtok(curtok()->next);
      node_t *rhs = shift_ctx(ctx);
      node = new_binary_with_source_op(ND_LT, node, rhs, TK_LT);
    } else if (curtok()->kind == TK_LE) {
      set_curtok(curtok()->next);
      node_t *rhs = shift_ctx(ctx);
      node = new_binary_with_source_op(ND_LE, node, rhs, TK_LE);
    } else if (curtok()->kind == TK_GT) {
      set_curtok(curtok()->next);
      node_t *rhs = shift_ctx(ctx);
      node = new_binary_with_source_op(ND_LT, rhs, node, TK_GT);
    } else if (curtok()->kind == TK_GE) {
      set_curtok(curtok()->next);
      node_t *rhs = shift_ctx(ctx);
      node = new_binary_with_source_op(ND_LE, rhs, node, TK_GE);
    }
    else return node;
  }
}

static node_t *shift_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = add_ctx(ctx);
  for (;;) {
    if (curtok()->kind == TK_SHL) {
      set_curtok(curtok()->next);
      node_t *rhs = add_ctx(ctx);
      node = new_binary_with_source_op(ND_SHL, node, rhs, TK_SHL);
    } else if (curtok()->kind == TK_SHR) {
      set_curtok(curtok()->next);
      node_t *rhs = add_ctx(ctx);
      node = new_binary_with_source_op(ND_SHR, node, rhs, TK_SHR);
    }
    else return node;
  }
}

static node_t *add_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = mul_ctx(ctx);
  for (;;) {
    if (curtok()->kind == TK_PLUS) {
      set_curtok(curtok()->next);
      node_t *rhs = mul_ctx(ctx);
      node = new_binary_with_source_op(ND_ADD, node, rhs, TK_PLUS);
    } else if (curtok()->kind == TK_MINUS) {
      set_curtok(curtok()->next);
      node_t *rhs = mul_ctx(ctx);
      node = new_binary_with_source_op(ND_SUB, node, rhs, TK_MINUS);
    }
    else return node;
  }
}

static node_t *mul_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = cast_ctx(ctx);
  for (;;) {
    if (curtok()->kind == TK_MUL) {
      set_curtok(curtok()->next);
      node_t *rhs = cast_ctx(ctx);
      node = new_binary_with_source_op(ND_MUL, node, rhs, TK_MUL);
    } else if (curtok()->kind == TK_DIV) {
      set_curtok(curtok()->next);
      node_t *rhs = cast_ctx(ctx);
      node = new_binary_with_source_op(ND_DIV, node, rhs, TK_DIV);
    } else if (curtok()->kind == TK_MOD) {
      set_curtok(curtok()->next);
      node_t *rhs = cast_ctx(ctx);
      node = new_binary_with_source_op(ND_MOD, node, rhs, TK_MOD);
    }
    else return node;
  }
}

static node_t *cast_ctx(expr_parse_ctx_t *ctx) {
  return cast_with_compound_addr_context(0, ctx);
}

static node_t *cast_with_compound_addr_context(int compound_addr_context, expr_parse_ctx_t *ctx) {
  token_t *cast_tok = curtok();
  if (parenthesized_type_name_is_compound_literal(curtok())) {
    return unary_with_compound_addr_context(compound_addr_context, ctx);
  }
  parsed_parenthesized_type_name_t parsed_type;
  if (parse_parenthesized_type_name(curtok(), &parsed_type)) {
    set_curtok(parsed_type.after_rparen);
    node_t *operand = cast_with_compound_addr_context(compound_addr_context, ctx);
    node_t *source_cast =
        psx_node_new_source_cast(operand, parsed_type.type_name);
    source_cast->tok = cast_tok;
    return apply_postfix(source_cast, ctx);
  }
  return unary_ctx(ctx);
}

static node_t *parse_sizeof_operand(expr_parse_ctx_t *ctx, token_t *op_tok) {
  node_sizeof_query_t *query = arena_alloc(sizeof(node_sizeof_query_t));
  query->base.kind = ND_SIZEOF_QUERY;
  query->base.tok = op_tok;
  query->base.type = ps_type_new_integer(TK_UNSIGNED, 8, 1);

  if (curtok()->kind == TK_LPAREN) {
    psx_type_name_ref_t captured = {0};
    token_t *type_end = NULL;
    if (capture_type_name_ref_at(
            curtok()->next, 1, &captured, &type_end) &&
        type_end && type_end->kind == TK_RPAREN && type_end->next &&
        type_end->next->kind != TK_LBRACE) {
      query->is_type_name = 1;
      query->type_name = captured;
      if (!psx_bind_type_name_ref(&query->type_name)) return NULL;
      set_curtok(type_end->next);
      return (node_t *)query;
    }
    token_t *outer = curtok();
    captured = (psx_type_name_ref_t){0};
    type_end = NULL;
    if (outer->next && outer->next->kind == TK_LPAREN &&
        capture_type_name_ref_at(
            outer->next->next, 1, &captured, &type_end) &&
        type_end && type_end->kind == TK_RPAREN && type_end->next &&
        type_end->next->kind == TK_RPAREN) {
      query->is_type_name = 1;
      query->type_name = captured;
      if (!psx_bind_type_name_ref(&query->type_name)) return NULL;
      set_curtok(type_end->next->next);
      return (node_t *)query;
    }

    set_curtok(curtok()->next);
    expr_parse_ctx_t child_ctx = expr_parse_ctx_unevaluated_child(ctx);
    query->operand = expr_internal_ctx(&child_ctx);
    tk_expect(')');
    return (node_t *)query;
  }

  expr_parse_ctx_t child_ctx = expr_parse_ctx_unevaluated_child(ctx);
  query->operand = unary_ctx(&child_ctx);
  return (node_t *)query;
}

static node_t *parse_alignof_type_name(token_t *op_tok) {
  node_alignof_query_t *query = arena_alloc(sizeof(node_alignof_query_t));
  query->base.kind = ND_ALIGNOF_QUERY;
  query->base.tok = op_tok;
  query->base.type = ps_type_new_integer(TK_UNSIGNED, 8, 1);

  psx_type_name_ref_t captured = {0};
  token_t *type_end = NULL;
  if (capture_type_name_ref_at(curtok(), 0, &captured, &type_end) &&
      type_end && type_end->kind == TK_RPAREN) {
    query->type_name = captured;
    if (!psx_bind_type_name_ref(&query->type_name)) return NULL;
    set_curtok(type_end->next);
    return (node_t *)query;
  }
  token_t *outer = curtok();
  captured = (psx_type_name_ref_t){0};
  type_end = NULL;
  if (outer && outer->kind == TK_LPAREN &&
      capture_type_name_ref_at(outer->next, 0, &captured, &type_end) &&
      type_end && type_end->kind == TK_RPAREN && type_end->next &&
      type_end->next->kind == TK_RPAREN) {
    query->type_name = captured;
    if (!psx_bind_type_name_ref(&query->type_name)) return NULL;
    set_curtok(type_end->next->next);
    return (node_t *)query;
  }
  ps_diag_ctx(curtok(), "alignof", "%s",
              diag_message_for(DIAG_ERR_PARSER_ALIGNOF_TYPE_NAME_REQUIRED));
  return (node_t *)query;
}

static node_t *build_pre_inc_dec_node(
    node_kind_t kind, token_t *op_tok, expr_parse_ctx_t *ctx) {
  node_t *target = unary_ctx(ctx);
  node_t *node = arena_alloc(sizeof(node_t));
  node->kind = kind;
  node->lhs = target;
  node->tok = op_tok;
  return node;
}

static node_t *build_unary_deref_syntax(node_t *operand, token_t *op_tok) {
  node_t *syntax = ps_node_new_unary_deref_syntax_for(operand);
  syntax->tok = op_tok;
  return syntax;
}

// `&operand`。コンマ式 (a, b) に対する `&(a, b)` は a を評価した上で &b を返す形に組み立てる。
static node_t *build_unary_addr_node(node_t *operand) {
  if (operand && operand->kind == ND_COMPOUND_LITERAL) {
    node_compound_literal_t *compound =
        (node_compound_literal_t *)operand;
    compound->requires_addressable_object = 1;
    return operand;
  }
  if (operand && operand->kind == ND_COMMA && operand->rhs) {
    /* `&(compound-literal)` 等、値が COMMA(init, ref) の形。rhs に同じ単項 & の
     * ロジックを再帰適用する (直接 ND_ADDR で包むと配列複合リテラルの rhs が
     * 既に ND_ADDR (decay 済み) のとき二重に ND_ADDR でラップされ ir_build が
     * 失敗する)。下の ND_ADDR/ND_FUNCREF 簡約をここでも効かせる。 */
    return ps_node_new_raw_binary(
        ND_COMMA, operand->lhs, build_unary_addr_node(operand->rhs));
  }
  // C 仕様: 配列名 `arr` は (sizeof/&/レジスタ変数を除く) 文脈ではポインタ崩壊する。
  // `&arr` ではアドレス値はそのまま (型だけ `int(*)[N]` に変わる)。
  // ag_c では配列ローカル変数の参照は build_array_lvar_addr_node により
  // ND_ADDR(ND_LVAR) として表現されているため、`&arr` でさらに ND_ADDR でラップすると
  // codegen の `gen_lval` が ND_ADDR を受理せず E4002 になる。
  // 既に ND_ADDR で表現されているアドレス値はそのまま返す。
  if (operand && operand->kind == ND_ADDR) {
    /* `&arr` : 配列は既に decay 済みの ND_ADDR で表現されアドレス値は同じ。ただし
     * 結果型は `int(*)[N]` (ポインタ, 8B) なので、type_size=8 のコピーを返して
     * sizeof(&arr) が要素サイズでなく 8 を返すようにする (共有ノードは変更しない)。 */
    return ps_node_new_explicit_addr_value_for(operand);
  }
  /* `&f` (f は関数): 関数のアドレスは関数ポインタそのもの (= `f`)。ND_FUNCREF を
   * そのまま返す (ND_ADDR でラップすると IR builder が扱えず失敗する)。 */
  if (operand && operand->kind == ND_FUNCREF) {
    return operand;
  }
  return ps_node_new_unary_addr_for(operand);
}

static node_t *unary_ctx(expr_parse_ctx_t *ctx) {
  return unary_with_compound_addr_context(0, ctx);
}

static node_t *unary_with_compound_addr_context(int compound_addr_context, expr_parse_ctx_t *ctx) {
  token_kind_t k = curtok()->kind;
  if (k == TK_SIZEOF) {
    token_t *op_tok = curtok();
    set_curtok(curtok()->next);
    return parse_sizeof_operand(ctx, op_tok);
  }
  /* GNU 拡張 __real__ / __imag__: 複素数の実部/虚部を取り出す単項演算子
   * (実数オペランドでは __real__ x = x, __imag__ x = 0)。キーワードではなく
   * 特殊識別子として扱う (__func__ と同様)。creal/cimag を rvalue にも効かせる。 */
  if (k == TK_IDENT) {
    token_ident_t *kid = (token_ident_t *)curtok();
    if (kid->len == 8 && (memcmp(kid->str, "__real__", 8) == 0 ||
                          memcmp(kid->str, "__imag__", 8) == 0)) {
      int is_real = (kid->str[2] == 'r');
      set_curtok(curtok()->next);
      node_t *operand = cast_ctx(ctx);
      node_t *n = arena_alloc(sizeof(node_t));
      n->kind = is_real ? ND_CREAL : ND_CIMAG;
      n->lhs = operand;
      n->tok = (token_t *)kid;
      return n;
    }
  }
  if (k == TK_ALIGNOF) {
    token_t *op_tok = curtok();
    set_curtok(curtok()->next);
    tk_expect('(');
    return parse_alignof_type_name(op_tok);
  }
  if (k == TK_INC || k == TK_DEC) {
    token_t *op_tok = curtok();
    set_curtok(curtok()->next);
    return build_pre_inc_dec_node(
        k == TK_INC ? ND_PRE_INC : ND_PRE_DEC, op_tok, ctx);
  }
  if (k == TK_PLUS)  { set_curtok(curtok()->next); return cast_ctx(ctx); }
  if (k == TK_MINUS) {
    token_t *op_tok = curtok();
    set_curtok(curtok()->next);
    node_t *operand = cast_ctx(ctx);
    node_t *negate = arena_alloc(sizeof(node_t));
    negate->kind = ND_UNARY_NEGATE;
    negate->lhs = operand;
    negate->tok = op_tok;
    return negate;
  }
  if (k == TK_BANG)  {
    set_curtok(curtok()->next);
    node_t *eq = ps_node_new_raw_binary(
        ND_EQ, cast_ctx(ctx), ps_node_new_num(0));
    eq->from_logical_not = 1;  /* `!p == 0` の precedence-trap 警告に使う */
    return eq;
  }
  if (k == TK_TILDE) {
    set_curtok(curtok()->next);
    node_t *neg = ps_node_new_raw_binary(
        ND_SUB, ps_node_new_num(0), cast_ctx(ctx));
    return ps_node_new_raw_binary(ND_SUB, neg, ps_node_new_num(1));
  }
  if (k == TK_MUL) {
    token_t *op_tok = curtok();
    set_curtok(curtok()->next);
    return build_unary_deref_syntax(cast_ctx(ctx), op_tok);
  }
  if (k == TK_AMP) {
    set_curtok(curtok()->next);
    /* `&(int){5}`: ファイルスコープのスカラ複合リテラルを静的 gvar として
     * 実体化させ、アドレスを取れるようにする。 */
    node_t *operand = cast_with_compound_addr_context(1, ctx);
    return build_unary_addr_node(operand);
  }
  return apply_postfix(primary_with_compound_addr_context(compound_addr_context, ctx), ctx);
}


// `left[right]` の構文をそのまま保持する。operand の判定と正規化は semantic pass が行う。
static node_t *build_subscript_syntax(node_t *node, node_t *idx,
                                      token_t *op_tok) {
  node_t *syntax = ps_node_new_subscript_syntax_for(node, idx);
  syntax->tok = op_tok;
  return syntax;
}

static node_t *build_post_inc_dec_node(
    node_kind_t kind, node_t *operand, token_t *op_tok) {
  node_t *n = arena_alloc(sizeof(node_t));
  n->kind = kind;
  n->lhs = operand;
  n->tok = op_tok;
  return n;
}

static bool is_postfix_op_token(token_kind_t k) {
  return k == TK_LBRACKET || k == TK_LPAREN || k == TK_DOT ||
         k == TK_ARROW || k == TK_INC || k == TK_DEC;
}

static node_t *apply_postfix(node_t *node, expr_parse_ctx_t *ctx) {
  // 後置演算がコンマ式の rhs 側に適用される: `(a, b)++` ⇒ `(a, b++)`。
  if (node && node->kind == ND_COMMA && is_postfix_op_token(curtok()->kind)) {
    node->rhs = apply_postfix(node->rhs, ctx);
    return node;
  }
  for (;;) {
    token_kind_t k = curtok()->kind;
    if (k == TK_LBRACKET) {
      token_t *op_tok = curtok();
      set_curtok(curtok()->next);
      node_t *idx = expr_internal_ctx(ctx);
      tk_expect(']');
      node = build_subscript_syntax(node, idx, op_tok);
      continue;
    }
    if (k == TK_LPAREN) {
      node = parse_call_postfix(node, ctx);
      continue;
    }
    if (k == TK_DOT || k == TK_ARROW) {
      token_t *op_tok = curtok();
      set_curtok(curtok()->next);
      node = build_member_access(node, k == TK_ARROW ? 1 : 0, op_tok);
      continue;
    }
    if (k == TK_INC) {
      token_t *op_tok = curtok();
      set_curtok(curtok()->next);
      node = build_post_inc_dec_node(ND_POST_INC, node, op_tok);
      continue;
    }
    if (k == TK_DEC) {
      token_t *op_tok = curtok();
      set_curtok(curtok()->next);
      node = build_post_inc_dec_node(ND_POST_DEC, node, op_tok);
      continue;
    }
    return node;
  }
}

static node_t *parse_call_postfix(node_t *callee, expr_parse_ctx_t *ctx) {
  tk_expect('(');
  node_func_t *node = arena_alloc(sizeof(node_func_t));
  node->base.kind = ND_FUNCALL;
  /* callee が bare 関数参照 (ND_FUNCREF) のとき — 典型的には `_Generic(...)(args)` が
   * 関数を選んだ場合や `(funcname)(args)` — は直接呼び出しに変換する。funcname 経由なら
   * プロトタイプから戻り型/引数の fp ABI を引けるので、tgmath の `sqrt(2.0)` 等が double を
   * 正しく d0 で渡し受けできる (bare funcref の間接呼び出しはシグネチャを持たず整数扱いで
   * 値が化けていた)。 */
  if (callee && callee->kind == ND_FUNCREF) {
    node_funcref_t *fr = (node_funcref_t *)callee;
    node->funcname = fr->funcname;
    node->funcname_len = fr->funcname_len;
    node->function_type = fr->function_type;
    node->callee = NULL;
    callee = NULL;
  } else {
    node->callee = callee;
  }
  int nargs = 0;
  int arg_cap = 16;
  node->args = calloc(arg_cap, sizeof(node_t *));
  if (curtok()->kind == TK_RPAREN) {
    set_curtok(curtok()->next);
  } else {
    node->args[nargs++] = assign_ctx(ctx);
    while (curtok()->kind == TK_COMMA) {
      set_curtok(curtok()->next);
      if (nargs >= arg_cap) {
        arg_cap = pda_next_cap(arg_cap, nargs + 1);
        node->args = pda_xreallocarray(node->args, (size_t)arg_cap, sizeof(node_t *));
      }
      node->args[nargs++] = assign_ctx(ctx);
    }
    tk_expect(')');
  }
  node->nargs = nargs;
  return (node_t *)node;
}

// TK_LPAREN を見たときの compound literal `(T){...}` 試行。
// パースできたら結果ノードを返し、できなければ NULL（呼び出し側は通常の式へ）。
static node_t *try_parse_compound_literal(int compound_addr_context, expr_parse_ctx_t *ctx) {
  parsed_parenthesized_type_name_t parsed_type;
  if (curtok()->kind == TK_LPAREN &&
      parse_parenthesized_type_name(curtok(), &parsed_type) &&
      parsed_type.after_rparen &&
      parsed_type.after_rparen->kind == TK_LBRACE) {
    return parse_compound_literal_from_type(
        parsed_type.type_name, parsed_type.after_rparen,
        compound_addr_context, ctx);
  }
  return NULL;
}

static node_t *parse_generic_selection(expr_parse_ctx_t *ctx) {
  token_t *generic_tok = curtok();
  set_curtok(curtok()->next);
  tk_expect('(');

  expr_parse_ctx_t control_ctx = expr_parse_ctx_unevaluated_child(ctx);
  node_t *control = assign_ctx(&control_ctx);
  tk_expect(',');

  int count = 0;
  int capacity = 4;
  psx_generic_association_t *associations =
      calloc((size_t)capacity, sizeof(psx_generic_association_t));
  for (;;) {
    if (count >= capacity) {
      capacity = pda_next_cap(capacity, count + 1);
      associations = pda_xreallocarray(
          associations, (size_t)capacity,
          sizeof(psx_generic_association_t));
    }
    psx_generic_association_t *association = &associations[count++];
    association->tok = curtok();
    if (curtok()->kind == TK_DEFAULT) {
      association->is_default = 1;
      set_curtok(curtok()->next);
    } else if (!parse_generic_assoc_type(&association->type_name)) {
      ps_diag_ctx(curtok(), "generic", "%s",
                  diag_message_for(
                      DIAG_ERR_PARSER_GENERIC_ASSOC_TYPE_INVALID));
    }
    tk_expect(':');
    association->expression = assign_ctx(ctx);
    if (!tk_consume(',')) break;
  }
  tk_expect(')');

  node_generic_selection_t *selection =
      arena_alloc(sizeof(node_generic_selection_t));
  selection->base.kind = ND_GENERIC_SELECTION;
  selection->base.tok = generic_tok;
  selection->control = control;
  selection->associations = associations;
  selection->association_count = count;
  selection->selected_index = -1;
  return (node_t *)selection;
}

static node_t *parse_num_literal(void) {
  token_t *tok = curtok();
  token_num_t *num = (token_num_t *)tok;
  node_num_t *node = arena_alloc(sizeof(node_num_t));
  node->base.kind = ND_NUM;
  if (num->num_kind == TK_NUM_KIND_INT) {
    node->base.fp_kind = TK_FLOAT_KIND_NONE;
    node->float_suffix_kind = TK_FLOAT_SUFFIX_NONE;
    node->val = tk_as_num_int(tok)->val;
    /* long / long long サフィックス付き整数リテラルは値が 32bit に収まっても i64 と
     * して扱う (`2L * u` 等が 32bit 演算で wrap しないように)。unsigned サフィックスも
     * 比較/除算の符号判定のため node に伝播する。 */
    node->int_is_long = (tk_as_num_int(tok)->int_size != TK_INT_SIZE_INT) ? 1 : 0;
    node->int_is_long_long = (tk_as_num_int(tok)->int_size == TK_INT_SIZE_LONG_LONG) ? 1 : 0;
    node->base.is_unsigned = tk_as_num_int(tok)->is_unsigned ? 1 : 0;
    int int_size = node->int_is_long ? 8 : 4;
    node->base.type = ps_type_new_integer(
        node->base.is_unsigned ? TK_UNSIGNED : TK_INT,
        int_size, node->base.is_unsigned);
    node->base.type->is_long_long = node->int_is_long_long ? 1 : 0;
  } else {
    node->base.fp_kind = tk_as_num_float(tok)->fp_kind;
    node->float_suffix_kind = tk_as_num_float(tok)->float_suffix_kind;
    node->fval = tk_as_num_float(tok)->fval;
    node->base.type = ps_type_new_float(
        (tk_float_kind_t)node->base.fp_kind,
        node->base.fp_kind == TK_FLOAT_KIND_FLOAT ? 4 : 8);
  }
  if (node->base.fp_kind) {
    float_lit_t *lit = calloc(1, sizeof(float_lit_t));
    lit->id = float_label_count++;
    lit->fval = node->fval;
    lit->fp_kind = node->base.fp_kind;
    lit->float_suffix_kind = node->float_suffix_kind;
    psx_register_float_lit(lit);
    node->fval_id = lit->id;
  }
  set_curtok(curtok()->next);
  return (node_t *)node;
}

// 内容文字列・幅・プレフィックスから ND_STRING ノードと .LC ラベルを生成する。
// str はコピーせず lit->str に直接渡されるので、呼び出し側で alloc 済みであること。
static node_string_t *make_string_lit_node(char *str, int len,
                                           tk_char_width_t char_width,
                                           tk_string_prefix_kind_t prefix_kind) {
  node_string_t *snode = arena_alloc(sizeof(node_string_t));
  snode->base.kind = ND_STRING;
  int id = string_label_count++;
  int label_len = snprintf(NULL, 0, ".LC%d", id);
  snode->string_label = calloc((size_t)label_len + 1, 1);
  snprintf(snode->string_label, (size_t)label_len + 1, ".LC%d", id);
  string_lit_t *lit = calloc(1, sizeof(string_lit_t));
  lit->label = snode->string_label;
  lit->str = str;
  lit->len = len;
  lit->char_width = char_width ? char_width : TK_CHAR_WIDTH_CHAR;
  lit->str_prefix_kind = prefix_kind;
  psx_register_string_lit(lit);
  /* 文字列リテラルは char (または wchar) 配列で、式中ではポインタに decay する。
   * `"abc"[1]` の subscript チェックや (ptr + n) のスケーリングに使う。 */
  snode->char_width = char_width ? char_width : TK_CHAR_WIDTH_CHAR;
  snode->str_prefix_kind = prefix_kind;
  int elem_width = snode->char_width;
  int elem_is_unsigned = prefix_kind == TK_STR_PREFIX_u ||
                         prefix_kind == TK_STR_PREFIX_U;
  token_kind_t elem_kind = elem_width == TK_CHAR_WIDTH_CHAR
                               ? TK_CHAR
                               : (elem_is_unsigned ? TK_UNSIGNED : TK_INT);
  psx_type_t *elem_type =
      ps_type_new_integer(elem_kind, elem_width, elem_is_unsigned);
  snode->base.type = ps_type_new_pointer(elem_type, elem_width);
  snode->base.type->base_deref_size = elem_width;
  /* byte_len は「デコード後」の内容長 (要素数)。str はソースのまま (`\t` 等の
   * エスケープシーケンスを含む raw) なので、エスケープを 1 要素に畳んで数える。
   * これがないと sizeof("\t") が raw の 2(+1) を返していた (正しくは 1+1)。 */
  snode->byte_len = tk_count_string_code_units(str, len,
                                               char_width ? (int)char_width
                                                          : TK_CHAR_WIDTH_CHAR);
  return snode;
}

// C11 6.4.2.2 __func__: 各関数本体に暗黙定義される const char[] の関数名。
static node_t *make_func_name_string_node(void) {
  char *current_funcname = NULL;
  int current_funcname_len = 0;
  psx_decl_get_current_funcname(&current_funcname, &current_funcname_len);
  const char *fname = current_funcname ? current_funcname : "";
  int flen = current_funcname ? current_funcname_len : 0;
  char *fstr = calloc((size_t)flen + 1, 1);
  memcpy(fstr, fname, (size_t)flen);
  return (node_t *)make_string_lit_node(fstr, flen, TK_CHAR_WIDTH_CHAR, TK_STR_PREFIX_NONE);
}

// 連続する TK_STRING リテラルを結合して 1 つの ND_STRING ノードを返す。
static node_t *parse_string_literal_sequence(void) {
  tk_char_width_t merged_width = TK_CHAR_WIDTH_CHAR;
  tk_string_prefix_kind_t merged_prefix_kind = TK_STR_PREFIX_NONE;
  size_t total_len = 0;
  token_t *t = curtok();
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
      diag_emit_tokf(DIAG_ERR_PARSER_UNEXPECTED_TOKEN, t, "%s",
                     diag_message_for(DIAG_ERR_PARSER_STRING_PREFIX_MISMATCH));
    }
    if (st->len < 0 || (size_t)st->len > SIZE_MAX - total_len - 1) {
      diag_emit_tokf(DIAG_ERR_PARSER_STRING_LITERAL_TOO_LARGE, t, "%s",
                     diag_message_for(DIAG_ERR_PARSER_STRING_LITERAL_TOO_LARGE));
    }
    total_len += (size_t)st->len;
    t = t->next;
  }
  if (total_len > (size_t)INT_MAX) {
    diag_emit_tokf(DIAG_ERR_PARSER_STRING_LITERAL_TOO_LARGE, curtok(), "%s",
                   diag_message_for(DIAG_ERR_PARSER_STRING_LITERAL_TOO_LARGE));
  }
  char *merged = calloc(total_len + 1, 1);
  if (!merged) {
    diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
  }
  size_t off = 0;
  while (curtok() && curtok()->kind == TK_STRING) {
    token_string_t *st = (token_string_t *)curtok();
    if (st->len < 0 || (size_t)st->len > total_len - off) {
      diag_emit_tokf(DIAG_ERR_PARSER_STRING_CONCAT_SIZE_INVALID, curtok(), "%s",
                     diag_message_for(DIAG_ERR_PARSER_STRING_CONCAT_SIZE_INVALID));
    }
    memcpy(merged + off, st->str, (size_t)st->len);
    off += (size_t)st->len;
    set_curtok(curtok()->next);
  }
  return (node_t *)make_string_lit_node(merged, (int)total_len, merged_width, merged_prefix_kind);
}

// GCC __builtin_expect(exp, c): 第1引数 exp をそのまま返す (分岐ヒントは無視)。
static node_t *try_parse_builtin_expect_call(token_ident_t *tok, expr_parse_ctx_t *ctx) {
  if (tok->len != 16 || memcmp(tok->str, "__builtin_expect", 16) != 0) return NULL;
  if (curtok()->kind != TK_LPAREN) return NULL;
  set_curtok(curtok()->next); // skip '('
  node_t *exp = assign_ctx(ctx);
  tk_expect(',');
  (void)assign_ctx(ctx); // discard hint
  tk_expect(')');
  return exp;
}

// 名前が宣言済みでない (var==NULL) 識別子の直後に '(' が来ている場合の通常関数呼び出し。
// 戻り値型はcanonical function typeから引く。
static node_t *build_unqualified_call(
    token_ident_t *tok, expr_parse_ctx_t *ctx,
    const psx_identifier_resolution_t *resolution) {
  set_curtok(curtok()->next); // skip '('
  node_func_t *node = arena_alloc(sizeof(node_func_t));
  node->base.kind = ND_FUNCALL;
  node->base.tok = (token_t *)tok;
  node->callee = NULL;
  node->funcname = tok->str;
  node->funcname_len = tok->len;
  if (resolution && resolution->function_type)
    node->function_type = ps_type_clone(resolution->function_type);
  int nargs = 0;
  int arg_cap = 16;
  node->args = calloc(arg_cap, sizeof(node_t *));
  if (curtok()->kind == TK_RPAREN) {
    set_curtok(curtok()->next);
  } else {
    node->args[nargs++] = assign_ctx(ctx);
    while (curtok()->kind == TK_COMMA) {
      set_curtok(curtok()->next);
      if (nargs >= arg_cap) {
        arg_cap = pda_next_cap(arg_cap, nargs + 1);
        node->args = pda_xreallocarray(node->args, (size_t)arg_cap, sizeof(node_t *));
      }
      node->args[nargs++] = assign_ctx(ctx);
    }
    tk_expect(')');
  }
  node->nargs = nargs;
  /* C99/C11 では implicit function declaration は禁止 (C89 では int 戻りで暗黙宣言可)。
   * `undecl_func()` のように未宣言関数を呼ぶ場合に診断する。clang は default で warning、
   * `-Werror=implicit-function-declaration` で error。ag_c も warning として扱う。
   * tok が関数として登録されておらず、グローバル変数 (関数ポインタ) でもないなら未宣言。 */
  if (!resolution ||
      resolution->kind == PSX_IDENTIFIER_UNDECLARED_CALL) {
    node->base.is_implicit_func_decl = 1;
  }
  /* C11 6.5.2.2p2: 呼び出しの実引数数は仮引数数と一致 (non-variadic)、
   * または >= 固定引数数 (variadic) でなければならない。
   * 既に登録されている関数のみチェック (未宣言識別子は別エラーで弾かれる)。 */
  if (resolution && resolution->kind == PSX_IDENTIFIER_FUNCTION) {
    int expected = resolution->parameter_count;
    int is_variadic = resolution->is_variadic;
    int mismatch = is_variadic ? (nargs < expected) : (nargs != expected);
    if (mismatch) {
      ps_diag_ctx(curtok(), "funcall",
                   "関数呼び出しの引数数が一致しません: '%.*s' 期待 %s%d、実際 %d",
                   tok->len, tok->str,
                   is_variadic ? "≥" : "", expected, nargs);
    }
  }
  return (node_t *)node;
}

// 関数名識別子（呼び出しじゃなく値として使われる場合）の ND_FUNCREF ノード。
static node_t *build_funcref_node(
    token_ident_t *tok,
    const psx_identifier_resolution_t *resolution) {
  node_funcref_t *fr = arena_alloc(sizeof(node_funcref_t));
  fr->base.kind = ND_FUNCREF;
  fr->base.tok = (token_t *)tok;
  if (resolution && resolution->function_type)
    fr->function_type = ps_type_clone(resolution->function_type);
  fr->funcname = tok->str;
  fr->funcname_len = tok->len;
  return (node_t *)fr;
}

// グローバル変数表から名前を引く。見つからなければ NULL。
// 配列のときは ND_ADDR でラップして返す。
static node_t *build_global_var_node(global_var_t *global) {
  if (!global) return NULL;
  if (ps_gvar_is_array(global))
    return ps_node_new_gvar_array_addr_for(global);
  return ps_node_new_gvar_for(global);
}

/* static local 配列のベースアドレスを ND_ADDR(ND_GVAR) として返す。
 * declaration pipelineは実体をmangled globalへ置き、local scopeには
 * alias lvar (is_static_local=1, static_global_name=mangled) を登録する。
 * alias は size=0 で frame 割当を抑制しているため、サイズ情報はグローバル変数表
 * から名前検索で引く。多次元配列は alias lvar に保存した stride 情報を
 * ND_ADDR(ND_GVAR) へ伝播し、通常のローカル/グローバル配列と同じ subscript 経路に乗せる。 */
static node_t *build_static_local_array_addr_node(lvar_t *var) {
  /* static-local array aliases are lowering metadata; do not materialize
   * var->decl_type while recognizing them, because the alias intentionally has
   * size=0/is_array=0 and carries its array shape in stride fields. */
  short gv_type_size = (short)var->elem_size;
  for (global_var_t *gv = psx_resolve_global_object_symbol(
           var->static_global_name, var->static_global_name_len);
       gv; gv = NULL) {
    if (gv->name_len == var->static_global_name_len &&
        memcmp(gv->name, var->static_global_name, (size_t)gv->name_len) == 0) {
      int storage_size = ps_gvar_storage_size(gv, 0);
      if (storage_size > 0) gv_type_size = (short)storage_size;
      break;
    }
  }
  return ps_node_new_static_local_array_addr_for(var, gv_type_size);
}

/* alias lvar が「static local 配列」を表すかを判別。
 * declaration pipelineはarray aliasをis_static_local + mangled global名 +
 * elem_size>0 + frame size 0として登録する。canonical decl_type上のarray shapeは
 * aliasにも保持される。scalar/pointer static localはframe sizeが0にならない。 */
static int lvar_is_static_local_array(lvar_t *var) {
  return var && var->is_static_local && var->static_global_name &&
         var->elem_size > 0 && var->size == 0 && !var->is_vla &&
         !var->is_param;
}

// 配列ローカル変数（非 VLA）: ベースアドレスを ND_ADDR(ND_LVAR) として返す。
static node_t *build_array_lvar_addr_node(lvar_t *var) {
  return ps_node_new_lvar_array_addr_for(var, ps_lvar_tag_kind(var) != TK_EOF);
}

// byref 仮引数 (>16バイト構造体の値渡し): IR entry で受け取った pointer から
// フレーム上の通常 lvar slot へ memcpy 済みなので、式としては通常の struct lvar。
static node_t *build_byref_param_node(lvar_t *var) {
  return ps_node_new_lvar_identifier_ref_for(var);
}

// 識別子トークン tok を解決して node を返す:
//   1. __func__ → 暗黙文字列リテラル
//   2. 未定義 + enum const → 定数
//   3. 未定義 + '(' → 関数呼び出し
//   4. 未定義 + 既登録関数名 → 関数参照
//   5. 未定義 + グローバル変数 → ND_GVAR
//   6. それ以外 → ローカル変数 (必要なら新規登録)
static node_t *resolve_identifier(token_ident_t *tok, expr_parse_ctx_t *ctx) {
  if (tok->len == 8 && memcmp(tok->str, "__func__", 8) == 0) {
    return make_func_name_string_node();
  }
  // stdarg.h の va_start マクロが参照する ag_c 固有 builtin。
  // codegen で `add x?, x29, #STACK_SIZE` を出して variadic 引数領域の
  // 先頭アドレスを返す。
  if (tok->len == 13 && memcmp(tok->str, "__va_arg_area", 13) == 0) {
    node_t *n = arena_alloc(sizeof(node_t));
    n->kind = ND_VA_ARG_AREA;
    n->fp_kind = TK_FLOAT_KIND_NONE;
    return n;
  }
  int is_call = curtok()->kind == TK_LPAREN;
  if (is_call) {
    node_t *be = try_parse_builtin_expect_call(tok, ctx);
    if (be) return be;
  }
  psx_identifier_resolution_t resolution;
  psx_resolve_identifier(
      &(psx_identifier_resolution_request_t){
          .name = tok->str,
          .name_len = tok->len,
          .is_call = is_call,
      },
      &resolution);
  if (resolution.kind == PSX_IDENTIFIER_ENUM_CONSTANT)
    return ps_node_new_num(resolution.enum_value);
  if (resolution.kind == PSX_IDENTIFIER_GLOBAL_OBJECT)
    return build_global_var_node(resolution.global);
  if (resolution.kind == PSX_IDENTIFIER_FUNCTION ||
      resolution.kind == PSX_IDENTIFIER_UNDECLARED_CALL) {
    return is_call ? build_unqualified_call(tok, ctx, &resolution)
                   : build_funcref_node(tok, &resolution);
  }
  lvar_t *var = resolution.local;
  if (resolution.kind == PSX_IDENTIFIER_UNDEFINED || !var) {
    /* C89/C99/C11: 変数は必ず宣言が必要。未宣言識別子はエラー。
     * (旧 ag_c は暗黙のローカル変数として自動登録していたが、これは
     *  非標準動作なので削除した。tok を渡して位置情報を診断に含める。) */
    psx_diag_undefined_with_name((token_t *)tok, "variable", tok->str, tok->len);
    /* diag_emit_tokf は exit するためここには到達しないが、
     * 解析を続けたい場合のフォールバックとして lvar 登録しておく。 */
    var = psx_decl_register_lvar(tok->str, tok->len);
  }
  /* static local 配列の実体はdeclaration pipelineでglobal storageへlowering済み。
   * alias lvar のoffsetは実体位置ではないので、build_array_lvar_addr_node が
   * フレーム上の偽アドレスを base にしないよう専用経路で ND_ADDR(ND_GVAR) を返す。 */
  if (lvar_is_static_local_array(var)) {
    return annotate_lvar_usage_node(build_static_local_array_addr_node(var), var, ctx);
  }
  if (ps_lvar_is_array(var) && !ps_lvar_is_vla(var)) {
    return annotate_lvar_usage_node(build_array_lvar_addr_node(var), var, ctx);
  }
  if (ps_lvar_is_vla(var)) {
    return annotate_lvar_usage_node(ps_node_new_vla_decay_ref_for(var), var, ctx);
  }
  if (var->is_byref_param) {
    return annotate_lvar_usage_node(build_byref_param_node(var), var, ctx);
  }
  return annotate_lvar_usage_node(ps_node_new_lvar_identifier_ref_for(var), var, ctx);
}

static node_t *primary_with_compound_addr_context(int compound_addr_context, expr_parse_ctx_t *ctx) {
  node_t *cl = try_parse_compound_literal(compound_addr_context, ctx);
  if (cl) return cl;

  if (curtok()->kind == TK_GENERIC) return parse_generic_selection(ctx);

  if (curtok()->kind == TK_NUM) return parse_num_literal();

  if (curtok()->kind == TK_LPAREN && curtok()->next &&
      curtok()->next->kind == TK_LBRACE) {
    return psx_parse_statement_expression();
  }

  if (curtok()->kind == TK_LPAREN) {
    enter_paren_nest_or_die(ctx);
    set_curtok(curtok()->next);
    node_t *node = expr_internal_ctx(ctx);
    tk_expect(')');
    leave_paren_nest(ctx);
    return node;
  }

  token_ident_t *tok = tk_consume_ident();
  if (tok) return resolve_identifier(tok, ctx);

  if (curtok()->kind == TK_STRING) {
    return parse_string_literal_sequence();
  }

  ps_diag_ctx(curtok(), "primary", "%s",
               diag_message_for(DIAG_ERR_PARSER_PRIMARY_NUMBER_EXPECTED));
  return NULL;
}
