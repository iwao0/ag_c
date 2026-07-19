#ifndef SEMANTIC_LOCAL_INITIALIZER_BINDING_H
#define SEMANTIC_LOCAL_INITIALIZER_BINDING_H

#include "../parser/ast.h"

typedef struct arena_context_t arena_context_t;
typedef struct lvar_t lvar_t;
typedef struct psx_resolution_store_t psx_resolution_store_t;

node_t *psx_bind_local_initializer_target_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, lvar_t *var,
    node_t *initializer, psx_decl_init_kind_t initializer_kind,
    token_t *initializer_tok);

#endif
