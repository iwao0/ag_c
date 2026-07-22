#ifndef SEMANTIC_FUNCTION_CALL_RESOLUTION_H
#define SEMANTIC_FUNCTION_CALL_RESOLUTION_H

#include "../parser/node_fwd.h"
#include "type_identity.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;

typedef enum {
  PSX_BUILTIN_CALL_NONE = 0,
  PSX_BUILTIN_CALL_EXPECT,
  PSX_BUILTIN_CALL_ATOMIC_LOAD,
  PSX_BUILTIN_CALL_ATOMIC_STORE,
  PSX_BUILTIN_CALL_ATOMIC_EXCHANGE,
  PSX_BUILTIN_CALL_ATOMIC_COMPARE_EXCHANGE,
  PSX_BUILTIN_CALL_ATOMIC_FETCH_ADD,
  PSX_BUILTIN_CALL_ATOMIC_FETCH_SUB,
  PSX_BUILTIN_CALL_ATOMIC_FETCH_OR,
  PSX_BUILTIN_CALL_ATOMIC_FETCH_XOR,
  PSX_BUILTIN_CALL_ATOMIC_FETCH_AND,
  PSX_BUILTIN_CALL_ATOMIC_FENCE,
} psx_builtin_call_kind_t;

psx_builtin_call_kind_t psx_function_call_builtin_kind(
    const node_function_call_t *call);
int psx_builtin_call_is_atomic(psx_builtin_call_kind_t kind);
int psx_resolve_atomic_builtin_call(
    psx_semantic_context_t *semantic_context,
    psx_builtin_call_kind_t kind,
    const psx_qual_type_t *argument_types,
    const unsigned char *argument_is_null_pointer_constant,
    int argument_count, psx_qual_type_t *result_type);
const node_t *psx_builtin_expect_value_operand(
    const node_function_call_t *call);

psx_qual_type_t psx_resolve_function_reference_qual_type(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t function_qual_type);

#endif
