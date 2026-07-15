#ifndef PARSER_DECLARATION_SYNTAX_H
#define PARSER_DECLARATION_SYNTAX_H

#include "core.h"
#include "declarator_shape.h"
#include "enum_const.h"

typedef struct psx_parsed_aggregate_body_t psx_parsed_aggregate_body_t;
typedef struct psx_parsed_function_parameters_t
    psx_parsed_function_parameters_t;
typedef struct node_t node_t;
typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_parser_runtime_context_t psx_parser_runtime_context_t;
typedef struct psx_local_declaration_callbacks_t
    psx_local_declaration_callbacks_t;

typedef struct {
  token_t *start;
  token_t *end;
  node_t *node;
  long long constant_value;
  int has_constant_value;
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
  token_t *diagnostic_token;
  psx_parsed_aggregate_body_t *aggregate_body;
  psx_parsed_enum_body_t *enum_body;
} psx_parsed_tag_action_t;

typedef enum {
  PSX_PARSED_DECL_TYPE_NONE = 0,
  PSX_PARSED_DECL_TYPE_BUILTIN,
  PSX_PARSED_DECL_TYPE_TAG,
  PSX_PARSED_DECL_TYPEDEF_NAME,
  PSX_PARSED_DECL_TYPE_IMPLICIT_INT,
} psx_parsed_decl_type_source_t;

typedef int (*psx_decl_typedef_name_predicate_t)(
    token_t *token, void *context);

typedef struct {
  psx_decl_typedef_name_predicate_t is_typedef_name;
  void (*diagnose_complex_requires_float)(void *context, token_t *token);
  void *context;
  psx_semantic_context_t *semantic_context;
  psx_global_registry_t *global_registry;
  psx_local_registry_t *local_registry;
  psx_parser_runtime_context_t *runtime_context;
  int allow_implicit_int;
} psx_decl_specifier_syntax_options_t;

typedef struct {
  psx_parsed_decl_type_source_t source;
  psx_type_spec_result_t type_spec;
  token_ident_t *typedef_name;
  token_t *diagnostic_token;
  psx_parsed_tag_action_t tag_action;
  psx_parsed_const_expr_t alignas_expressions[8];
  int alignas_expression_count;
} psx_parsed_decl_specifier_t;

typedef struct {
  int declarator_op_index;
  psx_parsed_const_expr_t expression;
  int has_static;
  int is_const_qualified;
  int is_volatile_qualified;
  int is_restrict_qualified;
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

typedef struct psx_parsed_type_name_t {
  psx_parsed_decl_specifier_t specifier;
  psx_parsed_declarator_t declarator;
  struct psx_parsed_type_name_t *atomic_inner;
  token_t *diagnostic_token;
  token_t *end;
} psx_parsed_type_name_t;

void psx_parse_decl_specifier_syntax_ex(
    psx_parsed_decl_specifier_t *specifier,
    const psx_decl_specifier_syntax_options_t *options);
int psx_try_parse_decl_specifier_syntax_ex(
    psx_parsed_decl_specifier_t *specifier,
    const psx_decl_specifier_syntax_options_t *options);
void psx_parse_declarator_syntax_tree_into_with_typedef_lookup_in_contexts(
    psx_parsed_declarator_t *declarator,
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    psx_decl_typedef_name_predicate_t is_typedef_name,
    void *typedef_name_context);
int psx_try_parse_toplevel_declarator_syntax_tree_with_typedef_lookup_in_contexts(
    psx_parsed_declarator_t *declarator,
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    psx_decl_typedef_name_predicate_t is_typedef_name,
    void *typedef_name_context);
psx_parsed_declarator_t psx_parse_abstract_declarator_syntax_tree_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context);
psx_parsed_declarator_t psx_parse_parameter_declarator_syntax_tree_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    psx_decl_typedef_name_predicate_t is_typedef_name, void *context);
void ps_parse_runtime_declarator_expressions_in_contexts(
    psx_parsed_declarator_t *declarator,
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    const psx_local_declaration_callbacks_t *local_declarations);
void ps_prepare_constant_declarator_expressions_in_context(
    psx_parsed_declarator_t *declarator,
    psx_semantic_context_t *semantic_context);
void ps_prepare_decl_specifier_alignments_in_context(
    psx_parsed_decl_specifier_t *specifier,
    psx_semantic_context_t *semantic_context);
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
