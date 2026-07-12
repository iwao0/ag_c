#ifndef SEMANTIC_FUNCTION_PARAMETER_RESOLUTION_H
#define SEMANTIC_FUNCTION_PARAMETER_RESOLUTION_H

#include "../parser/function_parameter_syntax.h"

void psx_resolve_function_parameters_syntax(
    psx_parsed_function_parameters_t *parameters,
    psx_declarator_op_t *function_op, token_t *diagnostic_token);

#endif
