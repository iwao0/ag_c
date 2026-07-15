#ifndef SEMANTIC_FUNCTION_CALL_RESOLUTION_H
#define SEMANTIC_FUNCTION_CALL_RESOLUTION_H

#include "../parser/type.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;

typedef enum {
  PSX_FUNCTION_CALL_RESOLUTION_OK = 0,
  PSX_FUNCTION_CALL_RESOLUTION_NOT_CALLABLE,
} psx_function_call_resolution_status_t;

typedef struct {
  psx_function_call_resolution_status_t status;
  const psx_type_t *function_type;
} psx_function_call_resolution_t;

void psx_resolve_function_call_type(
    const psx_type_t *bound_function_type,
    const psx_type_t *callee_type, int is_implicit_declaration,
    psx_function_call_resolution_t *resolution);
const psx_type_t *psx_resolve_function_reference_type(
    psx_semantic_context_t *semantic_context,
    const psx_type_t *function_type);

#endif
