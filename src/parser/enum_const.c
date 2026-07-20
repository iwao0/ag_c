#include "enum_const.h"
#include "diag.h"
#include "dynarray.h"
#include "semantic_ctx.h"
#include "../diag/diag.h"
#include "../target_info.h"
#include "../tokenizer/tokenizer.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
  /* case ラベルでは INT_MAX を超える整数リテラルも long long として受理する
   * (C11 6.8.4.2)。enum 定数・配列次元・_Alignas・ビットフィールド幅の
   * 経路は従来どおり整数 token の期待処理で int に制約する。 */
  int allow_wide_const;
  psx_semantic_context_t *semantic_context;
  psx_name_classifier_t name_classifier;
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
    const psx_name_classifier_t *name_classifier,
    tokenizer_context_t *tokenizer_context) {
  enum_const_eval_ctx_t ctx = {
      .semantic_context = semantic_context,
      .name_classifier =
          name_classifier ? *name_classifier : (psx_name_classifier_t){0},
      .tokenizer_context = tokenizer_context,
  };
  return parse_conditional_ctx(&ctx);
}

long long psx_parse_case_const_expr_in_contexts(
    psx_semantic_context_t *semantic_context,
    const psx_name_classifier_t *name_classifier,
    tokenizer_context_t *tokenizer_context) {
  enum_const_eval_ctx_t ctx = {
      .allow_wide_const = 1,
      .semantic_context = semantic_context,
      .name_classifier =
          name_classifier ? *name_classifier : (psx_name_classifier_t){0},
      .tokenizer_context = tokenizer_context,
  };
  return parse_conditional_ctx(&ctx);
}

long long psx_eval_parsed_enum_const_expr_in_context(
    psx_semantic_context_t *semantic_context,
    const psx_name_classifier_t *name_classifier,
    token_t *start, token_t *end) {
  if (!start || !end) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(semantic_context), start,
        "constant-expression", "missing parsed constant expression range");
  }
  tokenizer_context_t tokenizer_context;
  if (!tk_cursor_context_init(
          &tokenizer_context, ps_ctx_diagnostics(semantic_context))) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(semantic_context), start,
        "constant-expression", "missing tokenizer diagnostics");
  }
  tk_set_current_token_ctx(&tokenizer_context, start);
  long long value = psx_parse_enum_const_expr_in_contexts(
      semantic_context, name_classifier, &tokenizer_context);
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
  // sizeof / _Alignof share type-name parsing, but select distinct values
  // from the active target layout.
  if (curtok(ctx)->kind == TK_SIZEOF || curtok(ctx)->kind == TK_ALIGNOF) {
    int wants_alignment = curtok(ctx)->kind == TK_ALIGNOF;
    set_curtok(ctx, curtok(ctx)->next);
    if (curtok(ctx)->kind == TK_LPAREN) {
      set_curtok(ctx, curtok(ctx)->next);
      int size = 0;
      int alignment = 1;
      if (psx_ctx_is_type_token(curtok(ctx)->kind) ||
          psx_ctx_is_tag_keyword(curtok(ctx)->kind) ||
          ps_name_classifier_is_typedef_name(
              &ctx->name_classifier, curtok(ctx))) {
        psx_ctx_get_type_token_layout_in(
            ctx->semantic_context, curtok(ctx)->kind,
            &size, &alignment);
        if (psx_ctx_is_tag_keyword(curtok(ctx)->kind)) {
          token_kind_t tk = curtok(ctx)->kind;
          set_curtok(ctx, curtok(ctx)->next);
          token_ident_t *tag =
              tk_consume_ident_ctx(ctx->tokenizer_context);
          if (tag && ps_ctx_has_tag_type_in(
                         ctx->semantic_context,
                         tk, tag->str, tag->len)) {
            size = ps_ctx_get_tag_size_in(
                ctx->semantic_context, tk, tag->str, tag->len);
            alignment = ps_ctx_get_tag_align_in(
                ctx->semantic_context, tk, tag->str, tag->len);
          }
        } else if (ps_name_classifier_is_typedef_name(
                       &ctx->name_classifier, curtok(ctx))) {
          token_ident_t *id = (token_ident_t *)curtok(ctx);
          psx_ctx_find_typedef_layout_in(
              ctx->semantic_context,
              id->str, id->len, &size, &alignment);
          set_curtok(ctx, curtok(ctx)->next);
        } else {
          set_curtok(ctx, curtok(ctx)->next);
          while (psx_ctx_is_type_token(curtok(ctx)->kind))
            set_curtok(ctx, curtok(ctx)->next);
        }
        while (curtok(ctx)->kind == TK_MUL) {
          const ag_data_layout_t *data_layout =
              ps_ctx_data_layout(ctx->semantic_context);
          size = ag_data_layout_pointer_size(data_layout);
          alignment = ag_data_layout_pointer_alignment(data_layout);
          set_curtok(ctx, curtok(ctx)->next);
        }
      }
      tk_expect_ctx(ctx->tokenizer_context, ')');
      return wants_alignment ? alignment : size;
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

void psx_parse_enum_body_syntax(
    psx_parsed_enum_body_t *body,
    const psx_enum_body_syntax_context_t *context) {
  if (!body || !context || !context->diagnostics ||
      !context->parse_assignment_expression ||
      !context->tokenizer_context)
    return;
  tokenizer_context_t *tokenizer_context = context->tokenizer_context;
  memset(body, 0, sizeof(*body));
  while (!tk_consume_ctx(tokenizer_context, '}')) {
    if (body->member_count >= PS_MAX_DECLARATOR_COUNT) {
      ps_diag_ctx_in(
          context->diagnostics,
          tk_get_current_token_ctx(tokenizer_context), "enum-syntax",
          "enum member limit exceeded");
    }
    if (body->member_count == body->member_capacity) {
      body->member_capacity = pda_next_cap_in(
          context->diagnostics, body->member_capacity,
          body->member_count + 1);
      body->members = pda_xreallocarray_in(
          context->diagnostics, body->members,
          (size_t)body->member_capacity, sizeof(*body->members));
    }
    psx_parsed_enum_member_t *member =
        &body->members[body->member_count++];
    memset(member, 0, sizeof(*member));
    member->enumerator = tk_consume_ident_ctx(tokenizer_context);
    if (!member->enumerator) {
      ps_diag_missing_in(
          context->diagnostics,
          tk_get_current_token_ctx(tokenizer_context),
          diag_text_for_in(
              context->diagnostics, DIAG_TEXT_ENUMERATOR_NAME));
    }
    ps_name_classifier_declare(
        context->name_classifier, (token_t *)member->enumerator, 0);
    if (tk_consume_ctx(tokenizer_context, '='))
      member->initializer = context->parse_assignment_expression(
          context->expression_context);
    if (tk_consume_ctx(tokenizer_context, '}')) break;
    tk_expect_ctx(tokenizer_context, ',');
    if (tk_consume_ctx(tokenizer_context, '}')) break;
  }
}

void psx_dispose_parsed_enum_body(psx_parsed_enum_body_t *body) {
  if (!body) return;
  free(body->members);
  memset(body, 0, sizeof(*body));
}
