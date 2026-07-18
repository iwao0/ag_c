#ifndef PARSER_ENUM_CONST_H
#define PARSER_ENUM_CONST_H

#include "name_classifier.h"
#include "../tokenizer/token.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct tokenizer_context_t tokenizer_context_t;
typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;
typedef struct node_t node_t;

typedef struct {
  token_ident_t *enumerator;
  node_t *initializer;
} psx_parsed_enum_member_t;

typedef struct {
  psx_parsed_enum_member_t *members;
  int member_count;
  int member_capacity;
} psx_parsed_enum_body_t;

typedef struct {
  ag_diagnostic_context_t *diagnostics;
  void *expression_context;
  node_t *(*parse_assignment_expression)(void *context);
  tokenizer_context_t *tokenizer_context;
  const psx_name_classifier_t *name_classifier;
} psx_enum_body_syntax_context_t;

long long psx_parse_enum_const_expr_in_contexts(
    psx_semantic_context_t *semantic_context,
    const psx_name_classifier_t *name_classifier,
    tokenizer_context_t *tokenizer_context);
long long psx_parse_case_const_expr_in_contexts(
    psx_semantic_context_t *semantic_context,
    const psx_name_classifier_t *name_classifier,
    tokenizer_context_t *tokenizer_context);
long long psx_eval_parsed_enum_const_expr_in_context(
    psx_semantic_context_t *semantic_context,
    const psx_name_classifier_t *name_classifier,
    token_t *start, token_t *end);
void psx_parse_enum_body_syntax(
    psx_parsed_enum_body_t *body,
    const psx_enum_body_syntax_context_t *context);
void psx_dispose_parsed_enum_body(psx_parsed_enum_body_t *body);

#endif
