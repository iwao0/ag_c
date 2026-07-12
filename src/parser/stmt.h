#ifndef PARSER_STMT_H
#define PARSER_STMT_H

#include "ast.h"
#include "local_declaration_syntax.h"

node_t *psx_stmt_stmt(
    const psx_local_declaration_callbacks_t *local_declarations);
node_t *psx_parse_statement_expression(void);

#endif
