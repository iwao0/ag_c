#ifndef PARSER_FUNCTION_PUBLIC_H
#define PARSER_FUNCTION_PUBLIC_H

#include "core.h"
#include "type.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct {
  token_kind_t token_kind;
  tk_float_kind_t fp_kind;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int struct_size;
  int is_pointer;
  int is_unsigned;
  int is_void;
  int is_complex;
  int is_funcptr;
  psx_decl_funcptr_sig_t funcptr_sig;
  int pointer_levels;
  int pointee_const_qualified;
  int pointee_volatile_qualified;
  psx_ret_pointee_array_t pointee_array;
} psx_function_ret_info_t;

bool ps_ctx_has_function_name(char *name, int len);
int ps_ctx_is_function_defined(char *name, int len);
psx_function_ret_info_t ps_ctx_get_function_ret_info(char *name, int len);
bool ps_ctx_get_function_is_variadic(char *name, int len, int *out_nargs_fixed);
int ps_ctx_get_function_nargs_fixed(char *name, int len);
/* Returns the canonical C signature length, or -1 when the function is unknown.
 * A zero-sized output queries the required length; for example void(void) is v(). */
int ps_ctx_format_function_signature(char *name, int len,
                                     char *out, size_t out_size);
const psx_type_t *ps_ctx_get_function_param_type(char *name, int len,
                                                 int param_idx);
int ps_ctx_scalar_type_size(token_kind_t kind);

#endif
