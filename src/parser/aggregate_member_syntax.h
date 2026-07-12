#ifndef PARSER_AGGREGATE_MEMBER_SYNTAX_H
#define PARSER_AGGREGATE_MEMBER_SYNTAX_H

#include "../semantic/declaration_resolution.h"
#include "core.h"

typedef struct {
  psx_decl_type_request_t declaration;
  int requested_alignment;
} psx_parsed_aggregate_member_specifier_t;

typedef struct {
  token_ident_t *member;
  psx_declarator_shape_t declarator_shape;
  int pointer_levels;
  int has_bitfield;
  int bit_width;
} psx_parsed_aggregate_member_declarator_t;

void psx_parse_aggregate_member_specifier(
    psx_parsed_aggregate_member_specifier_t *specifier);
psx_parsed_aggregate_member_declarator_t
psx_parse_aggregate_member_declarator(void);

#endif
