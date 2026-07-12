#ifndef PARSER_TAG_DECLARATION_H
#define PARSER_TAG_DECLARATION_H

#include "../semantic/tag_declaration_resolution.h"
#include "ast.h"

void psx_apply_parsed_tag_declaration(
    token_kind_t kind, char *name, int name_len,
    psx_tag_declaration_mode_t mode, int member_count,
    int size, int alignment, token_t *diag_tok);

#endif
