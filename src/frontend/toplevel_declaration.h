#ifndef FRONTEND_TOPLEVEL_DECLARATION_H
#define FRONTEND_TOPLEVEL_DECLARATION_H

#include "../parser/toplevel_declaration_syntax.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;

void psx_apply_toplevel_declaration(
    psx_parsed_toplevel_declaration_t *declaration);
void psx_apply_toplevel_declaration_in_context(
    psx_semantic_context_t *semantic_context,
    psx_parsed_toplevel_declaration_t *declaration);
void psx_frontend_init_toplevel_declaration_callbacks(
    psx_toplevel_declaration_callbacks_t *callbacks,
    psx_semantic_context_t *semantic_context);
const psx_toplevel_declaration_callbacks_t *
psx_frontend_toplevel_declaration_callbacks(void);

#endif
