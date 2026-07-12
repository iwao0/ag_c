#ifndef FRONTEND_TOPLEVEL_DECLARATION_H
#define FRONTEND_TOPLEVEL_DECLARATION_H

#include "../parser/toplevel_declaration_syntax.h"

void psx_apply_toplevel_declaration(
    psx_parsed_toplevel_declaration_t *declaration);
const psx_toplevel_declaration_callbacks_t *
psx_frontend_toplevel_declaration_callbacks(void);

#endif
