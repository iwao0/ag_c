#ifndef FRONTEND_TRANSLATION_UNIT_LEGACY_AST_H
#define FRONTEND_TRANSLATION_UNIT_LEGACY_AST_H

#include "translation_unit.h"

/* Transitional access for the legacy AST IR builder. */
node_t *psx_frontend_legacy_ast_function(
    const psx_frontend_stream_t *stream);

#endif
