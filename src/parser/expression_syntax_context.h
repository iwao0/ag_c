#ifndef PARSER_EXPRESSION_SYNTAX_CONTEXT_H
#define PARSER_EXPRESSION_SYNTAX_CONTEXT_H

#include "name_classifier.h"

typedef struct node_t node_t;
typedef struct psx_parsed_type_name_t psx_parsed_type_name_t;
typedef struct psx_parser_runtime_context_t psx_parser_runtime_context_t;

typedef struct {
  void *context;
  psx_parser_runtime_context_t *runtime_context;
  psx_name_classifier_t name_classifier;
  void (*capture_lookup_point)(
      void *context, unsigned *scope_seq,
      unsigned *declaration_seq);
  void (*current_function_name)(
      void *context, char **name, int *name_len);
  node_t *(*parse_initializer_list)(void *context);
  node_t *(*parse_statement_expression)(void *context);
  int (*parse_type_name)(
      void *context, token_t *start, int runtime_bounds,
      psx_parsed_type_name_t *out);
} psx_expression_syntax_context_t;

#endif
