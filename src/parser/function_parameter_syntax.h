#ifndef PARSER_FUNCTION_PARAMETER_SYNTAX_H
#define PARSER_FUNCTION_PARAMETER_SYNTAX_H

#include "declaration_syntax.h"

typedef struct {
  psx_parsed_decl_specifier_t specifier;
  psx_parsed_declarator_t declarator;
  /* Non-owning identity of a shared old-style declaration specifier. */
  const psx_parsed_decl_specifier_t *shared_specifier;
  /* Identifier-list position used by the resolved function signature. */
  int definition_index;
} psx_parsed_function_parameter_t;

typedef struct {
  psx_parsed_decl_specifier_t specifier;
  psx_parsed_declarator_t *declarators;
  int declarator_count;
  int declarator_capacity;
  token_t *diagnostic_token;
} psx_parsed_old_style_parameter_declaration_t;

struct psx_parsed_function_parameters_t {
  psx_parsed_function_parameter_t *items;
  int count;
  int capacity;
  int is_variadic;
  int is_identifier_list;
  psx_parsed_old_style_parameter_declaration_t *old_style_declarations;
  int old_style_declaration_count;
  int old_style_declaration_capacity;
};

typedef enum {
  PSX_PARAMETER_TYPE_DEFERRED_TYPEDEF = 0,
  PSX_PARAMETER_TYPE_ALLOW_IDENTIFIER_LIST,
  PSX_PARAMETER_TYPE_C11_STRICT,
} psx_function_parameter_type_mode_t;

int psx_parse_function_parameters_syntax_with_typedef_lookup_in_contexts(
    psx_parsed_function_parameters_t *parameters,
    psx_function_parameter_type_mode_t type_mode,
    const psx_decl_specifier_syntax_options_t *options);
int psx_parse_old_style_function_parameter_declarations_syntax_in_contexts(
    psx_parsed_function_parameters_t *parameters,
    const psx_decl_specifier_syntax_options_t *options);
int psx_function_definition_parameter_syntax_at(
    const psx_parsed_function_parameters_t *parameters, int index,
    psx_parsed_function_parameter_t *parameter);
void psx_dispose_function_parameters_syntax(
    psx_parsed_function_parameters_t *parameters);

#endif
