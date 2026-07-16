#ifndef SEMANTIC_FUNCTION_CALL_RESOLUTION_H
#define SEMANTIC_FUNCTION_CALL_RESOLUTION_H

#include "../parser/node_fwd.h"
#include "../parser/type.h"

typedef struct arena_context_t arena_context_t;
typedef struct psx_semantic_context_t psx_semantic_context_t;

typedef enum {
  PSX_FUNCTION_CALL_RESOLUTION_OK = 0,
  PSX_FUNCTION_CALL_RESOLUTION_NOT_CALLABLE,
} psx_function_call_resolution_status_t;

typedef struct {
  psx_function_call_resolution_status_t status;
  const psx_type_t *function_type;
} psx_function_call_resolution_t;

int psx_function_call_prepare_resolution_in(
    arena_context_t *arena_context, node_function_call_t *call);
void psx_function_call_bind_direct_name(
    node_function_call_t *call, char *name, int name_len);
char *psx_function_call_direct_name(
    const node_function_call_t *call);
int psx_function_call_direct_name_length(
    const node_function_call_t *call);
void psx_function_call_bind_type(
    node_function_call_t *call, const psx_type_t *callee_type);
const psx_type_t *psx_function_call_type(
    const node_function_call_t *call);
void psx_function_call_bind_qual_type(
    node_function_call_t *call, const psx_type_t *canonical_type,
    psx_qual_type_t callee_qual_type);
psx_qual_type_t psx_function_call_qual_type(
    const node_function_call_t *call);
void psx_function_call_set_implicit_declaration(
    node_function_call_t *call, int enabled);
int psx_function_call_is_implicit_declaration(
    const node_function_call_t *call);

void psx_resolve_function_call_type(
    const psx_type_t *bound_function_type,
    const psx_type_t *callee_type, int is_implicit_declaration,
    psx_function_call_resolution_t *resolution);
const psx_type_t *psx_resolve_function_reference_type(
    psx_semantic_context_t *semantic_context,
    const psx_type_t *function_type);

#endif
