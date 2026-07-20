#ifndef SEMANTIC_FUNCTION_PARAMETER_RESOLUTION_H
#define SEMANTIC_FUNCTION_PARAMETER_RESOLUTION_H

#include "declaration_resolution.h"
#include "../parser/declarator_shape.h"
#include "../parser/function_parameter_syntax.h"

typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct arena_context_t arena_context_t;

void psx_resolve_declarator_syntax_in_context(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_parsed_declarator_t *parsed,
    psx_declarator_shape_t *shape, int *bit_width);
void psx_set_resolved_function_parameter_qual_types(
    arena_context_t *arena_context,
    psx_declarator_op_t *function_op,
    const psx_qual_type_t *parameter_qual_types,
    int parameter_count, int is_variadic, int has_prototype);

#endif
