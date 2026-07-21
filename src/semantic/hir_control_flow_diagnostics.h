#ifndef SEMANTIC_HIR_CONTROL_FLOW_DIAGNOSTICS_H
#define SEMANTIC_HIR_CONTROL_FLOW_DIAGNOSTICS_H

#include "../hir/hir.h"

typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;
typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct token_t token_t;

void psx_emit_hir_control_flow_warnings(
    const psx_hir_module_t *module, psx_hir_node_id_t root,
    ag_diagnostic_context_t *diagnostics,
    psx_local_registry_t *local_registry,
    const token_t *fallback_diag_tok);

#endif
