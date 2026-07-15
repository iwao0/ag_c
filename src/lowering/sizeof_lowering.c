#include "sizeof_lowering.h"
#include "runtime_context.h"

#include "../parser/node_utils.h"
#include "../parser/type_builder.h"

node_t *lower_sizeof_query_expression(
    psx_lowering_context_t *lowering_context,
    node_sizeof_query_t *query, node_t *evaluated_prefix) {
  if (!query) return NULL;
  arena_context_t *arena_context = ps_lowering_arena(lowering_context);
  node_t *result;
  if (query->runtime_size_expr) {
    result = query->runtime_size_expr;
  } else if (query->runtime_size_slot != 0) {
    node_t *slot = ps_node_new_unsigned_lvar_typed_in(
        arena_context, query->runtime_size_slot, 8);
    result = ps_node_new_integer_cast_result_in(
        arena_context, slot,
        ps_type_new_integer_in(arena_context, TK_LONG, 1));
  } else {
    result = ps_node_new_num_in(
        arena_context, query->resolved_size);
  }
  return evaluated_prefix
             ? ps_node_new_binary_in(
                   arena_context, ND_COMMA,
                   evaluated_prefix, result)
             : result;
}
