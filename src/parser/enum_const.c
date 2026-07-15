#include "enum_const.h"
#include "diag.h"
#include "dynarray.h"
#include "semantic_ctx.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
  /* case ラベルでは INT_MAX を超える整数リテラルも long long として受理する
   * (C11 6.8.4.2)。enum 定数・配列次元・_Alignas・ビットフィールド幅の
   * 経路は従来どおり整数 token の期待処理で int に制約する。 */
  int allow_wide_const;
  psx_semantic_context_t *semantic_context;
  tokenizer_context_t *tokenizer_context;
} enum_const_eval_ctx_t;

static inline token_t *curtok(enum_const_eval_ctx_t *ctx) {
  return tk_get_current_token_ctx(ctx->tokenizer_context);
}

static inline ag_diagnostic_context_t *diagnostics(
    enum_const_eval_ctx_t *ctx) {
  return ps_ctx_diagnostics(ctx->semantic_context);
}

static inline void set_curtok(
    enum_const_eval_ctx_t *ctx, token_t *tok) {
  tk_set_current_token_ctx(ctx->tokenizer_context, tok);
}

static long long parse_conditional_ctx(enum_const_eval_ctx_t *ctx);
static long long parse_logor_ctx(enum_const_eval_ctx_t *ctx);
static long long parse_logand_ctx(enum_const_eval_ctx_t *ctx);
static long long parse_bitor_ctx(enum_const_eval_ctx_t *ctx);
static long long parse_bitxor_ctx(enum_const_eval_ctx_t *ctx);
static long long parse_bitand_ctx(enum_const_eval_ctx_t *ctx);
static long long parse_eq_ctx(enum_const_eval_ctx_t *ctx);
static long long parse_rel_ctx(enum_const_eval_ctx_t *ctx);
static long long parse_shift_ctx(enum_const_eval_ctx_t *ctx);
static long long parse_add_ctx(enum_const_eval_ctx_t *ctx);
static long long parse_mul_ctx(enum_const_eval_ctx_t *ctx);
static long long parse_unary_ctx(enum_const_eval_ctx_t *ctx);
static long long parse_primary_ctx(enum_const_eval_ctx_t *ctx);

long long psx_parse_enum_const_expr_in_contexts(
    psx_semantic_context_t *semantic_context,
    tokenizer_context_t *tokenizer_context) {
  enum_const_eval_ctx_t ctx = {
      .semantic_context = semantic_context,
      .tokenizer_context = tokenizer_context,
  };
  return parse_conditional_ctx(&ctx);
}

long long psx_parse_case_const_expr_in_contexts(
    psx_semantic_context_t *semantic_context,
    tokenizer_context_t *tokenizer_context) {
  enum_const_eval_ctx_t ctx = {
      .allow_wide_const = 1,
      .semantic_context = semantic_context,
      .tokenizer_context = tokenizer_context,
  };
  return parse_conditional_ctx(&ctx);
}

long long psx_eval_parsed_enum_const_expr_in_context(
    psx_semantic_context_t *semantic_context,
    token_t *start, token_t *end) {
  if (!start || !end) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(semantic_context), start,
        "constant-expression", "missing parsed constant expression range");
  }
  tokenizer_context_t tokenizer_context;
  tk_context_init(&tokenizer_context);
  tk_context_bind_diagnostic_context(
      &tokenizer_context, ps_ctx_diagnostics(semantic_context));
  tk_set_current_token_ctx(&tokenizer_context, start);
  long long value = psx_parse_enum_const_expr_in_contexts(
      semantic_context, &tokenizer_context);
  if (tk_get_current_token_ctx(&tokenizer_context) != end) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(semantic_context),
        tk_get_current_token_ctx(&tokenizer_context),
        "constant-expression",
        "parsed constant expression was not fully consumed");
  }
  tk_context_dispose(&tokenizer_context);
  return value;
}

