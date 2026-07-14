#include "alignas_value.h"
#include "enum_const.h"
#include "semantic_ctx.h"
#include "../tokenizer/tokenizer.h"

static inline token_t *curtok(void) { return tk_get_current_token(); }
static inline void set_curtok(token_t *tok) { tk_set_current_token(tok); }

int psx_parse_alignas_value(void) {
  return psx_parse_alignas_value_in_context(ps_ctx_active());
}

int psx_parse_alignas_value_in_context(
    psx_semantic_context_t *semantic_context) {
  tk_expect('(');
  int val = 1;
  if (psx_ctx_is_type_token(curtok()->kind) ||
      psx_ctx_is_typedef_name_token_in(semantic_context, curtok())) {
    int elem_size = 8;
    if (curtok()->kind == TK_IDENT) {
      token_ident_t *identifier = (token_ident_t *)curtok();
      psx_ctx_find_typedef_sizeof_in(
          semantic_context, identifier->str, identifier->len,
          &elem_size);
    } else {
      psx_ctx_get_type_info(curtok()->kind, NULL, &elem_size);
    }
    val = elem_size;
    while (curtok()->kind != TK_RPAREN && curtok()->kind != TK_EOF) set_curtok(curtok()->next);
  } else {
    long long v = psx_parse_enum_const_expr_in_context(
        semantic_context);
    val = (v > 0) ? (int)v : 1;
  }
  tk_expect(')');
  return val;
}

int psx_eval_parsed_alignas_value(token_t *start, token_t *end) {
  return psx_eval_parsed_alignas_value_in_context(
      ps_ctx_active(), start, end);
}

int psx_eval_parsed_alignas_value_in_context(
    psx_semantic_context_t *semantic_context,
    token_t *start, token_t *end) {
  if (!start || !end) return 1;
  token_t *saved_token = curtok();
  set_curtok(start);
  int value = 1;
  if (psx_ctx_is_type_token(curtok()->kind) ||
      psx_ctx_is_typedef_name_token_in(semantic_context, curtok())) {
    int elem_size = 8;
    if (curtok()->kind == TK_IDENT) {
      token_ident_t *identifier = (token_ident_t *)curtok();
      psx_ctx_find_typedef_sizeof_in(
          semantic_context, identifier->str, identifier->len,
          &elem_size);
    } else {
      psx_ctx_get_type_info(curtok()->kind, NULL, &elem_size);
    }
    value = elem_size;
    set_curtok(end);
  } else {
    long long parsed = psx_eval_parsed_enum_const_expr_in_context(
        semantic_context, start, end);
    value = parsed > 0 ? (int)parsed : 1;
  }
  if (curtok() != end) set_curtok(end);
  set_curtok(saved_token);
  return value;
}
