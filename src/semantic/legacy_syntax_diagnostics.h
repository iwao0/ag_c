#ifndef SEMANTIC_LEGACY_SYNTAX_DIAGNOSTICS_H
#define SEMANTIC_LEGACY_SYNTAX_DIAGNOSTICS_H

typedef struct ag_compilation_options_t ag_compilation_options_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_lowering_context_t psx_lowering_context_t;
typedef struct psx_parsed_function_definition_t
    psx_parsed_function_definition_t;
typedef struct psx_parser_runtime_context_t psx_parser_runtime_context_t;
typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct node_t node_t;
typedef struct token_t token_t;

int psx_legacy_syntax_diagnostics_accept_function_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const psx_parsed_function_definition_t *syntax_function,
    const token_t *fallback_diag_tok);

int psx_legacy_syntax_diagnostics_accept_nonfunction_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const node_t *syntax,
    const token_t *fallback_diag_tok,
    int is_initializer);

#endif
