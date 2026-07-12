#ifndef LOWERING_STATIC_DATA_INITIALIZER_H
#define LOWERING_STATIC_DATA_INITIALIZER_H

#include "../parser/ast.h"
#include "../parser/symtab.h"

typedef struct {
  global_var_t *global;
  psx_type_t *type;
  psx_decl_init_kind_t initializer_kind;
  node_t *initializer;
  token_t *diag_tok;
} psx_static_declaration_initializer_request_t;

typedef struct {
  int type_completed;
  int initialized;
} psx_static_declaration_initializer_result_t;

int lower_static_declaration_initializer(
    const psx_static_declaration_initializer_request_t *request,
    psx_static_declaration_initializer_result_t *result);

int lower_static_object_initializer(
    global_var_t *global, psx_type_t *type,
    node_init_list_t *initializer, token_t *fallback_tok);
int lower_static_scalar_array_initializer(
    global_var_t *global, psx_type_t *type,
    node_init_list_t *initializer, token_t *fallback_tok);

#endif
