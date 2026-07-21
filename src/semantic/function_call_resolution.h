#ifndef SEMANTIC_FUNCTION_CALL_RESOLUTION_H
#define SEMANTIC_FUNCTION_CALL_RESOLUTION_H

#include "../parser/node_fwd.h"
#include "type_identity.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;

typedef enum {
  PSX_BUILTIN_CALL_NONE = 0,
  PSX_BUILTIN_CALL_EXPECT,
} psx_builtin_call_kind_t;

psx_builtin_call_kind_t psx_function_call_builtin_kind(
    const node_function_call_t *call);
const node_t *psx_builtin_expect_value_operand(
    const node_function_call_t *call);

psx_qual_type_t psx_resolve_function_reference_qual_type(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t function_qual_type);

#endif
