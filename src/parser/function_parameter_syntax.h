#ifndef PARSER_FUNCTION_PARAMETER_SYNTAX_H
#define PARSER_FUNCTION_PARAMETER_SYNTAX_H

#include "declaration_syntax.h"

typedef struct {
  psx_parsed_decl_specifier_t specifier;
  psx_parsed_declarator_t declarator;
} psx_parsed_function_parameter_t;

struct psx_parsed_function_parameters_t {
  psx_parsed_function_parameter_t *items;
  int count;
  int capacity;
  int is_variadic;
};

typedef enum {
  PSX_PARAMETER_TYPE_DEFERRED_TYPEDEF = 0,
  PSX_PARAMETER_TYPE_ALLOW_IMPLICIT_INT,
  PSX_PARAMETER_TYPE_C11_STRICT,
} psx_function_parameter_type_mode_t;

int psx_parse_function_parameters_syntax_with_typedef_lookup_in_contexts(
    psx_parsed_function_parameters_t *parameters,
    psx_function_parameter_type_mode_t type_mode,
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    const psx_name_classifier_t *name_classifier);
void psx_dispose_function_parameters_syntax(
    psx_parsed_function_parameters_t *parameters);

#endif
