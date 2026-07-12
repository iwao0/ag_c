#ifndef PARSER_AGGREGATE_MEMBER_SYNTAX_H
#define PARSER_AGGREGATE_MEMBER_SYNTAX_H

#include "../semantic/declaration_resolution.h"
#include "core.h"
#include "enum_const.h"
#include "static_assert_declaration.h"

typedef struct psx_parsed_aggregate_body_t psx_parsed_aggregate_body_t;

typedef struct {
  token_t *start;
  token_t *end;
} psx_parsed_aggregate_const_expr_t;

typedef enum {
  PSX_PARSED_MEMBER_TAG_NONE = 0,
  PSX_PARSED_MEMBER_TAG_REFERENCE,
  PSX_PARSED_MEMBER_TAG_DEFINITION,
} psx_parsed_member_tag_action_kind_t;

typedef struct {
  psx_parsed_member_tag_action_kind_t action;
  token_kind_t kind;
  char *name;
  int name_len;
  token_t *diagnostic_token;
  psx_parsed_aggregate_body_t *aggregate_body;
  psx_parsed_enum_body_t *enum_body;
} psx_parsed_member_tag_action_t;

typedef struct {
  psx_decl_type_request_t declaration;
  psx_parsed_member_tag_action_t tag_action;
  psx_parsed_aggregate_const_expr_t alignas_expressions[8];
  int alignas_expression_count;
} psx_parsed_aggregate_member_specifier_t;

typedef struct {
  int declarator_op_index;
  psx_parsed_aggregate_const_expr_t expression;
} psx_parsed_aggregate_array_bound_t;

typedef struct {
  token_ident_t *member;
  token_t *diagnostic_token;
  psx_declarator_shape_t declarator_shape;
  int pointer_levels;
  int has_bitfield;
  psx_parsed_aggregate_const_expr_t bit_width_expression;
  psx_parsed_aggregate_array_bound_t array_bounds[24];
  int array_bound_count;
} psx_parsed_aggregate_member_declarator_t;

typedef struct {
  psx_parsed_aggregate_member_specifier_t specifier;
  psx_parsed_aggregate_member_declarator_t *declarators;
  int declarator_count;
  int declarator_capacity;
  int pack_alignment;
} psx_parsed_aggregate_member_declaration_t;

typedef enum {
  PSX_PARSED_AGGREGATE_MEMBER_DECLARATION = 0,
  PSX_PARSED_AGGREGATE_STATIC_ASSERT,
} psx_parsed_aggregate_item_kind_t;

typedef struct {
  psx_parsed_aggregate_item_kind_t kind;
  union {
    psx_parsed_aggregate_member_declaration_t member_declaration;
    psx_parsed_static_assert_declaration_t static_assertion;
  } value;
} psx_parsed_aggregate_item_t;

struct psx_parsed_aggregate_body_t {
  psx_parsed_aggregate_item_t *items;
  int item_count;
  int item_capacity;
};

void psx_parse_aggregate_member_specifier(
    psx_parsed_aggregate_member_specifier_t *specifier);
psx_parsed_aggregate_member_declarator_t
psx_parse_aggregate_member_declarator(void);
void psx_parse_aggregate_body(psx_parsed_aggregate_body_t *body);
void psx_dispose_parsed_aggregate_body(psx_parsed_aggregate_body_t *body);

#endif
