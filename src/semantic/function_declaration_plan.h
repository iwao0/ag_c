#ifndef SEMANTIC_FUNCTION_DECLARATION_PLAN_H
#define SEMANTIC_FUNCTION_DECLARATION_PLAN_H

#include "../parser/type.h"

typedef struct {
  const psx_type_t *return_type;
  psx_type_t *const *parameter_types;
  int parameter_count;
  int is_variadic;
  psx_decl_funcptr_sig_t callable_signature;
} psx_function_declaration_request_t;

typedef struct {
  psx_type_t *function_type;
} psx_function_declaration_plan_t;

int psx_plan_function_declaration(
    const psx_function_declaration_request_t *request,
    psx_function_declaration_plan_t *plan);

#endif
