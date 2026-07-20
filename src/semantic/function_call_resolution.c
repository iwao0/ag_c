#include "function_call_resolution.h"

#include "../parser/ast.h"
#include "resolution_state.h"
#include "../parser/node_utils.h"
#include "../parser/semantic_ctx.h"

#include <string.h>

psx_builtin_call_kind_t psx_function_call_builtin_kind(
    const node_function_call_t *call) {
  static const char builtin_expect[] = "__builtin_expect";
  if (!call || !call->callee ||
      call->callee->kind != ND_IDENTIFIER)
    return PSX_BUILTIN_CALL_NONE;
  const node_identifier_t *identifier =
      (const node_identifier_t *)call->callee;
  if (identifier->name_len ==
          (int)(sizeof(builtin_expect) - 1) &&
      memcmp(
          identifier->name, builtin_expect,
          sizeof(builtin_expect) - 1) == 0)
    return PSX_BUILTIN_CALL_EXPECT;
  return PSX_BUILTIN_CALL_NONE;
}

const node_t *psx_builtin_expect_value_operand(
    const node_function_call_t *call) {
  return psx_function_call_builtin_kind(call) ==
                 PSX_BUILTIN_CALL_EXPECT &&
             call->argument_count == 2 && call->arguments &&
             call->arguments[0] && call->arguments[1]
         ? call->arguments[0] : NULL;
}

static psx_function_call_resolution_state_t *call_state(
    psx_resolution_store_t *store, node_function_call_t *call) {
  psx_node_resolution_state_t *state =
      ps_node_resolution_state(store, call ? &call->base : NULL);
  return state ? &state->function_call : NULL;
}

static const psx_function_call_resolution_state_t *call_state_const(
    const psx_resolution_store_t *store,
    const node_function_call_t *call) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(store, call ? &call->base : NULL);
  return state ? &state->function_call : NULL;
}

int psx_function_call_prepare_resolution_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, node_function_call_t *call) {
  return call && ps_node_prepare_resolution_state_for_size_in(
                     store, arena_context, (node_t *)call, sizeof(*call));
}

void psx_function_call_bind_direct_name(
    psx_resolution_store_t *store,
    node_function_call_t *call, char *name, int name_len) {
  psx_function_call_resolution_state_t *state = call_state(store, call);
  if (!state) return;
  state->direct_name = name;
  state->direct_name_len = name_len;
}

char *psx_function_call_direct_name(
    const psx_resolution_store_t *store,
    const node_function_call_t *call) {
  const psx_function_call_resolution_state_t *state =
      call_state_const(store, call);
  return state ? state->direct_name : NULL;
}

int psx_function_call_direct_name_length(
    const psx_resolution_store_t *store,
    const node_function_call_t *call) {
  const psx_function_call_resolution_state_t *state =
      call_state_const(store, call);
  return state ? state->direct_name_len : 0;
}

void psx_function_call_bind_qual_type(
    psx_resolution_store_t *store,
    node_function_call_t *call,
    psx_qual_type_t callee_qual_type) {
  psx_function_call_resolution_state_t *state = call_state(store, call);
  if (!state) return;
  state->callee_qual_type = callee_qual_type;
}

psx_qual_type_t psx_function_call_qual_type(
    const psx_resolution_store_t *store,
    const node_function_call_t *call) {
  const psx_function_call_resolution_state_t *state =
      call_state_const(store, call);
  return state ? state->callee_qual_type
               : (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                                   PSX_TYPE_QUALIFIER_NONE};
}

void psx_function_call_set_implicit_declaration(
    psx_resolution_store_t *store,
    node_function_call_t *call, int enabled) {
  psx_function_call_resolution_state_t *state = call_state(store, call);
  if (!state) return;
  state->is_implicit_declaration = enabled ? 1 : 0;
}

int psx_function_call_is_implicit_declaration(
    const psx_resolution_store_t *store,
    const node_function_call_t *call) {
  const psx_function_call_resolution_state_t *state =
      call_state_const(store, call);
  return state && state->is_implicit_declaration;
}

psx_qual_type_t psx_resolve_function_reference_qual_type(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t function_qual_type) {
  psx_type_shape_t shape = {0};
  if (!semantic_context ||
      !psx_semantic_type_table_describe(
          ps_ctx_semantic_type_table_in(semantic_context),
          function_qual_type.type_id, &shape) ||
      shape.kind != PSX_TYPE_FUNCTION) {
    return (psx_qual_type_t){
        PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  }
  return ps_ctx_intern_pointer_to_qual_type_in(
      semantic_context, function_qual_type);
}
