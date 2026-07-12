#include "alignof_lowering.h"

#include "../parser/node_utils.h"

node_t *lower_alignof_query_expression(node_alignof_query_t *query) {
  if (!query) return NULL;
  return ps_node_new_num(query->resolved_alignment);
}
