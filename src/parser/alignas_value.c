#include "internal/alignas_value.h"
#include "internal/enum_const.h"
#include "internal/semantic_ctx.h"
#include "../tokenizer/tokenizer.h"

static inline token_t *curtok(void) { return tk_get_current_token(); }
static inline void set_curtok(token_t *tok) { tk_set_current_token(tok); }

int psx_parse_alignas_value(void) {
  tk_expect('(');
  int val = 1;
  if (psx_ctx_is_type_token(curtok()->kind) || psx_ctx_is_typedef_name_token(curtok())) {
    int elem_size = 8;
    psx_ctx_get_type_info(curtok()->kind, NULL, &elem_size);
    val = elem_size;
    while (curtok()->kind != TK_RPAREN && curtok()->kind != TK_EOF) set_curtok(curtok()->next);
  } else {
    long long v = psx_parse_enum_const_expr();
    val = (v > 0) ? (int)v : 1;
  }
  tk_expect(')');
  return val;
}
