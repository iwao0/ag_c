#ifndef PARSER_STRUCT_LAYOUT_H
#define PARSER_STRUCT_LAYOUT_H

#include "../../tokenizer/token.h"

typedef struct {
  token_ident_t *member;
  int is_ptr;
  int has_func_suffix;
  int paren_array_mul;
} member_decl_head_t;

typedef member_decl_head_t (*parse_member_decl_head_fn)(void);

int psx_parse_struct_or_union_members_layout(token_kind_t tag_kind, char *tag_name, int tag_len,
                                             int *out_size,
                                             parse_member_decl_head_fn parse_head);

int psx_parse_tag_definition_body(token_kind_t tag_kind, char *tag_name, int tag_len,
                                  int *out_size, parse_member_decl_head_fn parse_head);

#endif
