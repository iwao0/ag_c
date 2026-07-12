#ifndef PARSER_STRUCT_LAYOUT_H
#define PARSER_STRUCT_LAYOUT_H

#include "aggregate_member_syntax.h"
#include "core.h"

int ps_apply_parsed_aggregate_body_layout(
    const psx_parsed_aggregate_body_t *body,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    int *out_size, int *out_align);

int psx_parse_struct_or_union_members_layout(token_kind_t tag_kind, char *tag_name, int tag_len,
                                             int *out_size, int *out_align);

int psx_parse_tag_definition_body(token_kind_t tag_kind, char *tag_name, int tag_len,
                                  int *out_size, int *out_align);

#endif