static long long parse_conditional_ctx(enum_const_eval_ctx_t *ctx) {
  long long cond = parse_logor_ctx(ctx);
  if (!tk_consume_ctx(ctx->tokenizer_context, '?')) return cond;
  long long then_v = parse_conditional_ctx(ctx);
  tk_expect_ctx(ctx->tokenizer_context, ':');
  long long else_v = parse_conditional_ctx(ctx);
  return cond ? then_v : else_v;
}

static long long parse_logor_ctx(enum_const_eval_ctx_t *ctx) {
  long long v = parse_logand_ctx(ctx);
  while (curtok(ctx)->kind == TK_OROR) {
    set_curtok(ctx, curtok(ctx)->next);
    long long r = parse_logand_ctx(ctx);
    v = (v || r) ? 1 : 0;
  }
  return v;
}

static long long parse_logand_ctx(enum_const_eval_ctx_t *ctx) {
  long long v = parse_bitor_ctx(ctx);
  while (curtok(ctx)->kind == TK_ANDAND) {
    set_curtok(ctx, curtok(ctx)->next);
    long long r = parse_bitor_ctx(ctx);
    v = (v && r) ? 1 : 0;
  }
  return v;
}

static long long parse_bitor_ctx(enum_const_eval_ctx_t *ctx) {
  long long v = parse_bitxor_ctx(ctx);
  while (curtok(ctx)->kind == TK_PIPE) {
    set_curtok(ctx, curtok(ctx)->next);
    long long r = parse_bitxor_ctx(ctx);
    v |= r;
  }
  return v;
}

static long long parse_bitxor_ctx(enum_const_eval_ctx_t *ctx) {
  long long v = parse_bitand_ctx(ctx);
  while (curtok(ctx)->kind == TK_CARET) {
    set_curtok(ctx, curtok(ctx)->next);
    long long r = parse_bitand_ctx(ctx);
    v ^= r;
  }
  return v;
}

static long long parse_bitand_ctx(enum_const_eval_ctx_t *ctx) {
  long long v = parse_eq_ctx(ctx);
  while (curtok(ctx)->kind == TK_AMP) {
    set_curtok(ctx, curtok(ctx)->next);
    long long r = parse_eq_ctx(ctx);
    v &= r;
  }
  return v;
}

static long long parse_eq_ctx(enum_const_eval_ctx_t *ctx) {
  long long v = parse_rel_ctx(ctx);
  while (curtok(ctx)->kind == TK_EQEQ || curtok(ctx)->kind == TK_NEQ) {
    token_kind_t op = curtok(ctx)->kind;
    set_curtok(ctx, curtok(ctx)->next);
    long long r = parse_rel_ctx(ctx);
    v = (op == TK_EQEQ) ? (v == r) : (v != r);
  }
  return v;
}

static long long parse_rel_ctx(enum_const_eval_ctx_t *ctx) {
  long long v = parse_shift_ctx(ctx);
  while (curtok(ctx)->kind == TK_LT || curtok(ctx)->kind == TK_LE ||
         curtok(ctx)->kind == TK_GT || curtok(ctx)->kind == TK_GE) {
    token_kind_t op = curtok(ctx)->kind;
    set_curtok(ctx, curtok(ctx)->next);
    long long r = parse_shift_ctx(ctx);
    switch (op) {
      case TK_LT: v = (v < r); break;
      case TK_LE: v = (v <= r); break;
      case TK_GT: v = (v > r); break;
      default: v = (v >= r); break;
    }
  }
  return v;
}

static long long parse_shift_ctx(enum_const_eval_ctx_t *ctx) {
  long long v = parse_add_ctx(ctx);
  while (curtok(ctx)->kind == TK_SHL || curtok(ctx)->kind == TK_SHR) {
    token_kind_t op = curtok(ctx)->kind;
    set_curtok(ctx, curtok(ctx)->next);
    long long r = parse_add_ctx(ctx);
    v = (op == TK_SHL) ? (v << r) : (v >> r);
  }
  return v;
}

