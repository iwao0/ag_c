#ifndef FRONTEND_FUNCTION_DEFINITION_H
#define FRONTEND_FUNCTION_DEFINITION_H

#include "../parser/function_definition_syntax.h"
#include "../parser/ast.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_registry_t psx_local_registry_t;

node_function_definition_t *psx_apply_function_definition_header_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parsed_function_definition_t *definition);

node_function_definition_t *psx_apply_function_definition_header(
    psx_parsed_function_definition_t *definition);

#endif
