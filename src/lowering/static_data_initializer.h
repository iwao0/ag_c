#ifndef LOWERING_STATIC_DATA_INITIALIZER_H
#define LOWERING_STATIC_DATA_INITIALIZER_H

#include "../parser/ast.h"
#include "../parser/symtab.h"

int lower_static_object_initializer(
    global_var_t *global, psx_type_t *type,
    node_init_list_t *initializer, token_t *fallback_tok);
int lower_static_scalar_array_initializer(
    global_var_t *global, psx_type_t *type,
    node_init_list_t *initializer, token_t *fallback_tok);

#endif
