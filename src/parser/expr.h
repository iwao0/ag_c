#ifndef PARSER_EXPR_H
#define PARSER_EXPR_H

#include "ast.h"
#include "expression_syntax_context.h"
#include "local_declaration_syntax.h"
#include "name_classifier.h"

typedef struct psx_parser_runtime_context_t psx_parser_runtime_context_t;

node_t *psx_expr_expr_syntax(
    const psx_expression_syntax_context_t *syntax_context);
node_t *psx_expr_assign_syntax(
    const psx_expression_syntax_context_t *syntax_context);
node_t *psx_expr_conditional_syntax(
    const psx_expression_syntax_context_t *syntax_context);

node_t *psx_expr_expr_with_syntax_services(
    psx_parser_runtime_context_t *runtime_context,
    const psx_name_classifier_t *name_classifier,
    const psx_local_declaration_callbacks_t *local_declarations,
    char *current_function_name, int current_function_name_len);
node_t *psx_expr_assign_with_syntax_services(
    psx_parser_runtime_context_t *runtime_context,
    const psx_name_classifier_t *name_classifier,
    const psx_local_declaration_callbacks_t *local_declarations,
    char *current_function_name, int current_function_name_len);
node_t *psx_expr_conditional_with_syntax_services(
    psx_parser_runtime_context_t *runtime_context,
    const psx_name_classifier_t *name_classifier,
    const psx_local_declaration_callbacks_t *local_declarations,
    char *current_function_name, int current_function_name_len);
#endif
