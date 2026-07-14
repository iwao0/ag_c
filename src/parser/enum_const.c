#include "enum_const.h"
#include "diag.h"
#include "dynarray.h"
#include "semantic_ctx.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"

#include <stdlib.h>
#include <string.h>

static inline token_t *curtok(void) { return tk_get_current_token(); }
static inline void set_curtok(token_t *tok) { tk_set_current_token(tok); }

typedef struct {
  /* case ラベルでは INT_MAX を超える整数リテラルも long long として受理する
   * (C11 6.8.4.2)。enum 定数・配列次元・_Alignas・ビットフィールド幅の
   * 経路は従来どおり tk_expect_number() で int に制約する。 */
  int allow_wide_const;
  psx_semantic_context_t *semantic_context;
} enum_const_eval_ctx_t;

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

long long psx_parse_enum_const_expr(void) {
  return psx_parse_enum_const_expr_in_context(ps_ctx_active());
}

long long psx_parse_enum_const_expr_in_context(
    psx_semantic_context_t *semantic_context) {
  enum_const_eval_ctx_t ctx = {
      .semantic_context = semantic_context,
  };
  return parse_conditional_ctx(&ctx);
}

long long psx_parse_case_const_expr(void) {
  return psx_parse_case_const_expr_in_context(ps_ctx_active());
}

long long psx_parse_case_const_expr_in_context(
    psx_semantic_context_t *semantic_context) {
  enum_const_eval_ctx_t ctx = {
      .allow_wide_const = 1,
      .semantic_context = semantic_context,
  };
  return parse_conditional_ctx(&ctx);
}

long long psx_eval_parsed_enum_const_expr(token_t *start, token_t *end) {
  return psx_eval_parsed_enum_const_expr_in_context(
      ps_ctx_active(), start, end);
}

long long psx_eval_parsed_enum_const_expr_in_context(
    psx_semantic_context_t *semantic_context,
    token_t *start, token_t *end) {
  if (!start || !end) {
    ps_diag_ctx(curtok(), "constant-expression",
                 "missing parsed constant expression range");
  }
  token_t *saved_token = curtok();
  set_curtok(start);
  long long value = psx_parse_enum_const_expr_in_context(
      semantic_context);
  if (curtok() != end) {
    ps_diag_ctx(curtok(), "constant-expression",
                 "parsed constant expression was not fully consumed");
  }
  set_curtok(saved_token);
  return value;
}

static long long parse_conditional_ctx(enum_const_eval_ctx_t *ctx) {
  long long cond = parse_logor_ctx(ctx);
  if (!tk_consume('?')) return cond;
  long long then_v = parse_conditional_ctx(ctx);
  tk_expect(':');
  long long else_v = parse_conditional_ctx(ctx);
  return cond ? then_v : else_v;
}

static long long parse_logor_ctx(enum_const_eval_ctx_t *ctx) {
  long long v = parse_logand_ctx(ctx);
  while (curtok()->kind == TK_OROR) {
    set_curtok(curtok()->next);
    long long r = parse_logand_ctx(ctx);
    v = (v || r) ? 1 : 0;
  }
  return v;
}

static long long parse_logand_ctx(enum_const_eval_ctx_t *ctx) {
  long long v = parse_bitor_ctx(ctx);
  while (curtok()->kind == TK_ANDAND) {
    set_curtok(curtok()->next);
    long long r = parse_bitor_ctx(ctx);
    v = (v && r) ? 1 : 0;
  }
  return v;
}

static long long parse_bitor_ctx(enum_const_eval_ctx_t *ctx) {
  long long v = parse_bitxor_ctx(ctx);
  while (curtok()->kind == TK_PIPE) {
    set_curtok(curtok()->next);
    long long r = parse_bitxor_ctx(ctx);
    v |= r;
  }
  return v;
}

static long long parse_bitxor_ctx(enum_const_eval_ctx_t *ctx) {
  long long v = parse_bitand_ctx(ctx);
  while (curtok()->kind == TK_CARET) {
    set_curtok(curtok()->next);
    long long r = parse_bitand_ctx(ctx);
    v ^= r;
  }
  return v;
}

static long long parse_bitand_ctx(enum_const_eval_ctx_t *ctx) {
  long long v = parse_eq_ctx(ctx);
  while (curtok()->kind == TK_AMP) {
    set_curtok(curtok()->next);
    long long r = parse_eq_ctx(ctx);
    v &= r;
  }
  return v;
}

