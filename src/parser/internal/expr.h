#ifndef PARSER_EXPR_H
#define PARSER_EXPR_H

#include "../ast.h"

node_t *psx_expr_expr(void);
node_t *psx_expr_assign(void);

void psx_expr_set_current_func_ret_type(token_kind_t ret_kind, tk_float_kind_t fp_kind);
token_kind_t psx_expr_current_func_ret_token_kind(void);
tk_float_kind_t psx_expr_current_func_ret_fp_kind(void);
void psx_expr_set_current_func_ret_struct_size(int size);
int psx_expr_current_func_ret_struct_size(void);
void psx_expr_set_current_funcname(char *name, int len);

#endif
