#ifndef PARSER_FUNCTION_PUBLIC_H
#define PARSER_FUNCTION_PUBLIC_H

#include "core.h"
#include <stdbool.h>

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

enum {
  PSX_PCAT_UNSET = 0,
  PSX_PCAT_INT4  = 1,  /* char/short/int / _Bool */
  PSX_PCAT_INT8  = 2,  /* long / long long */
  PSX_PCAT_FLOAT = 3,
  PSX_PCAT_DOUBLE = 4,
  PSX_PCAT_PTR   = 5,
  PSX_PCAT_STRUCT = 6,
  PSX_PCAT_OTHER  = 7,
};

bool psx_ctx_has_function_name(char *name, int len);
int psx_ctx_is_function_defined(char *name, int len);
psx_function_ret_info_t psx_ctx_get_function_ret_info(char *name, int len);
bool psx_ctx_get_function_is_variadic(char *name, int len, int *out_nargs_fixed);
int psx_ctx_get_function_nargs_fixed(char *name, int len);
tk_float_kind_t psx_ctx_get_function_param_fp_kind(char *name, int len, int param_idx);
int psx_ctx_get_function_param_int_size(char *name, int len, int param_idx);
int psx_ctx_get_function_param_int_unsigned(char *name, int len, int param_idx);
int psx_ctx_get_function_param_category(char *name, int len, int idx);
int psx_ctx_scalar_type_size(token_kind_t kind);

#endif
