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

void psx_parse_function_parameters_syntax(
    psx_parsed_function_parameters_t *parameters);
void psx_parse_function_parameters_syntax_ex(
    psx_parsed_function_parameters_t *parameters,
    int allow_implicit_int);
void psx_dispose_function_parameters_syntax(
    psx_parsed_function_parameters_t *parameters);

#endif
