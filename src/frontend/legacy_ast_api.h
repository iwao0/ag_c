#ifndef FRONTEND_LEGACY_AST_API_H
#define FRONTEND_LEGACY_AST_API_H

#include "../compilation_session.h"
#include "../parser/node_fwd.h"
#include "../tokenizer/token.h"
#include "../tokenizer/tokenizer.h"

node_t **psx_frontend_legacy_program_ast_in_session(
    ag_compilation_session_t *session,
    tokenizer_context_t *tk_ctx, token_t *start);
node_t *psx_frontend_legacy_analyze_expression_ast_in_session(
    ag_compilation_session_t *session,
    const node_t *syntax_expression,
    const token_t *fallback_diag_tok);

#endif
