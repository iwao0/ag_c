#ifndef FRONTEND_SEMANTIC_PIPELINE_H
#define FRONTEND_SEMANTIC_PIPELINE_H

#include "../parser/ast.h"
#include "../tokenizer/token.h"
#include "../compilation_session.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_registry_t psx_local_registry_t;

void psx_frontend_analyze_function_in_session(
    ag_compilation_session_t *session,
    node_t *function, const token_t *fallback_diag_tok);
node_t *psx_frontend_analyze_expression_in_session(
    ag_compilation_session_t *session,
    node_t *expression, const token_t *fallback_diag_tok);
node_t *psx_frontend_analyze_expression_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const ag_compilation_options_t *options,
    node_t *expression, const token_t *fallback_diag_tok);
node_t *psx_frontend_analyze_initializer_syntax_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const ag_compilation_options_t *options,
    node_t *syntax, const token_t *fallback_diag_tok);
void psx_frontend_analyze_program_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const ag_compilation_options_t *options,
    node_t **program);
void psx_frontend_analyze_program_in_session(
    ag_compilation_session_t *session, node_t **program);

#endif
