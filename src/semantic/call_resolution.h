#ifndef SEMANTIC_CALL_RESOLUTION_H
#define SEMANTIC_CALL_RESOLUTION_H

#include "../parser/semantic_ctx.h"

typedef enum {
  PSX_CALL_TYPES_OK = 0,
  PSX_CALL_TYPES_NOT_CALLABLE,
  PSX_CALL_TYPES_ARGUMENT_COUNT_MISMATCH,
} psx_call_types_status_t;

typedef struct {
  psx_call_types_status_t status;
  psx_qual_type_t function_qual_type;
  psx_qual_type_t return_qual_type;
  int parameter_count;
  int is_variadic;
} psx_call_types_resolution_t;

typedef enum {
  PSX_CALL_ARGUMENT_TYPES_OK = 0,
  PSX_CALL_ARGUMENT_TYPES_INVALID,
  PSX_CALL_ARGUMENT_TYPES_INCOMPATIBLE,
  PSX_CALL_ARGUMENT_TYPES_DISCARDS_QUALIFIERS,
} psx_call_argument_types_status_t;

void psx_resolve_call_qual_types_in(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t callee_qual_type,
    int argument_count,
    psx_call_types_resolution_t *resolution);

void psx_resolve_call_argument_qual_types_in(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t function_qual_type,
    int argument_index,
    psx_qual_type_t argument_qual_type,
    int argument_is_null_pointer_constant,
    psx_call_argument_types_status_t *status);

#endif
