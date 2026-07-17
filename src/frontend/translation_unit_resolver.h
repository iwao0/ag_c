#ifndef FRONTEND_TRANSLATION_UNIT_RESOLVER_H
#define FRONTEND_TRANSLATION_UNIT_RESOLVER_H

#include "translation_unit.h"

typedef int (*psx_frontend_function_resolver_t)(
    void *context,
    ag_compilation_session_t *session,
    const psx_parsed_function_definition_t *syntax_function,
    const token_t *fallback_diag_tok,
    psx_hir_node_id_t *hir_root);

int psx_frontend_next_function_with_resolver(
    psx_frontend_stream_t *stream,
    psx_frontend_function_t *function,
    psx_frontend_function_resolver_t resolver,
    void *resolver_context);

#endif
