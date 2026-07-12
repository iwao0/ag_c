#ifndef PARSER_ENUM_CONSTANT_DECLARATION_H
#define PARSER_ENUM_CONSTANT_DECLARATION_H

#include "ast.h"

void psx_apply_parsed_enum_constant(
    char *name, int name_len, long long value, token_t *diag_tok);

#endif
