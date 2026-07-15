#ifndef FRONTEND_TOPLEVEL_DECLARATION_H
#define FRONTEND_TOPLEVEL_DECLARATION_H

#include "../parser/toplevel_declaration_syntax.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_registry_t psx_local_registry_t;

void psx_apply_toplevel_declaration_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parsed_toplevel_declaration_t *declaration);
void psx_frontend_init_toplevel_declaration_callbacks_in_contexts(
    psx_toplevel_declaration_callbacks_t *callbacks,
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry);
#endif
