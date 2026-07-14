#ifndef PARSER_STMT_H
#define PARSER_STMT_H

#include "ast.h"
#include "local_declaration_syntax.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_local_registry_t psx_local_registry_t;

node_t *psx_stmt_stmt(
    const psx_local_declaration_callbacks_t *local_declarations);
node_t *psx_stmt_stmt_in_context(
    psx_semantic_context_t *semantic_context,
    const psx_local_declaration_callbacks_t *local_declarations);
node_t *psx_stmt_stmt_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_local_registry_t *local_registry,
    const psx_local_declaration_callbacks_t *local_declarations);
node_t *psx_parse_statement_expression(void);
node_t *psx_parse_statement_expression_in_context(
    psx_semantic_context_t *semantic_context,
    const psx_local_declaration_callbacks_t *local_declarations);
node_t *psx_parse_statement_expression_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_local_registry_t *local_registry,
    const psx_local_declaration_callbacks_t *local_declarations);

#endif
