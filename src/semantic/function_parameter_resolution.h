#ifndef SEMANTIC_FUNCTION_PARAMETER_RESOLUTION_H
#define SEMANTIC_FUNCTION_PARAMETER_RESOLUTION_H

#include "declaration_resolution.h"
#include "../parser/declarator_shape.h"
#include "../parser/function_parameter_syntax.h"

void psx_resolve_declarator_syntax(
    const psx_parsed_declarator_t *parsed,
    psx_declarator_shape_t *shape, int *bit_width);
void psx_set_resolved_function_parameter_types(
    psx_declarator_op_t *function_op,
    const psx_type_t *const *parameter_types,
    int parameter_count, int is_variadic);

#endif
