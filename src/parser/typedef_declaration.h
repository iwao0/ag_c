#ifndef PARSER_TYPEDEF_DECLARATION_H
#define PARSER_TYPEDEF_DECLARATION_H

#include "ast.h"
#include "type.h"

void psx_apply_parsed_typedef_declaration(
    char *name, int name_len, const psx_type_t *type, token_t *diag_tok);

#endif
