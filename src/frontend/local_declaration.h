#ifndef FRONTEND_LOCAL_DECLARATION_H
#define FRONTEND_LOCAL_DECLARATION_H

#include "../parser/local_declaration_syntax.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;

void psx_frontend_init_local_declaration_callbacks(
    psx_local_declaration_callbacks_t *callbacks,
    psx_semantic_context_t *semantic_context);

const psx_local_declaration_callbacks_t *
psx_frontend_local_declaration_callbacks(void);

#endif
