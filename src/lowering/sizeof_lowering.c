#include "sizeof_lowering.h"

#include "../parser/node_utils.h"

node_t *lower_sizeof_query_expression(
    node_sizeof_query_t *query, node_t *evaluated_prefix) {
  if (!query) return NULL;
  node_t *result;
  if (query->runtime_size_expr) {
    result = query->runtime_size_expr;
  } else if (query->runtime_size_slot != 0) {
    node_t *slot = psx_node_new_unsigned_lvar_typed(
        query->runtime_size_slot, 8);
    result = ps_node_new_integer_cast_result(slot, NULL, 8, 1, 0);
  } else {
    result = ps_node_new_num(query->resolved_size);
  }
  return evaluated_prefix
             ? ps_node_new_binary(ND_COMMA, evaluated_prefix, result)
             : result;
}
