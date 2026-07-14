#ifndef PARSER_GLOBAL_REGISTRY_H
#define PARSER_GLOBAL_REGISTRY_H

#include "gvar_public.h"

typedef struct psx_type_t psx_type_t;

void ps_global_registry_reset_translation_unit(void);
void ps_global_registry_reset_diag_state(void);
int ps_global_registry_bind_decl_type(
    global_var_t *global, const psx_type_t *type);
int ps_global_registry_complete_array_type(
    global_var_t *global, const psx_type_t *complete_type);

#endif
