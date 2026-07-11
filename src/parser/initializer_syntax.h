#ifndef PARSER_INITIALIZER_SYNTAX_H
#define PARSER_INITIALIZER_SYNTAX_H

#include "ast.h"

int psx_initializer_syntax_is_scalar_array_list(token_t *brace);
int psx_initializer_syntax_is_simple_member_list(token_t *brace);
int psx_initializer_syntax_is_flat_mixed_list(token_t *brace);
int psx_initializer_syntax_is_braced_subobject_array_list(token_t *brace);
node_t *psx_parse_initializer_syntax_list(void);
long long psx_initializer_syntax_count_brace_elements(token_t *brace);
long long psx_initializer_syntax_infer_array_count(
    token_t *assign_tok, int element_size);
int psx_initializer_syntax_first_element_is_brace(token_t *assign_tok);
int psx_initializer_syntax_has_top_level_index_designator(
    token_t *assign_tok);

#endif
