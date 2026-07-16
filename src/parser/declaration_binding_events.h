#ifndef PARSER_DECLARATION_BINDING_EVENTS_H
#define PARSER_DECLARATION_BINDING_EVENTS_H

#include "declaration_syntax.h"

void psx_record_decl_specifier_binding_events(
    const psx_parsed_decl_specifier_t *specifier,
    const psx_name_classifier_t *name_classifier);
void psx_record_declarator_binding_events(
    const psx_parsed_declarator_t *declarator,
    const psx_name_classifier_t *name_classifier);
void psx_record_function_parameter_binding_events(
    const psx_parsed_function_parameters_t *parameters,
    const psx_name_classifier_t *name_classifier);
void psx_record_function_definition_declarator_binding_events(
    const psx_parsed_declarator_t *declarator,
    const psx_name_classifier_t *name_classifier);

#endif
