#ifndef SEMANTIC_FUNCTION_DEFINITION_RESOLUTION_H
#define SEMANTIC_FUNCTION_DEFINITION_RESOLUTION_H

#include "resolved_function.h"

typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_lowering_context_t psx_lowering_context_t;
typedef struct psx_parsed_function_definition_t
    psx_parsed_function_definition_t;
typedef struct psx_parser_runtime_context_t psx_parser_runtime_context_t;
typedef struct psx_semantic_context_t psx_semantic_context_t;

node_function_definition_t *
psx_prepare_function_definition_resolution_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    psx_lowering_context_t *lowering_context,
    const psx_parsed_function_definition_t *definition);

#endif
