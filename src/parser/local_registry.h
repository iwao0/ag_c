#ifndef PARSER_LOCAL_REGISTRY_H
#define PARSER_LOCAL_REGISTRY_H

#include "decl.h"

unsigned psx_local_registry_current_scope_seq(void);
void psx_local_registry_add(lvar_t *var);

#endif
