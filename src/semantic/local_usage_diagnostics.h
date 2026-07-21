#ifndef SEMANTIC_LOCAL_USAGE_DIAGNOSTICS_H
#define SEMANTIC_LOCAL_USAGE_DIAGNOSTICS_H

typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;
typedef struct lvar_t lvar_t;
typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct token_t token_t;

void psx_prepare_recorded_local_usage_in(
    psx_local_registry_t *local_registry,
    lvar_t *storage_objects);
void psx_emit_recorded_local_usage_warnings_in(
    ag_diagnostic_context_t *diagnostics,
    lvar_t *storage_objects,
    const token_t *fallback_diag_tok);

#endif
