#ifndef PARSER_AGGREGATE_MEMBER_SYNTAX_H
#define PARSER_AGGREGATE_MEMBER_SYNTAX_H

#include "function_parameter_syntax.h"
#include "static_assert_declaration.h"

typedef struct {
  psx_parsed_decl_specifier_t specifier;
  psx_parsed_declarator_t *declarators;
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

void ps_parse_aggregate_body(psx_parsed_aggregate_body_t *body);
void ps_dispose_parsed_aggregate_body(psx_parsed_aggregate_body_t *body);

#endif
