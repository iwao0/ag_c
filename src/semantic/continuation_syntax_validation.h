#ifndef SEMANTIC_CONTINUATION_SYNTAX_VALIDATION_H
#define SEMANTIC_CONTINUATION_SYNTAX_VALIDATION_H

#include "../continuation_options.h"

typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_parsed_function_definition_t
    psx_parsed_function_definition_t;
typedef struct psx_semantic_context_t psx_semantic_context_t;

int psx_validate_continuation_condition_types_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const ag_continuation_options_t *continuation,
    const psx_parsed_function_definition_t *function);

#endif
