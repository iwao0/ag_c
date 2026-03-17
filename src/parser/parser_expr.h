#ifndef PARSER_EXPR_H
#define PARSER_EXPR_H

#include "ast.h"

node_t *pexpr_expr(void);
node_t *pexpr_assign(void);

void pexpr_set_current_func_ret_type(token_kind_t ret_kind, tk_float_kind_t fp_kind);
token_kind_t pexpr_current_func_ret_token_kind(void);
tk_float_kind_t pexpr_current_func_ret_fp_kind(void);

#endif