static long long parse_add_ctx(enum_const_eval_ctx_t *ctx) {
  long long v = parse_mul_ctx(ctx);
  while (curtok(ctx)->kind == TK_PLUS || curtok(ctx)->kind == TK_MINUS) {
    token_kind_t op = curtok(ctx)->kind;
    set_curtok(ctx, curtok(ctx)->next);
    long long r = parse_mul_ctx(ctx);
    v = (op == TK_PLUS) ? (v + r) : (v - r);
  }
  return v;
}

static long long parse_mul_ctx(enum_const_eval_ctx_t *ctx) {
  long long v = parse_unary_ctx(ctx);
  while (curtok(ctx)->kind == TK_MUL || curtok(ctx)->kind == TK_DIV ||
         curtok(ctx)->kind == TK_MOD) {
    token_kind_t op = curtok(ctx)->kind;
    set_curtok(ctx, curtok(ctx)->next);
    long long r = parse_unary_ctx(ctx);
    if (op == TK_MUL) v *= r;
    else if (op == TK_DIV) v /= r;
    else v %= r;
  }
  return v;
}

static long long parse_unary_ctx(enum_const_eval_ctx_t *ctx) {
  if (curtok(ctx)->kind == TK_PLUS) {
    set_curtok(ctx, curtok(ctx)->next);
    return parse_unary_ctx(ctx);
  }
  if (curtok(ctx)->kind == TK_MINUS) {
    set_curtok(ctx, curtok(ctx)->next);
    return -parse_unary_ctx(ctx);
  }
  if (curtok(ctx)->kind == TK_TILDE) {
    set_curtok(ctx, curtok(ctx)->next);
    return ~parse_unary_ctx(ctx);
  }
  if (curtok(ctx)->kind == TK_BANG) {
    set_curtok(ctx, curtok(ctx)->next);
    return !parse_unary_ctx(ctx);
  }
  // sizeof / _Alignof: ag_c では基本型に対して両者は同じ値を返すので、
  // 定数式評価器ではトークンだけ違う形で共通の処理を通す。
  // `_Alignas(_Alignof(T))` のネストでも _Alignof が定数として扱えるようにする。
  if (curtok(ctx)->kind == TK_SIZEOF || curtok(ctx)->kind == TK_ALIGNOF) {
    set_curtok(ctx, curtok(ctx)->next);
    if (curtok(ctx)->kind == TK_LPAREN) {
      set_curtok(ctx, curtok(ctx)->next);
      int sz = 8;
      if (psx_ctx_is_type_token(curtok(ctx)->kind) ||
          psx_ctx_is_tag_keyword(curtok(ctx)->kind) ||
          psx_ctx_is_typedef_name_token_in(
              ctx->semantic_context, curtok(ctx))) {
        psx_ctx_get_type_info(curtok(ctx)->kind, NULL, &sz);
        if (psx_ctx_is_tag_keyword(curtok(ctx)->kind)) {
          token_kind_t tk = curtok(ctx)->kind;
          set_curtok(ctx, curtok(ctx)->next);
          token_ident_t *tag =
              tk_consume_ident_ctx(ctx->tokenizer_context);
          if (tag && ps_ctx_has_tag_type_in(
                         ctx->semantic_context,
                         tk, tag->str, tag->len)) {
            sz = ps_ctx_get_tag_size_in(
                ctx->semantic_context, tk, tag->str, tag->len);
          }
        } else if (psx_ctx_is_typedef_name_token_in(
                       ctx->semantic_context, curtok(ctx))) {
          token_ident_t *id = (token_ident_t *)curtok(ctx);
          int td_sizeof = 8;
          if (psx_ctx_find_typedef_sizeof_in(
                  ctx->semantic_context,
                  id->str, id->len, &td_sizeof))
            sz = td_sizeof;
          set_curtok(ctx, curtok(ctx)->next);
        } else {
          set_curtok(ctx, curtok(ctx)->next);
          while (psx_ctx_is_type_token(curtok(ctx)->kind))
            set_curtok(ctx, curtok(ctx)->next);
        }
        while (curtok(ctx)->kind == TK_MUL) {
          sz = 8;
          set_curtok(ctx, curtok(ctx)->next);
        }
      }
      tk_expect_ctx(ctx->tokenizer_context, ')');
      return sz;
    }
    return parse_unary_ctx(ctx);
  }
  return parse_primary_ctx(ctx);
}

