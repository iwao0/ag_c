#ifndef SEMANTIC_FUNCTION_CALL_RESOLUTION_H
#define SEMANTIC_FUNCTION_CALL_RESOLUTION_H

#include "../parser/node_fwd.h"
#include "../parser/type.h"

typedef struct arena_context_t arena_context_t;
typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_resolution_store_t psx_resolution_store_t;

typedef enum {
  PSX_BUILTIN_CALL_NONE = 0,
  PSX_BUILTIN_CALL_EXPECT,
} psx_builtin_call_kind_t;

psx_builtin_call_kind_t psx_function_call_builtin_kind(
    const node_function_call_t *call);
const node_t *psx_builtin_expect_value_operand(
    const node_function_call_t *call);

int psx_function_call_prepare_resolution_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, node_function_call_t *call);
void psx_function_call_bind_direct_name(
    psx_resolution_store_t *store,
    node_function_call_t *call, char *name, int name_len);
char *psx_function_call_direct_name(
    const psx_resolution_store_t *store,
    const node_function_call_t *call);
int psx_function_call_direct_name_length(
    const psx_resolution_store_t *store,
    const node_function_call_t *call);
const psx_type_t *psx_function_call_type(
    const psx_resolution_store_t *store,
    const node_function_call_t *call);
void psx_function_call_bind_qual_type(
    psx_resolution_store_t *store,
    node_function_call_t *call,
    const psx_semantic_type_table_t *callee_type_table,
    psx_qual_type_t callee_qual_type);
psx_qual_type_t psx_function_call_qual_type(
    const psx_resolution_store_t *store,
    const node_function_call_t *call);
void psx_function_call_set_implicit_declaration(
    psx_resolution_store_t *store,
    node_function_call_t *call, int enabled);
int psx_function_call_is_implicit_declaration(
    const psx_resolution_store_t *store,
    const node_function_call_t *call);

const psx_type_t *psx_resolve_function_reference_type(
    psx_semantic_context_t *semantic_context,
    const psx_type_t *function_type);

#endif
