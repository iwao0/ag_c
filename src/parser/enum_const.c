#include "internal/enum_const.h"
#include "internal/diag.h"
#include "internal/semantic_ctx.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"

static inline token_t *curtok(void) { return tk_get_current_token(); }
static inline void set_curtok(token_t *tok) { tk_set_current_token(tok); }

static long long parse_conditional(void);
static long long parse_logor(void);
static long long parse_logand(void);
static long long parse_bitor(void);
static long long parse_bitxor(void);
static long long parse_bitand(void);
static long long parse_eq(void);
static long long parse_rel(void);
static long long parse_shift(void);
static long long parse_add(void);
static long long parse_mul(void);
static long long parse_unary(void);
static long long parse_primary(void);

long long psx_parse_enum_const_expr(void) { return parse_conditional(); }

static long long parse_conditional(void) {
  long long cond = parse_logor();
  if (!tk_consume('?')) return cond;
  long long then_v = psx_parse_enum_const_expr();
  tk_expect(':');
  long long else_v = parse_conditional();
  return cond ? then_v : else_v;
}

static long long parse_logor(void) {
  long long v = parse_logand();
  while (curtok()->kind == TK_OROR) {
    set_curtok(curtok()->next);
    long long r = parse_logand();
    v = (v || r) ? 1 : 0;
  }
  return v;
}

static long long parse_logand(void) {
  long long v = parse_bitor();
  while (curtok()->kind == TK_ANDAND) {
    set_curtok(curtok()->next);
    long long r = parse_bitor();
    v = (v && r) ? 1 : 0;
  }
  return v;
}

static long long parse_bitor(void) {
  long long v = parse_bitxor();
  while (curtok()->kind == TK_PIPE) {
    set_curtok(curtok()->next);
    long long r = parse_bitxor();
    v |= r;
  }
  return v;
}

static long long parse_bitxor(void) {
  long long v = parse_bitand();
  while (curtok()->kind == TK_CARET) {
    set_curtok(curtok()->next);
    long long r = parse_bitand();
    v ^= r;
  }
  return v;
}

static long long parse_bitand(void) {
  long long v = parse_eq();
  while (curtok()->kind == TK_AMP) {
    set_curtok(curtok()->next);
    long long r = parse_eq();
    v &= r;
  }
  return v;
}

static long long parse_eq(void) {
  long long v = parse_rel();
  while (curtok()->kind == TK_EQEQ || curtok()->kind == TK_NEQ) {
    token_kind_t op = curtok()->kind;
    set_curtok(curtok()->next);
    long long r = parse_rel();
    v = (op == TK_EQEQ) ? (v == r) : (v != r);
  }
  return v;
}

static long long parse_rel(void) {
  long long v = parse_shift();
  while (curtok()->kind == TK_LT || curtok()->kind == TK_LE || curtok()->kind == TK_GT || curtok()->kind == TK_GE) {
    token_kind_t op = curtok()->kind;
    set_curtok(curtok()->next);
    long long r = parse_shift();
    switch (op) {
      case TK_LT: v = (v < r); break;
      case TK_LE: v = (v <= r); break;
      case TK_GT: v = (v > r); break;
      default: v = (v >= r); break;
    }
  }
  return v;
}

static long long parse_shift(void) {
  long long v = parse_add();
  while (curtok()->kind == TK_SHL || curtok()->kind == TK_SHR) {
    token_kind_t op = curtok()->kind;
    set_curtok(curtok()->next);
    long long r = parse_add();
    v = (op == TK_SHL) ? (v << r) : (v >> r);
  }
  return v;
}

static long long parse_add(void) {
  long long v = parse_mul();
  while (curtok()->kind == TK_PLUS || curtok()->kind == TK_MINUS) {
    token_kind_t op = curtok()->kind;
    set_curtok(curtok()->next);
    long long r = parse_mul();
    v = (op == TK_PLUS) ? (v + r) : (v - r);
  }
  return v;
}

static long long parse_mul(void) {
  long long v = parse_unary();
  while (curtok()->kind == TK_MUL || curtok()->kind == TK_DIV || curtok()->kind == TK_MOD) {
    token_kind_t op = curtok()->kind;
    set_curtok(curtok()->next);
    long long r = parse_unary();
    if (op == TK_MUL) v *= r;
    else if (op == TK_DIV) v /= r;
    else v %= r;
  }
  return v;
}

static long long parse_unary(void) {
  if (curtok()->kind == TK_PLUS) {
    set_curtok(curtok()->next);
    return parse_unary();
  }
  if (curtok()->kind == TK_MINUS) {
    set_curtok(curtok()->next);
    return -parse_unary();
  }
  if (curtok()->kind == TK_TILDE) {
    set_curtok(curtok()->next);
    return ~parse_unary();
  }
  if (curtok()->kind == TK_BANG) {
    set_curtok(curtok()->next);
    return !parse_unary();
  }
  if (curtok()->kind == TK_SIZEOF) {
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
          if (tag && psx_ctx_has_tag_type(tk, tag->str, tag->len)) {
            sz = psx_ctx_get_tag_size(tk, tag->str, tag->len);
          }
        } else if (psx_ctx_is_typedef_name_token(curtok())) {
          token_ident_t *id = (token_ident_t *)curtok();
          token_kind_t td_base = TK_EOF;
          int td_elem = 8;
          tk_float_kind_t td_fp = TK_FLOAT_KIND_NONE;
          token_kind_t td_tag = TK_EOF;
          char *td_tag_name = NULL;
          int td_tag_len = 0;
          int td_ptr = 0;
          int td_sizeof = 8;
          psx_ctx_find_typedef_name(id->str, id->len, &td_base, &td_elem, &td_fp,
                                    &td_tag, &td_tag_name, &td_tag_len, &td_ptr, NULL, NULL, NULL);
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
    return parse_unary();
  }
  return parse_primary();
}

static long long parse_primary(void) {
  if (curtok()->kind == TK_LPAREN) {
    set_curtok(curtok()->next);
    long long v = psx_parse_enum_const_expr();
    tk_expect(')');
    return v;
  }
  token_ident_t *id = tk_consume_ident();
  if (id) {
    long long v = 0;
    if (!psx_ctx_find_enum_const(id->str, id->len, &v)) {
      psx_diag_ctx(curtok(), "enum", diag_message_for(DIAG_ERR_PARSER_ENUM_CONST_UNDEFINED),
                   id->len, id->str);
    }
    return v;
  }
  return tk_expect_number();
}