static long long parse_primary_ctx(enum_const_eval_ctx_t *ctx) {
  if (curtok(ctx)->kind == TK_LPAREN) {
    set_curtok(ctx, curtok(ctx)->next);
    long long v = parse_conditional_ctx(ctx);
    tk_expect_ctx(ctx->tokenizer_context, ')');
    return v;
  }
  token_ident_t *id = tk_consume_ident_ctx(ctx->tokenizer_context);
  if (id) {
    long long v = 0;
    if (!ps_ctx_find_enum_const_in(
            ctx->semantic_context, id->str, id->len, &v)) {
      ps_diag_ctx_in(
          diagnostics(ctx), curtok(ctx), "enum",
          diag_message_for_in(
              diagnostics(ctx), DIAG_ERR_PARSER_ENUM_CONST_UNDEFINED),
          id->len, id->str);
    }
    return v;
  }
  /* case ラベル文脈では int 範囲外の整数リテラルも long long として受理する。 */
  if (ctx->allow_wide_const && curtok(ctx)->kind == TK_NUM &&
      tk_as_num(curtok(ctx))->num_kind == TK_NUM_KIND_INT) {
    long long v = tk_as_num_int(curtok(ctx))->val;
    set_curtok(ctx, curtok(ctx)->next);
    return v;
  }
  return tk_expect_number_ctx(ctx->tokenizer_context);
}

static psx_parsed_enum_expr_t *parse_prepared_conditional(
    enum_const_eval_ctx_t *ctx);

static psx_parsed_enum_expr_t *new_prepared_expr(
    enum_const_eval_ctx_t *ctx,
    psx_parsed_enum_expr_kind_t kind, token_kind_t op,
    psx_parsed_enum_expr_t *lhs, psx_parsed_enum_expr_t *rhs) {
  psx_parsed_enum_expr_t *expression = calloc(1, sizeof(*expression));
  if (!expression) {
    ps_diag_ctx_in(
        diagnostics(ctx), curtok(ctx), "enum-syntax",
        "enum expression allocation failed");
  }
  expression->kind = kind;
  expression->op = op;
  expression->diagnostic_token = curtok(ctx);
  expression->lhs = lhs;
  expression->rhs = rhs;
  return expression;
}

static psx_parsed_enum_expr_t *parse_prepared_primary(
    enum_const_eval_ctx_t *ctx) {
  if (tk_consume_ctx(ctx->tokenizer_context, '(')) {
    psx_parsed_enum_expr_t *expression =
        parse_prepared_conditional(ctx);
    tk_expect_ctx(ctx->tokenizer_context, ')');
    return expression;
  }
  token_ident_t *identifier =
      tk_consume_ident_ctx(ctx->tokenizer_context);
  if (identifier) {
    psx_parsed_enum_expr_t *expression = new_prepared_expr(
        ctx, PSX_ENUM_EXPR_IDENTIFIER, TK_IDENT, NULL, NULL);
    expression->identifier = identifier->str;
    expression->identifier_len = identifier->len;
    expression->diagnostic_token = (token_t *)identifier;
    return expression;
  }
  token_t *diagnostic_token = curtok(ctx);
  psx_parsed_enum_expr_t *expression = new_prepared_expr(
      ctx, PSX_ENUM_EXPR_VALUE, TK_NUM, NULL, NULL);
  expression->value = tk_expect_number_ctx(ctx->tokenizer_context);
  expression->diagnostic_token = diagnostic_token;
  return expression;
}

