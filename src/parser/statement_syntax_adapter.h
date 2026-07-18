#ifndef PARSER_STATEMENT_SYNTAX_ADAPTER_H
#define PARSER_STATEMENT_SYNTAX_ADAPTER_H

#include "local_declaration_syntax.h"
#include "name_classifier.h"
#include "statement_syntax_context.h"

typedef struct psx_parser_runtime_context_t psx_parser_runtime_context_t;
typedef struct {
  psx_parser_runtime_context_t *runtime_context;
  const psx_local_declaration_callbacks_t *local_declarations;
  psx_name_classifier_t name_classifier;
  char *current_function_name;
  int current_function_name_len;
} psx_statement_syntax_adapter_t;

int psx_statement_syntax_adapter_init(
    psx_statement_syntax_adapter_t *adapter,
    psx_parser_runtime_context_t *runtime_context,
    const psx_name_classifier_t *name_classifier,
    const psx_local_declaration_callbacks_t *local_declarations,
    char *current_function_name, int current_function_name_len);
psx_statement_syntax_context_t psx_statement_syntax_adapter_context(
    psx_statement_syntax_adapter_t *adapter);

#endif
