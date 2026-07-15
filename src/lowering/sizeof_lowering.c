#include "sizeof_lowering.h"
#include "runtime_context.h"

#include "../parser/node_utils.h"
#include "../parser/type_builder.h"

node_t *lower_sizeof_query_expression(
    psx_lowering_context_t *lowering_context,
    node_sizeof_query_t *query, node_t *evaluated_prefix) {
  if (!query) return NULL;
  node_t *result;
  if (query->runtime_size_expr) {
    result = query->runtime_size_expr;
  } else if (query->runtime_size_slot != 0) {
    node_t *slot = ps_node_new_unsigned_lvar_typed(
        query->runtime_size_slot, 8);
    result = ps_node_new_integer_cast_result(
        slot, ps_type_new_integer_in(
                  ps_lowering_arena(lowering_context),
                  TK_UNSIGNED, 8, 1));
  } else {
    result = ps_node_new_num(query->resolved_size);
  }
  return evaluated_prefix
             ? ps_node_new_binary(ND_COMMA, evaluated_prefix, result)
             : result;
}