static psx_parsed_enum_expr_t *parse_prepared_unary(
    enum_const_eval_ctx_t *ctx) {
  token_kind_t op = curtok(ctx)->kind;
  if (op == TK_PLUS || op == TK_MINUS || op == TK_TILDE || op == TK_BANG) {
    set_curtok(ctx, curtok(ctx)->next);
    return new_prepared_expr(
        ctx, PSX_ENUM_EXPR_UNARY, op, parse_prepared_unary(ctx), NULL);
  }
  if (op == TK_SIZEOF || op == TK_ALIGNOF) {
    long long value = parse_unary_ctx(ctx);
    psx_parsed_enum_expr_t *expression = new_prepared_expr(
        ctx, PSX_ENUM_EXPR_VALUE, TK_NUM, NULL, NULL);
    expression->value = value;
    return expression;
  }
  return parse_prepared_primary(ctx);
}

typedef psx_parsed_enum_expr_t *(*prepared_enum_parse_fn_t)(
    enum_const_eval_ctx_t *ctx);

static psx_parsed_enum_expr_t *parse_prepared_binary_level(
    enum_const_eval_ctx_t *ctx,
    prepared_enum_parse_fn_t next_level,
    const token_kind_t *operators, int operator_count) {
  psx_parsed_enum_expr_t *expression = next_level(ctx);
  for (;;) {
    token_kind_t op = curtok(ctx)->kind;
    int matches = 0;
    for (int i = 0; i < operator_count; i++) {
      if (op == operators[i]) {
        matches = 1;
        break;
      }
    }
    if (!matches) return expression;
    set_curtok(ctx, curtok(ctx)->next);
    expression = new_prepared_expr(
        ctx, PSX_ENUM_EXPR_BINARY, op, expression, next_level(ctx));
  }
}

static psx_parsed_enum_expr_t *parse_prepared_mul(
    enum_const_eval_ctx_t *ctx) {
  const token_kind_t operators[] = {TK_MUL, TK_DIV, TK_MOD};
  return parse_prepared_binary_level(
      ctx, parse_prepared_unary, operators, 3);
}

static psx_parsed_enum_expr_t *parse_prepared_add(
    enum_const_eval_ctx_t *ctx) {
  const token_kind_t operators[] = {TK_PLUS, TK_MINUS};
  return parse_prepared_binary_level(
      ctx, parse_prepared_mul, operators, 2);
}

static psx_parsed_enum_expr_t *parse_prepared_shift(
    enum_const_eval_ctx_t *ctx) {
  const token_kind_t operators[] = {TK_SHL, TK_SHR};
  return parse_prepared_binary_level(
      ctx, parse_prepared_add, operators, 2);
}

static psx_parsed_enum_expr_t *parse_prepared_rel(
    enum_const_eval_ctx_t *ctx) {
  const token_kind_t operators[] = {TK_LT, TK_LE, TK_GT, TK_GE};
  return parse_prepared_binary_level(
      ctx, parse_prepared_shift, operators, 4);
}

static psx_parsed_enum_expr_t *parse_prepared_eq(
    enum_const_eval_ctx_t *ctx) {
  const token_kind_t operators[] = {TK_EQEQ, TK_NEQ};
  return parse_prepared_binary_level(
      ctx, parse_prepared_rel, operators, 2);
}

static psx_parsed_enum_expr_t *parse_prepared_bitand(
    enum_const_eval_ctx_t *ctx) {
  const token_kind_t operators[] = {TK_AMP};
  return parse_prepared_binary_level(
      ctx, parse_prepared_eq, operators, 1);
}

static psx_parsed_enum_expr_t *parse_prepared_bitxor(
    enum_const_eval_ctx_t *ctx) {
  const token_kind_t operators[] = {TK_CARET};
  return parse_prepared_binary_level(
      ctx, parse_prepared_bitand, operators, 1);
}

