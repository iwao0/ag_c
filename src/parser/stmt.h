#ifndef PARSER_STMT_H
#define PARSER_STMT_H

#include "statement_syntax_context.h"

node_t *psx_stmt_stmt_syntax(
    const psx_statement_syntax_context_t *syntax_context);
node_t *psx_parse_statement_expression_syntax(
    const psx_statement_syntax_context_t *syntax_context);

#endif
