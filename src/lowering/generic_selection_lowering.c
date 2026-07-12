#include "generic_selection_lowering.h"

node_t *lower_generic_selection_expression(node_t *node) {
  if (!node || node->kind != ND_GENERIC_SELECTION) return node;
  node_generic_selection_t *selection = (node_generic_selection_t *)node;
  int selected = selection->selected_index;
  if (selected < 0 || selected >= selection->association_count) return node;
  node_t *expression = selection->associations[selected].expression;
  return expression ? expression : node;
}
