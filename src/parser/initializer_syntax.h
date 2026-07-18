#ifndef PARSER_INITIALIZER_SYNTAX_H
#define PARSER_INITIALIZER_SYNTAX_H

#include "ast.h"

typedef struct psx_parser_runtime_context_t psx_parser_runtime_context_t;

typedef struct {
  void *context;
  psx_parser_runtime_context_t *runtime_context;
  node_t *(*parse_assignment_expression)(void *context);
  void (*diagnose_unsupported_gnu_extension)(
      void *context, const token_t *token, const char *name);
} psx_initializer_syntax_context_t;

typedef struct {
  int has_initializer;
  psx_decl_init_kind_t kind;
  node_t *value;
  token_t *assign_tok;
  token_t *value_tok;
} psx_parsed_initializer_t;

node_t *psx_parse_initializer_syntax_list_with_context(
    const psx_initializer_syntax_context_t *context);
void psx_prepare_optional_initializer_syntax(
    psx_parsed_initializer_t *out,
    psx_parser_runtime_context_t *runtime_context);
void psx_parse_initializer_syntax_value_with_context(
    psx_parsed_initializer_t *out, token_t *assign_tok,
    const psx_initializer_syntax_context_t *context);

#endif
