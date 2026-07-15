#include "alignof_lowering.h"

#include "runtime_context.h"
#include "../parser/node_utils.h"

node_t *lower_alignof_query_expression(
    psx_lowering_context_t *lowering_context,
    node_alignof_query_t *query) {
  if (!query) return NULL;
  return ps_node_new_num_in(
      ps_lowering_arena(lowering_context), query->resolved_alignment);
}
