#ifndef SEMANTIC_LVAR_USAGE_ANALYSIS_H
#define SEMANTIC_LVAR_USAGE_ANALYSIS_H

#include "../parser/ast.h"
#include "../parser/decl.h"
#include "../tokenizer/token.h"
#include "resolved_function.h"

typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;
typedef struct psx_resolution_store_t psx_resolution_store_t;

void psx_collect_lvar_usage_events_in(
    const psx_resolution_store_t *store,
    psx_local_registry_t *local_registry,
    node_t *node, psx_lvar_usage_region_t *inherited_region);
void psx_analyze_function_lvar_usage_in(
    const psx_resolution_store_t *store,
    ag_diagnostic_context_t *diagnostics,
    psx_local_registry_t *local_registry,
    node_function_definition_t *function,
    const token_t *fallback_diag_tok);
void psx_analyze_recorded_lvar_usage_in(
    ag_diagnostic_context_t *diagnostics,
    psx_local_registry_t *local_registry,
    lvar_t *storage_objects,
    const token_t *fallback_diag_tok);

#endif
