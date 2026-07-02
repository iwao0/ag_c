#ifndef PARSER_EXPR_H
#define PARSER_EXPR_H

#include "ast.h"

node_t *psx_expr_expr(void);
node_t *psx_expr_assign(void);

void psx_expr_set_current_func_ret_type(token_kind_t ret_kind, tk_float_kind_t fp_kind);
token_kind_t psx_expr_current_func_ret_token_kind(void);
tk_float_kind_t psx_expr_current_func_ret_fp_kind(void);
void psx_expr_set_current_func_ret_struct_size(int size);
int psx_expr_current_func_ret_struct_size(void);
void psx_expr_set_current_func_ret_is_pointer(int is_pointer);
int psx_expr_current_func_ret_is_pointer(void);
void psx_expr_set_current_func_ret_is_unsigned(int is_unsigned);
int psx_expr_current_func_ret_is_unsigned(void);
void psx_expr_set_current_funcname(char *name, int len);
void psx_expr_reset_translation_unit_state(void);
/* g_current_funcname を読む。`static int n` を `<funcname>__n` に
 * mangle するために必要。 */
void psx_expr_get_current_funcname(char **out_name, int *out_len);

#endif
