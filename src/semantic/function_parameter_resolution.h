#ifndef SEMANTIC_FUNCTION_PARAMETER_RESOLUTION_H
#define SEMANTIC_FUNCTION_PARAMETER_RESOLUTION_H

#include "declaration_resolution.h"
#include "../parser/function_parameter_syntax.h"

void psx_resolve_declarator_syntax(
    const psx_parsed_declarator_t *parsed,
    psx_declarator_shape_t *shape, int *bit_width,
    const psx_decl_syntax_resolution_context_t *context);
void psx_resolve_function_parameter_types(
    psx_parsed_function_parameters_t *parameters,
    psx_declarator_op_t *function_op, token_t *diagnostic_token,
    const psx_decl_syntax_resolution_context_t *context);
void psx_set_resolved_function_parameter_types(
    psx_declarator_op_t *function_op, psx_type_t **parameter_types,
    int parameter_count, int is_variadic);

#endif
