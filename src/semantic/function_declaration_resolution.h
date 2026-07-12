#ifndef SEMANTIC_FUNCTION_DECLARATION_RESOLUTION_H
#define SEMANTIC_FUNCTION_DECLARATION_RESOLUTION_H

#include "function_declaration_plan.h"

typedef enum {
  PSX_FUNCTION_DECLARATION_OK = 0,
  PSX_FUNCTION_DECLARATION_INVALID,
  PSX_FUNCTION_DECLARATION_OBJECT_NAME_CONFLICT,
  PSX_FUNCTION_DECLARATION_TYPE_CONFLICT,
  PSX_FUNCTION_DECLARATION_DUPLICATE_DEFINITION,
} psx_function_declaration_status_t;

typedef struct {
  char *name;
  int name_len;
  const psx_type_t *return_type;
  psx_type_t *const *parameter_types;
  int parameter_count;
  int is_variadic;
  int is_definition;
} psx_function_declaration_resolution_request_t;

typedef struct {
  psx_function_declaration_status_t status;
  psx_function_declaration_plan_t declaration_plan;
} psx_function_declaration_resolution_t;

void psx_resolve_function_declaration(
    const psx_function_declaration_resolution_request_t *request,
    psx_function_declaration_resolution_t *resolution);

#endif
