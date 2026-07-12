#ifndef PARSER_LOCAL_REGISTRY_H
#define PARSER_LOCAL_REGISTRY_H

#include "decl.h"

unsigned psx_local_registry_current_scope_seq(void);
void psx_local_registry_add(lvar_t *var);
lvar_t *psx_local_registry_create_storage_object(
    char *name, int name_len, int offset, int storage_size,
    int element_size, int is_array, int alignment);
void psx_local_registry_update_storage_object(
    lvar_t *var, int offset, int storage_size,
    int element_size, int is_array, int alignment);

#endif
