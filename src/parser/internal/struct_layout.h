#ifndef PARSER_STRUCT_LAYOUT_H
#define PARSER_STRUCT_LAYOUT_H

#include "../../tokenizer/token.h"

typedef struct {
  token_ident_t *member;
  int is_ptr;
  int has_func_suffix;
  int paren_array_mul;
} member_decl_head_t;

typedef struct {
  int (*parse_alignas_value)(void);
  void (*make_anonymous_tag_name)(char **out_name, int *out_len);
  int (*parse_tag_definition_body)(token_kind_t tag_kind, char *tag_name, int tag_len, int *out_size);
  member_decl_head_t (*parse_member_decl_head)(void);
  long long (*parse_enum_const_expr)(void);
  int (*parse_member_array_suffixes)(int *out_is_flex_array);
} struct_member_layout_ops_t;

int psx_parse_struct_or_union_members_layout(token_kind_t tag_kind, char *tag_name, int tag_len,
                                             int *out_size,
                                             const struct_member_layout_ops_t *ops);

#endif
