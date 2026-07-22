#ifndef PARSER_DECLARATION_SYNTAX_H
#define PARSER_DECLARATION_SYNTAX_H

#include "core.h"
#include "declarator_shape.h"
#include "enum_const.h"
#include "name_classifier.h"

typedef struct psx_parsed_aggregate_body_t psx_parsed_aggregate_body_t;
typedef struct psx_parsed_function_parameters_t
    psx_parsed_function_parameters_t;
typedef struct psx_parsed_type_name_t psx_parsed_type_name_t;
typedef struct node_t node_t;
typedef struct psx_parser_runtime_context_t psx_parser_runtime_context_t;
typedef struct psx_local_declaration_callbacks_t
    psx_local_declaration_callbacks_t;

typedef struct {
  token_t *start;
  token_t *end;
  node_t *node;
  char *identifier_name;
  int identifier_name_len;
} psx_parsed_const_expr_t;

typedef enum {
  PSX_PARSED_TAG_NONE = 0,
  PSX_PARSED_TAG_REFERENCE,
  PSX_PARSED_TAG_DEFINITION,
} psx_parsed_tag_action_kind_t;

typedef struct {
  psx_parsed_tag_action_kind_t action;
  token_kind_t kind;
  char *name;
  int name_len;
  unsigned char is_anonymous;
  token_t *diagnostic_token;
  psx_parsed_aggregate_body_t *aggregate_body;
  psx_parsed_enum_body_t *enum_body;
} psx_parsed_tag_action_t;

typedef enum {
  PSX_PARSED_DECL_TYPE_NONE = 0,
  PSX_PARSED_DECL_TYPE_BUILTIN,
  PSX_PARSED_DECL_TYPE_TAG,
  PSX_PARSED_DECL_TYPEDEF_NAME,
  PSX_PARSED_DECL_TYPE_ATOMIC_TYPE_NAME,
  PSX_PARSED_DECL_TYPE_IMPLICIT_INT,
} psx_parsed_decl_type_source_t;

typedef struct {
  const psx_name_classifier_t *name_classifier;
  void (*diagnose_complex_requires_float)(void *context, token_t *token);
  void *context;
  void *expression_context;
  node_t *(*parse_assignment_expression)(void *context);
  psx_parser_runtime_context_t *runtime_context;
  int allow_implicit_int;
} psx_decl_specifier_syntax_options_t;

typedef enum {
  PSX_PARSED_ALIGNAS_EXPRESSION = 0,
  PSX_PARSED_ALIGNAS_TYPE_NAME,
} psx_parsed_alignas_kind_t;

typedef struct {
  psx_parsed_alignas_kind_t kind;
  token_t *diagnostic_token;
  node_t *expression;
  psx_parsed_type_name_t *type_name;
  unsigned scope_seq;
  unsigned declaration_seq;
} psx_parsed_alignas_t;

typedef struct {
  psx_parsed_decl_type_source_t source;
  psx_type_spec_result_t type_spec;
  token_ident_t *typedef_name;
  psx_parsed_type_name_t *atomic_type_name;
  token_t *diagnostic_token;
  psx_parsed_tag_action_t tag_action;
  psx_parsed_alignas_t alignas_specifiers[8];
  int alignas_specifier_count;
  unsigned char binding_events_recorded;
} psx_parsed_decl_specifier_t;

typedef struct {
  int declarator_op_index;
  psx_parsed_const_expr_t expression;
  int has_static;
  int is_const_qualified;
  int is_volatile_qualified;
  int is_restrict_qualified;
  int is_atomic_qualified;
} psx_parsed_array_bound_t;

typedef struct {
  int declarator_op_index;
  psx_parsed_function_parameters_t *parameters;
} psx_parsed_function_suffix_t;

typedef struct {
  token_ident_t *identifier;
  token_t *diagnostic_token;
  psx_declarator_shape_t declarator_shape;
  int has_bitfield;
  psx_parsed_const_expr_t bit_width_expression;
  psx_parsed_array_bound_t *array_bounds;
  int array_bound_count;
  int array_bound_capacity;
  psx_parsed_function_suffix_t *function_suffixes;
  int function_suffix_count;
  int function_suffix_capacity;
} psx_parsed_declarator_t;

struct psx_parsed_type_name_t {
  psx_parsed_decl_specifier_t specifier;
  psx_parsed_declarator_t declarator;
  struct psx_parsed_type_name_t *atomic_inner;
  token_t *diagnostic_token;
  token_t *end;
};

int psx_token_starts_type_name_syntax(
    token_t *token, const psx_name_classifier_t *name_classifier);

void psx_parse_decl_specifier_syntax_ex(
    psx_parsed_decl_specifier_t *specifier,
    const psx_decl_specifier_syntax_options_t *options);
int psx_try_parse_decl_specifier_syntax_ex(
    psx_parsed_decl_specifier_t *specifier,
    const psx_decl_specifier_syntax_options_t *options);
void psx_parse_declarator_syntax_tree_into_with_typedef_lookup_in_contexts(
    psx_parsed_declarator_t *declarator,
    const psx_decl_specifier_syntax_options_t *options);
int psx_try_parse_toplevel_declarator_syntax_tree_with_typedef_lookup_in_contexts(
    psx_parsed_declarator_t *declarator,
    const psx_decl_specifier_syntax_options_t *options);
psx_parsed_declarator_t psx_parse_abstract_declarator_syntax_tree_in_contexts(
    const psx_decl_specifier_syntax_options_t *options);
psx_parsed_declarator_t psx_parse_parameter_declarator_syntax_tree_in_contexts(
    const psx_decl_specifier_syntax_options_t *options);
const psx_parsed_function_suffix_t *
psx_declarator_outermost_function_suffix(
    const psx_parsed_declarator_t *declarator);
void psx_materialize_declarator_expression_syntax(
    psx_parsed_declarator_t *declarator,
    const psx_decl_specifier_syntax_options_t *options);
void ps_dispose_decl_specifier_syntax(
    psx_parsed_decl_specifier_t *specifier);
void psx_dispose_declarator_syntax(psx_parsed_declarator_t *declarator);
int psx_parse_type_name_syntax_at(
    token_t *start,
    const psx_decl_specifier_syntax_options_t *options,
    psx_parsed_type_name_t *out);
int psx_parse_runtime_type_name_syntax_at(
    token_t *start,
    const psx_decl_specifier_syntax_options_t *options,
    psx_parsed_type_name_t *out);
void psx_dispose_type_name_syntax(psx_parsed_type_name_t *type_name);

#endif
