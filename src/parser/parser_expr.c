#include "parser_expr.h"

static token_kind_t g_current_ret_token_kind = TK_INT;
static tk_float_kind_t g_current_ret_fp_kind = TK_FLOAT_KIND_NONE;

void pexpr_set_current_func_ret_type(token_kind_t ret_kind, tk_float_kind_t fp_kind) {
  g_current_ret_token_kind = ret_kind;
  g_current_ret_fp_kind = fp_kind;
}

token_kind_t pexpr_current_func_ret_token_kind(void) {
  return g_current_ret_token_kind;
}

tk_float_kind_t pexpr_current_func_ret_fp_kind(void) {
  return g_current_ret_fp_kind;
}

