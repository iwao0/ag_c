#ifndef PSX_TYPE_NAME_RESOLUTION_H
#define PSX_TYPE_NAME_RESOLUTION_H

#include "../parser/ast.h"

int psx_bind_type_name_ref(psx_type_name_ref_t *type_name);
const psx_type_t *psx_resolve_bound_type_name_ref(
    psx_type_name_ref_t *type_name);

#endif