static psx_parsed_enum_expr_t *parse_prepared_bitor(
    enum_const_eval_ctx_t *ctx) {
  const token_kind_t operators[] = {TK_PIPE};
  return parse_prepared_binary_level(
      ctx, parse_prepared_bitxor, operators, 1);
}

static psx_parsed_enum_expr_t *parse_prepared_logand(
    enum_const_eval_ctx_t *ctx) {
  const token_kind_t operators[] = {TK_ANDAND};
  return parse_prepared_binary_level(
      ctx, parse_prepared_bitor, operators, 1);
}

static psx_parsed_enum_expr_t *parse_prepared_logor(
    enum_const_eval_ctx_t *ctx) {
  const token_kind_t operators[] = {TK_OROR};
  return parse_prepared_binary_level(
      ctx, parse_prepared_logand, operators, 1);
}

static psx_parsed_enum_expr_t *parse_prepared_conditional(
    enum_const_eval_ctx_t *ctx) {
  psx_parsed_enum_expr_t *condition = parse_prepared_logor(ctx);
  if (!tk_consume_ctx(ctx->tokenizer_context, '?')) return condition;
  psx_parsed_enum_expr_t *expression = new_prepared_expr(
      ctx, PSX_ENUM_EXPR_CONDITIONAL, TK_QUESTION, condition,
      parse_prepared_conditional(ctx));
  tk_expect_ctx(ctx->tokenizer_context, ':');
  expression->alternative = parse_prepared_conditional(ctx);
  return expression;
}

static void dispose_prepared_enum_expr(psx_parsed_enum_expr_t *expression) {
  if (!expression) return;
  dispose_prepared_enum_expr(expression->lhs);
  dispose_prepared_enum_expr(expression->rhs);
  dispose_prepared_enum_expr(expression->alternative);
  free(expression);
}

void psx_parse_enum_body_in_contexts(
    psx_parsed_enum_body_t *body,
    psx_semantic_context_t *semantic_context,
    tokenizer_context_t *tokenizer_context) {
  if (!body || !semantic_context || !tokenizer_context) return;
  enum_const_eval_ctx_t ctx = {
      .semantic_context = semantic_context,
      .tokenizer_context = tokenizer_context,
  };
  memset(body, 0, sizeof(*body));
  while (!tk_consume_ctx(tokenizer_context, '}')) {
    if (body->member_count >= PS_MAX_DECLARATOR_COUNT) {
      ps_diag_ctx_in(
          diagnostics(&ctx), curtok(&ctx), "enum-syntax",
          "enum member limit exceeded");
    }
    if (body->member_count == body->member_capacity) {
      body->member_capacity = pda_next_cap_in(
          diagnostics(&ctx), body->member_capacity,
          body->member_count + 1);
      body->members = pda_xreallocarray_in(
          diagnostics(&ctx), body->members,
          (size_t)body->member_capacity, sizeof(*body->members));
    }
    psx_parsed_enum_member_t *member =
        &body->members[body->member_count++];
    memset(member, 0, sizeof(*member));
    member->enumerator = tk_consume_ident_ctx(tokenizer_context);
    if (!member->enumerator) {
      ps_diag_missing_in(
          diagnostics(&ctx), curtok(&ctx),
          diag_text_for_in(
              diagnostics(&ctx), DIAG_TEXT_ENUMERATOR_NAME));
    }
    if (tk_consume_ctx(tokenizer_context, '=')) {
      member->initializer = parse_prepared_conditional(&ctx);
    }
    if (tk_consume_ctx(tokenizer_context, '}')) break;
    tk_expect_ctx(tokenizer_context, ',');
    if (tk_consume_ctx(tokenizer_context, '}')) break;
  }
}

void psx_dispose_parsed_enum_body(psx_parsed_enum_body_t *body) {
  if (!body) return;
  for (int i = 0; i < body->member_count; i++)
    dispose_prepared_enum_expr(body->members[i].initializer);
  free(body->members);
  memset(body, 0, sizeof(*body));
}
