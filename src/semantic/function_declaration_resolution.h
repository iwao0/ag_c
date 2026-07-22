#ifndef SEMANTIC_FUNCTION_DECLARATION_RESOLUTION_H
#define SEMANTIC_FUNCTION_DECLARATION_RESOLUTION_H

#include "../parser/function_public.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;

typedef enum {
  PSX_FUNCTION_DECLARATION_OK = 0,
  PSX_FUNCTION_DECLARATION_INVALID,
  PSX_FUNCTION_DECLARATION_OBJECT_NAME_CONFLICT,
  PSX_FUNCTION_DECLARATION_TYPE_CONFLICT,
  PSX_FUNCTION_DECLARATION_LINKAGE_CONFLICT,
  PSX_FUNCTION_DECLARATION_DUPLICATE_DEFINITION,
} psx_function_declaration_status_t;

typedef struct {
  psx_semantic_context_t *semantic_context;
  char *name;
  int name_len;
  psx_qual_type_t function_qual_type;
  int is_definition;
  int is_static;
} psx_function_declaration_resolution_request_t;

typedef struct {
  psx_function_declaration_status_t status;
  const psx_function_symbol_t *function;
  int effective_is_static;
} psx_function_declaration_resolution_t;

void psx_resolve_function_declaration(
    const psx_function_declaration_resolution_request_t *request,
    psx_function_declaration_resolution_t *resolution);

#endif
