#ifndef FRONTEND_TOPLEVEL_DECLARATION_H
#define FRONTEND_TOPLEVEL_DECLARATION_H

#include "../parser/toplevel_declaration_syntax.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_parser_runtime_context_t psx_parser_runtime_context_t;
typedef struct psx_lowering_context_t psx_lowering_context_t;
typedef struct ag_compilation_options_t ag_compilation_options_t;

void psx_apply_toplevel_declaration_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    psx_parsed_toplevel_declaration_t *declaration);
#endif
