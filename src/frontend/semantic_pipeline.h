#ifndef FRONTEND_SEMANTIC_PIPELINE_H
#define FRONTEND_SEMANTIC_PIPELINE_H

#include "../parser/ast.h"
#include "../hir/hir.h"
#include "../tokenizer/token.h"
#include "../compilation_session.h"

int psx_frontend_resolve_function_to_hir_in_session(
    ag_compilation_session_t *session,
    const node_t *syntax_function, const token_t *fallback_diag_tok,
    psx_hir_node_id_t *hir_root);

#endif
