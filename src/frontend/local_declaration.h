#ifndef FRONTEND_LOCAL_DECLARATION_H
#define FRONTEND_LOCAL_DECLARATION_H

#include "../parser/local_declaration_syntax.h"

typedef struct psx_parser_runtime_context_t psx_parser_runtime_context_t;

typedef struct {
  psx_parser_runtime_context_t *runtime_context;
  psx_local_declaration_callbacks_t *syntax;
  char *current_function_name;
  int current_function_name_len;
} psx_frontend_local_declaration_syntax_adapter_t;

void psx_frontend_init_local_declaration_syntax_adapter(
    psx_frontend_local_declaration_syntax_adapter_t *adapter,
    psx_local_declaration_callbacks_t *callbacks,
    psx_parser_runtime_context_t *runtime_context,
    const psx_name_classifier_t *name_classifier,
    char *current_function_name, int current_function_name_len);
void psx_frontend_local_declaration_syntax_set_function_name(
    psx_frontend_local_declaration_syntax_adapter_t *adapter,
    char *current_function_name, int current_function_name_len);

#endif
