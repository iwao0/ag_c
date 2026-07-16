#ifndef PARSER_EXPR_H
#define PARSER_EXPR_H

#include "ast.h"
#include "expression_syntax_context.h"
#include "local_declaration_syntax.h"
#include "name_classifier.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_parser_runtime_context_t psx_parser_runtime_context_t;

node_t *psx_expr_expr_syntax(
    const psx_expression_syntax_context_t *syntax_context);
node_t *psx_expr_assign_syntax(
    const psx_expression_syntax_context_t *syntax_context);
node_t *psx_expr_conditional_syntax(
    const psx_expression_syntax_context_t *syntax_context);

node_t *psx_expr_expr_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    const psx_name_classifier_t *name_classifier,
    const psx_local_declaration_callbacks_t *local_declarations);
node_t *psx_expr_assign_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    const psx_name_classifier_t *name_classifier,
    const psx_local_declaration_callbacks_t *local_declarations);
node_t *psx_expr_conditional_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    const psx_name_classifier_t *name_classifier,
    const psx_local_declaration_callbacks_t *local_declarations);

#endif
