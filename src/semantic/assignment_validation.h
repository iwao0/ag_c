#ifndef SEMANTIC_ASSIGNMENT_VALIDATION_H
#define SEMANTIC_ASSIGNMENT_VALIDATION_H

#include "../parser/node_fwd.h"

typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;
typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct token_t token_t;

void psx_validate_assignment_in_context(
    psx_semantic_context_t *semantic_context, const node_t *node,
    ag_diagnostic_context_t *diagnostics,
    const token_t *fallback_diag_tok);

#endif
