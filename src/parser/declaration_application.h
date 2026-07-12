#ifndef PARSER_DECLARATION_APPLICATION_H
#define PARSER_DECLARATION_APPLICATION_H

#include "function_parameter_syntax.h"

psx_type_t *psx_apply_parsed_decl_specifier(
    const psx_parsed_decl_specifier_t *specifier);
int psx_apply_parsed_decl_alignment(
    const psx_parsed_decl_specifier_t *specifier);
void psx_apply_parsed_standalone_tag(
    const psx_parsed_decl_specifier_t *specifier);
void psx_apply_parsed_declarator(
    const psx_parsed_declarator_t *declarator,
    psx_declarator_shape_t *shape, int *bit_width);
void psx_apply_parsed_function_parameters(
    psx_parsed_function_parameters_t *parameters,
    psx_declarator_op_t *function_op, token_t *diagnostic_token);

#endif
