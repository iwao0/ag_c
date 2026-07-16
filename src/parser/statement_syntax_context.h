#ifndef PARSER_STATEMENT_SYNTAX_CONTEXT_H
#define PARSER_STATEMENT_SYNTAX_CONTEXT_H

#include "name_classifier.h"

typedef struct node_t node_t;
typedef struct psx_lvar_usage_region_t psx_lvar_usage_region_t;
typedef struct psx_parser_runtime_context_t psx_parser_runtime_context_t;

typedef struct {
  void *context;
  psx_parser_runtime_context_t *runtime_context;
  psx_name_classifier_t name_classifier;
  node_t *(*parse_expression)(void *context);
  node_t *(*parse_local_declaration)(void *context);
  node_t *(*parse_case_expression)(void *context);
  void (*enter_block_scope)(void *context);
  void (*leave_block_scope)(void *context);
  void (*enter_local_scope)(void *context);
  void (*leave_local_scope)(void *context);
  psx_lvar_usage_region_t *(*begin_usage_region)(void *context);
  void (*end_usage_region)(
      void *context, psx_lvar_usage_region_t *region);
} psx_statement_syntax_context_t;

#endif
