#include "enum_const.h"
#include "../semantic/declaration_application.h"
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
  enum_const_eval_ctx_t ctx = {0};
  return parse_conditional_ctx(&ctx);
}

long long psx_parse_case_const_expr(void) {
  enum_const_eval_ctx_t ctx = {.allow_wide_const = 1};
  return parse_conditional_ctx(&ctx);
}

long long ps_eval_parsed_enum_const_expr(token_t *start, token_t *end) {
  if (!start || !end) {
    ps_diag_ctx(curtok(), "constant-expression",
                 "missing parsed constant expression range");
  }
  token_t *saved_token = curtok();
  set_curtok(start);
  long long value = psx_parse_enum_const_expr();
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
      if (psx_ctx_is_type_token(curtok()->kind) || psx_ctx_is_tag_keyword(curtok()->kind) ||
          psx_ctx_is_typedef_name_token(curtok())) {
        psx_ctx_get_type_info(curtok()->kind, NULL, &sz);
        if (psx_ctx_is_tag_keyword(curtok()->kind)) {
          token_kind_t tk = curtok()->kind;
          set_curtok(curtok()->next);
          token_ident_t *tag = tk_consume_ident();
          if (tag && ps_ctx_has_tag_type(tk, tag->str, tag->len)) {
            sz = ps_ctx_get_tag_size(tk, tag->str, tag->len);
          }
        } else if (psx_ctx_is_typedef_name_token(curtok())) {
          token_ident_t *id = (token_ident_t *)curtok();
          int td_elem = 8;
          int td_ptr = 0;
          int td_sizeof = 8;
          psx_typedef_info_t _ti;
          if (ps_ctx_find_typedef_name(id->str, id->len, &_ti)) {
            td_elem = _ti.elem_size;
            td_ptr = _ti.is_pointer;
          }
          if (psx_ctx_find_typedef_sizeof(id->str, id->len, &td_sizeof)) sz = td_sizeof;
          else sz = td_ptr ? 8 : td_elem;
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
    if (!ps_ctx_find_enum_const(id->str, id->len, &v)) {
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

static token_t *find_enum_initializer_end(token_t *token) {
  int paren_depth = 0;
  int bracket_depth = 0;
  int brace_depth = 0;
  for (token_t *current = token; current; current = current->next) {
    if (paren_depth == 0 && bracket_depth == 0 && brace_depth == 0 &&
        (current->kind == TK_COMMA || current->kind == TK_RBRACE))
      return current;
    if (current->kind == TK_LPAREN) paren_depth++;
    else if (current->kind == TK_RPAREN) paren_depth--;
    else if (current->kind == TK_LBRACKET) bracket_depth++;
    else if (current->kind == TK_RBRACKET) bracket_depth--;
    else if (current->kind == TK_LBRACE) brace_depth++;
    else if (current->kind == TK_RBRACE) brace_depth--;
  }
  return NULL;
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
      psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_ENUMERATOR_NAME));
    }
    if (tk_consume('=')) {
      member->initializer_start = curtok();
      member->initializer_end = find_enum_initializer_end(curtok());
      if (!member->initializer_end) {
        ps_diag_ctx(curtok(), "enum-syntax",
                     "unterminated enum initializer");
      }
      set_curtok(member->initializer_end);
    }
    if (tk_consume('}')) break;
    tk_expect(',');
    if (tk_consume('}')) break;
  }
}

int ps_apply_parsed_enum_body(const psx_parsed_enum_body_t *body) {
  if (!body) return 0;
  long long next_value = 0;
  token_t *saved_token = curtok();
  for (int i = 0; i < body->member_count; i++) {
    const psx_parsed_enum_member_t *member = &body->members[i];
    long long value = next_value;
    if (member->initializer_start) {
      value = ps_eval_parsed_enum_const_expr(
          member->initializer_start, member->initializer_end);
    }
    psx_apply_parsed_enum_constant(
        member->enumerator->str, member->enumerator->len, value,
        (token_t *)member->enumerator);
    next_value = value + 1;
  }
  set_curtok(saved_token);
  return body->member_count;
}

void psx_dispose_parsed_enum_body(psx_parsed_enum_body_t *body) {
  if (!body) return;
  free(body->members);
  memset(body, 0, sizeof(*body));
}

int psx_parse_enum_members(void) {
  psx_parsed_enum_body_t body;
  psx_parse_enum_body(&body);
  int member_count = ps_apply_parsed_enum_body(&body);
  psx_dispose_parsed_enum_body(&body);
  return member_count;
}
