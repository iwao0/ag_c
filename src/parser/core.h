#ifndef PARSER_INTERNAL_CORE_H
#define PARSER_INTERNAL_CORE_H

/* core.h は AST node 型を使わない (token_kind_t と bool のみ)。
 * Phase C1-2: ast.h ではなく token.h を直接 include する。 */
#include "ret_pointee_array.h"
#include "../tokenizer/token.h"
#include <stdbool.h>

#define PS_MAX_DECLARATOR_COUNT 1024
#define PS_MAX_INITIALIZER_ELEMENTS 4096

typedef struct {
  token_kind_t kind;
  int is_unsigned;
  int is_complex;
  int is_long_long;
  int is_plain_char;
  int is_long_double;
  int is_atomic;
  int is_thread_local;
  int is_const_qualified;
  int is_volatile_qualified;
  int is_extern;
  int is_static;
  int alignas_value;
} psx_type_spec_result_t;

typedef struct {
  void *context;
  void (*consume_alignas)(void *context, psx_type_spec_result_t *result);
  void (*diagnose_complex_requires_float)(void *context, token_t *token);
} psx_type_spec_syntax_t;

token_kind_t psx_consume_type_kind_ex(psx_type_spec_result_t *out);
token_kind_t psx_consume_type_kind_with_syntax_ex(
    psx_type_spec_result_t *out, const psx_type_spec_syntax_t *syntax);
void psx_consume_pointer_prefix(int *is_ptr);
// `*` を消費しつつ段数を返す版 (多段ポインタ typedef の段数記録用)。
int psx_consume_pointer_prefix_counted(int *is_ptr);
bool psx_is_decl_prefix_token(token_kind_t k);
bool psx_is_gnu_attribute_token(const token_t *t);
void psx_skip_gnu_attributes(void);
void psx_skip_gnu_attributes_at(token_t **t);
typedef struct {
  int is_variadic;
  int nargs_fixed;
  unsigned short param_fp_mask;
  unsigned short param_int_mask;
} psx_funcptr_signature_t;

typedef struct {
  unsigned char int_width;
  tk_float_kind_t fp_kind;
  tk_float_kind_t pointee_fp_kind;
  psx_ret_pointee_array_t pointee_array;
  int is_void;
  int is_data_pointer;
  int is_complex;
} psx_funcptr_return_shape_t;

typedef struct {
  psx_funcptr_signature_t signature;
  psx_funcptr_return_shape_t return_shape;
} psx_funcptr_callable_shape_t;

typedef struct psx_funcptr_type_shape_t psx_funcptr_type_shape_t;

typedef struct {
  int is_funcptr;
  psx_funcptr_type_shape_t *type;
} psx_funcptr_returned_func_t;

struct psx_funcptr_type_shape_t {
  psx_funcptr_callable_shape_t callable;
  psx_funcptr_returned_func_t returned_funcptr;
};

typedef struct {
  psx_funcptr_type_shape_t function;
} psx_decl_funcptr_sig_t;

int ps_decl_funcptr_sig_has_payload(psx_decl_funcptr_sig_t sig);
int psx_funcptr_return_shape_has_payload(psx_funcptr_return_shape_t ret);
int psx_funcptr_signature_has_payload(psx_funcptr_signature_t sig);
int psx_funcptr_callable_shape_has_payload(psx_funcptr_callable_shape_t fn);
int psx_funcptr_returned_func_has_payload(psx_funcptr_returned_func_t ret);
int psx_funcptr_type_shape_has_payload(psx_funcptr_type_shape_t fn);
bool psx_try_consume_pragma_pack_marker(void);

#endif
