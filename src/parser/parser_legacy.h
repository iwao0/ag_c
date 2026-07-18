#ifndef PARSER_PARSER_LEGACY_H
#define PARSER_PARSER_LEGACY_H

#include "local_declaration_syntax.h"

typedef struct node_t node_t;
typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_parser_runtime_context_t psx_parser_runtime_context_t;
typedef struct tokenizer_context_t tokenizer_context_t;
typedef struct token_t token_t;

token_kind_t psx_consume_type_kind_in_contexts(
    psx_semantic_context_t *semantic_context,
    tokenizer_context_t *tokenizer_context,
    psx_type_spec_result_t *out);

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

node_t *ps_expr_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    const psx_local_declaration_callbacks_t *local_declarations,
    tokenizer_context_t *tk_ctx, token_t *start);

#endif
