#ifndef LOWERING_STATIC_DATA_INITIALIZER_H
#define LOWERING_STATIC_DATA_INITIALIZER_H

#include "../parser/ast.h"
#include "../parser/symtab.h"
#include "../semantic/static_initializer_resolution.h"

typedef struct {
  int type_completed;
  int initialized;
} psx_static_declaration_initializer_result_t;

int lower_resolved_static_initializer(
    global_var_t *global,
    const psx_static_initializer_resolution_t *resolution,
    token_t *diag_tok,
    psx_static_declaration_initializer_result_t *result);

int lower_static_object_initializer(
    global_var_t *global, psx_type_t *type,
    node_init_list_t *initializer, token_t *fallback_tok);
int lower_static_scalar_array_initializer(
    global_var_t *global, psx_type_t *type,
    node_init_list_t *initializer, token_t *fallback_tok);

#endif
