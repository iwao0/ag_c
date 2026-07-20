#ifndef SEMANTIC_PROTOTYPE_PARAMETER_H
#define SEMANTIC_PROTOTYPE_PARAMETER_H

#include "../type_system/type_ids.h"

typedef struct psx_prototype_parameter_t psx_prototype_parameter_t;
typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct token_t token_t;

const psx_prototype_parameter_t *psx_declare_prototype_parameter_in(
    psx_semantic_context_t *semantic_context,
    char *name, int name_len, psx_qual_type_t declaration_qual_type,
    token_t *diagnostic_token);
psx_qual_type_t psx_prototype_parameter_qual_type(
    const psx_prototype_parameter_t *parameter);

#endif