static long long parse_eq_ctx(enum_const_eval_ctx_t *ctx) {
  long long v = parse_rel_ctx(ctx);
  while (curtok()->kind == TK_EQEQ || curtok()->kind == TK_NEQ) {
    token_kind_t op = curtok()->kind;
    set_curtok(curtok()->next);
    long long r = parse_rel_ctx(ctx);
    v = (op == TK_EQEQ) ? (v == r) : (v != r);
  }
  return v;
}

static long long parse_rel_ctx(enum_const_eval_ctx_t *ctx) {
  long long v = parse_shift_ctx(ctx);
  while (curtok()->kind == TK_LT || curtok()->kind == TK_LE || curtok()->kind == TK_GT || curtok()->kind == TK_GE) {
    token_kind_t op = curtok()->kind;
    set_curtok(curtok()->next);
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
  while (curtok()->kind == TK_SHL || curtok()->kind == TK_SHR) {
    token_kind_t op = curtok()->kind;
    set_curtok(curtok()->next);
    long long r = parse_add_ctx(ctx);
    v = (op == TK_SHL) ? (v << r) : (v >> r);
  }
  return v;
}

static long long parse_add_ctx(enum_const_eval_ctx_t *ctx) {
  long long v = parse_mul_ctx(ctx);
  while (curtok()->kind == TK_PLUS || curtok()->kind == TK_MINUS) {
    token_kind_t op = curtok()->kind;
    set_curtok(curtok()->next);
    long long r = parse_mul_ctx(ctx);
    v = (op == TK_PLUS) ? (v + r) : (v - r);
  }
  return v;
}

static long long parse_mul_ctx(enum_const_eval_ctx_t *ctx) {
  long long v = parse_unary_ctx(ctx);
  while (curtok()->kind == TK_MUL || curtok()->kind == TK_DIV || curtok()->kind == TK_MOD) {
    token_kind_t op = curtok()->kind;
    set_curtok(curtok()->next);
    long long r = parse_unary_ctx(ctx);
    if (op == TK_MUL) v *= r;
    else if (op == TK_DIV) v /= r;
    else v %= r;
  }
  return v;
}

static long long parse_unary_ctx(enum_const_eval_ctx_t *ctx) {
  if (curtok()->kind == TK_PLUS) {
    set_curtok(curtok()->next);
    return parse_unary_ctx(ctx);
  }
  if (curtok()->kind == TK_MINUS) {
    set_curtok(curtok()->next);
    return -parse_unary_ctx(ctx);
  }
  if (curtok()->kind == TK_TILDE) {
    set_curtok(curtok()->next);
    return ~parse_unary_ctx(ctx);
  }
  if (curtok()->kind == TK_BANG) {
    set_curtok(curtok()->next);
    return !parse_unary_ctx(ctx);
  }
  // sizeof / _Alignof: ag_c では基本型に対して両者は同じ値を返すので、
  // 定数式評価器ではトークンだけ違う形で共通の処理を通す。
  // `_Alignas(_Alignof(T))` のネストでも _Alignof が定数として扱えるようにする。
  if (curtok()->kind == TK_SIZEOF || curtok()->kind == TK_ALIGNOF) {
    set_curtok(curtok()->next);
    if (curtok()->kind == TK_LPAREN) {
      set_curtok(curtok()->next);
      int sz = 8;
      if (psx_ctx_is_type_token(curtok()->kind) ||
          psx_ctx_is_tag_keyword(curtok()->kind) ||
          psx_ctx_is_typedef_name_token_in(
              ctx->semantic_context, curtok())) {
        psx_ctx_get_type_info(curtok()->kind, NULL, &sz);
        if (psx_ctx_is_tag_keyword(curtok()->kind)) {
          token_kind_t tk = curtok()->kind;
          set_curtok(curtok()->next);
          token_ident_t *tag = tk_consume_ident();
          if (tag && ps_ctx_has_tag_type_in(
                         ctx->semantic_context,
                         tk, tag->str, tag->len)) {
            sz = ps_ctx_get_tag_size_in(
                ctx->semantic_context, tk, tag->str, tag->len);
          }
        } else if (psx_ctx_is_typedef_name_token_in(
                       ctx->semantic_context, curtok())) {
          token_ident_t *id = (token_ident_t *)curtok();
          int td_sizeof = 8;
          if (psx_ctx_find_typedef_sizeof_in(
                  ctx->semantic_context,
                  id->str, id->len, &td_sizeof))
            sz = td_sizeof;
          set_curtok(curtok()->next);
        } else {
          set_curtok(curtok()->next);
          while (psx_ctx_is_type_token(curtok()->kind)) set_curtok(curtok()->next);
        }
        while (curtok()->kind == TK_MUL) { sz = 8; set_curtok(curtok()->next); }
      }
      tk_expect(')');
      return sz;
    }
    return parse_unary_ctx(ctx);
  }
  return parse_primary_ctx(ctx);
}

static long long parse_primary_ctx(enum_const_eval_ctx_t *ctx) {
  if (curtok()->kind == TK_LPAREN) {
    set_curtok(curtok()->next);
    long long v = parse_conditional_ctx(ctx);
    tk_expect(')');
    return v;
  }
  token_ident_t *id = tk_consume_ident();
  if (id) {
    long long v = 0;
    if (!ps_ctx_find_enum_const_in(
            ctx->semantic_context, id->str, id->len, &v)) {
      ps_diag_ctx(curtok(), "enum", diag_message_for(DIAG_ERR_PARSER_ENUM_CONST_UNDEFINED),
                   id->len, id->str);
    }
    return v;
  }
  /* case ラベル文脈では int 範囲外の整数リテラルも long long として受理する。 */
  if (ctx->allow_wide_const && curtok()->kind == TK_NUM &&
      tk_as_num(curtok())->num_kind == TK_NUM_KIND_INT) {
    long long v = tk_as_num_int(curtok())->val;
    set_curtok(curtok()->next);
    return v;
  }
  return tk_expect_number();
}

static psx_parsed_enum_expr_t *parse_prepared_conditional(void);

static psx_parsed_enum_expr_t *new_prepared_expr(
    psx_parsed_enum_expr_kind_t kind, token_kind_t op,
    psx_parsed_enum_expr_t *lhs, psx_parsed_enum_expr_t *rhs) {
  psx_parsed_enum_expr_t *expression = calloc(1, sizeof(*expression));
  if (!expression) {
    ps_diag_ctx(curtok(), "enum-syntax",
                 "enum expression allocation failed");
  }
  expression->kind = kind;
  expression->op = op;
  expression->diagnostic_token = curtok();
  expression->lhs = lhs;
  expression->rhs = rhs;
  return expression;
}

static psx_parsed_enum_expr_t *parse_prepared_primary(void) {
  if (tk_consume('(')) {
    psx_parsed_enum_expr_t *expression = parse_prepared_conditional();
    tk_expect(')');
    return expression;
  }
  token_ident_t *identifier = tk_consume_ident();
  if (identifier) {
    psx_parsed_enum_expr_t *expression = new_prepared_expr(
        PSX_ENUM_EXPR_IDENTIFIER, TK_IDENT, NULL, NULL);
    expression->identifier = identifier->str;
    expression->identifier_len = identifier->len;
    expression->diagnostic_token = (token_t *)identifier;
    return expression;
  }
  token_t *diagnostic_token = curtok();
  psx_parsed_enum_expr_t *expression = new_prepared_expr(
      PSX_ENUM_EXPR_VALUE, TK_NUM, NULL, NULL);
  expression->value = tk_expect_number();
  expression->diagnostic_token = diagnostic_token;
  return expression;
}

static psx_parsed_enum_expr_t *parse_prepared_unary(void) {
  token_kind_t op = curtok()->kind;
  if (op == TK_PLUS || op == TK_MINUS || op == TK_TILDE || op == TK_BANG) {
    set_curtok(curtok()->next);
    return new_prepared_expr(
        PSX_ENUM_EXPR_UNARY, op, parse_prepared_unary(), NULL);
  }
  if (op == TK_SIZEOF || op == TK_ALIGNOF) {
    enum_const_eval_ctx_t immediate = {0};
    long long value = parse_unary_ctx(&immediate);
    psx_parsed_enum_expr_t *expression = new_prepared_expr(
        PSX_ENUM_EXPR_VALUE, TK_NUM, NULL, NULL);
    expression->value = value;
    return expression;
  }
  return parse_prepared_primary();
}

static psx_parsed_enum_expr_t *parse_prepared_binary_level(
    psx_parsed_enum_expr_t *(*next_level)(void),
    const token_kind_t *operators, int operator_count) {
  psx_parsed_enum_expr_t *expression = next_level();
  for (;;) {
    token_kind_t op = curtok()->kind;
    int matches = 0;
    for (int i = 0; i < operator_count; i++) {
      if (op == operators[i]) {
        matches = 1;
        break;
      }
    }
    if (!matches) return expression;
    set_curtok(curtok()->next);
    expression = new_prepared_expr(
        PSX_ENUM_EXPR_BINARY, op, expression, next_level());
  }
}

static psx_parsed_enum_expr_t *parse_prepared_mul(void) {
  const token_kind_t operators[] = {TK_MUL, TK_DIV, TK_MOD};
  return parse_prepared_binary_level(
      parse_prepared_unary, operators, 3);
}

static psx_parsed_enum_expr_t *parse_prepared_add(void) {
  const token_kind_t operators[] = {TK_PLUS, TK_MINUS};
  return parse_prepared_binary_level(parse_prepared_mul, operators, 2);
}

static psx_parsed_enum_expr_t *parse_prepared_shift(void) {
  const token_kind_t operators[] = {TK_SHL, TK_SHR};
  return parse_prepared_binary_level(parse_prepared_add, operators, 2);
}

static psx_parsed_enum_expr_t *parse_prepared_rel(void) {
  const token_kind_t operators[] = {TK_LT, TK_LE, TK_GT, TK_GE};
  return parse_prepared_binary_level(parse_prepared_shift, operators, 4);
}

static psx_parsed_enum_expr_t *parse_prepared_eq(void) {
  const token_kind_t operators[] = {TK_EQEQ, TK_NEQ};
  return parse_prepared_binary_level(parse_prepared_rel, operators, 2);
}

static psx_parsed_enum_expr_t *parse_prepared_bitand(void) {
  const token_kind_t operators[] = {TK_AMP};
  return parse_prepared_binary_level(parse_prepared_eq, operators, 1);
}

static psx_parsed_enum_expr_t *parse_prepared_bitxor(void) {
  const token_kind_t operators[] = {TK_CARET};
  return parse_prepared_binary_level(parse_prepared_bitand, operators, 1);
}

static psx_parsed_enum_expr_t *parse_prepared_bitor(void) {
  const token_kind_t operators[] = {TK_PIPE};
  return parse_prepared_binary_level(parse_prepared_bitxor, operators, 1);
}

static psx_parsed_enum_expr_t *parse_prepared_logand(void) {
  const token_kind_t operators[] = {TK_ANDAND};
  return parse_prepared_binary_level(parse_prepared_bitor, operators, 1);
}

static psx_parsed_enum_expr_t *parse_prepared_logor(void) {
  const token_kind_t operators[] = {TK_OROR};
  return parse_prepared_binary_level(parse_prepared_logand, operators, 1);
}

static psx_parsed_enum_expr_t *parse_prepared_conditional(void) {
  psx_parsed_enum_expr_t *condition = parse_prepared_logor();
  if (!tk_consume('?')) return condition;
  psx_parsed_enum_expr_t *expression = new_prepared_expr(
      PSX_ENUM_EXPR_CONDITIONAL, TK_QUESTION, condition,
      parse_prepared_conditional());
  tk_expect(':');
  expression->alternative = parse_prepared_conditional();
  return expression;
}

static void dispose_prepared_enum_expr(psx_parsed_enum_expr_t *expression) {
  if (!expression) return;
  dispose_prepared_enum_expr(expression->lhs);
  dispose_prepared_enum_expr(expression->rhs);
  dispose_prepared_enum_expr(expression->alternative);
  free(expression);
}

void psx_parse_enum_body(psx_parsed_enum_body_t *body) {
  if (!body) return;
  memset(body, 0, sizeof(*body));
  while (!tk_consume('}')) {
    if (body->member_count >= PS_MAX_DECLARATOR_COUNT) {
      ps_diag_ctx(curtok(), "enum-syntax",
                   "enum member limit exceeded");
    }
    if (body->member_count == body->member_capacity) {
      body->member_capacity = pda_next_cap(
          body->member_capacity, body->member_count + 1);
      body->members = pda_xreallocarray(
          body->members, (size_t)body->member_capacity,
          sizeof(*body->members));
    }
    psx_parsed_enum_member_t *member =
        &body->members[body->member_count++];
    memset(member, 0, sizeof(*member));
    member->enumerator = tk_consume_ident();
    if (!member->enumerator) {
      ps_diag_missing(curtok(), diag_text_for(DIAG_TEXT_ENUMERATOR_NAME));
    }
    if (tk_consume('=')) {
      member->initializer = parse_prepared_conditional();
    }
    if (tk_consume('}')) break;
    tk_expect(',');
    if (tk_consume('}')) break;
  }
}

void psx_dispose_parsed_enum_body(psx_parsed_enum_body_t *body) {
  if (!body) return;
  for (int i = 0; i < body->member_count; i++)
    dispose_prepared_enum_expr(body->members[i].initializer);
  free(body->members);
  memset(body, 0, sizeof(*body));
}
