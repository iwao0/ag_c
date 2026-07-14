#ifndef FRONTEND_FUNCTION_DEFINITION_H
#define FRONTEND_FUNCTION_DEFINITION_H

#include "../parser/function_definition_syntax.h"
#include "../parser/ast.h"

node_function_definition_t *psx_apply_function_definition_header(
    psx_parsed_function_definition_t *definition);

#endif
