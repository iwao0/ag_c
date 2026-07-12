#ifndef PARSER_DECLARATION_SYNTAX_H
#define PARSER_DECLARATION_SYNTAX_H

#include "core.h"
#include "enum_const.h"
#include "type.h"

typedef struct psx_parsed_aggregate_body_t psx_parsed_aggregate_body_t;
typedef struct psx_parsed_function_parameters_t
    psx_parsed_function_parameters_t;

typedef struct {
  token_t *start;
  token_t *end;
} psx_parsed_const_expr_t;

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

typedef enum {
  PSX_PARSED_DECL_TYPE_NONE = 0,
  PSX_PARSED_DECL_TYPE_BUILTIN,
  PSX_PARSED_DECL_TYPE_TAG,
  PSX_PARSED_DECL_TYPEDEF_NAME,
} psx_parsed_decl_type_source_t;

typedef struct {
  psx_parsed_decl_type_source_t source;
  psx_type_spec_result_t type_spec;
  token_ident_t *typedef_name;
  token_t *diagnostic_token;
  psx_parsed_member_tag_action_t tag_action;
  psx_parsed_const_expr_t alignas_expressions[8];
  int alignas_expression_count;
} psx_parsed_decl_specifier_t;

typedef struct {
  int declarator_op_index;
  psx_parsed_const_expr_t expression;
} psx_parsed_array_bound_t;

typedef struct {
  int declarator_op_index;
  psx_parsed_function_parameters_t *parameters;
} psx_parsed_function_suffix_t;

typedef struct {
  token_ident_t *member;
  token_t *diagnostic_token;
  psx_declarator_shape_t declarator_shape;
  int pointer_levels;
  int has_bitfield;
  psx_parsed_const_expr_t bit_width_expression;
  psx_parsed_array_bound_t array_bounds[24];
  int array_bound_count;
  psx_parsed_function_suffix_t function_suffixes[24];
  int function_suffix_count;
} psx_parsed_declarator_t;

void psx_parse_decl_specifier_syntax(
    psx_parsed_decl_specifier_t *specifier);
psx_parsed_declarator_t psx_parse_declarator_syntax_tree(void);
void psx_dispose_decl_specifier_syntax(
    psx_parsed_decl_specifier_t *specifier);
void psx_dispose_declarator_syntax(psx_parsed_declarator_t *declarator);

#endif
