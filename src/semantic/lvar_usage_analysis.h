#ifndef SEMANTIC_LVAR_USAGE_ANALYSIS_H
#define SEMANTIC_LVAR_USAGE_ANALYSIS_H

#include "../parser/ast.h"
#include "../parser/decl.h"
#include "../tokenizer/token.h"

typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;

void psx_collect_lvar_usage_events_in(
    psx_local_registry_t *local_registry,
    node_t *node, psx_lvar_usage_region_t *inherited_region);
void psx_analyze_function_lvar_usage_in(
    ag_diagnostic_context_t *diagnostics,
    psx_local_registry_t *local_registry,
    node_function_definition_t *function,
    const token_t *fallback_diag_tok);

#endif
