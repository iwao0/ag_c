#include "tree_walk.h"

static int walk_node(
    const node_t *node, psx_semantic_tree_visitor_t visitor, void *user) {
  if (!node) return 1;
  if (visitor && !visitor(node, user)) return 0;

  switch (node->kind) {
    case ND_BLOCK: {
      node_t *const *body = ((const node_block_t *)node)->body;
      for (int i = 0; body && body[i]; i++) {
        if (!walk_node(body[i], visitor, user)) return 0;
      }
      return 1;
    }
    case ND_FUNCDEF: {
      const node_function_definition_t *function =
          (const node_function_definition_t *)node;
      for (int i = 0; i < function->parameter_count; i++) {
        if (!walk_node(function->parameters[i], visitor, user)) return 0;
      }
      break;
    }
    case ND_FUNCALL: {
      const node_function_call_t *call =
          (const node_function_call_t *)node;
      if (!walk_node(call->callee, visitor, user)) return 0;
      for (int i = 0; i < call->argument_count; i++) {
        if (!walk_node(call->arguments[i], visitor, user)) return 0;
      }
      break;
    }
    case ND_GENERIC_SELECTION: {
      const node_generic_selection_t *selection =
          (const node_generic_selection_t *)node;
      int selected = selection->selected_index;
      if (selected >= 0 && selected < selection->association_count) {
        return walk_node(
            selection->associations[selected].expression,
            visitor, user);
      }
      return 1;
    }
    case ND_IF:
    case ND_FOR:
    case ND_TERNARY: {
      const node_ctrl_t *control = (const node_ctrl_t *)node;
      if (!walk_node(control->init, visitor, user) ||
          !walk_node(control->inc, visitor, user) ||
          !walk_node(control->els, visitor, user)) {
        return 0;
      }
      break;
    }
    case ND_INIT_LIST: {
      const node_init_list_t *list = (const node_init_list_t *)node;
      for (int i = 0; i < list->entry_count; i++) {
        const psx_initializer_entry_t *entry = &list->entries[i];
        if (!walk_node(entry->value, visitor, user)) return 0;
        for (int d = 0; d < entry->designator_count; d++) {
          if (!walk_node(entry->designators[d].index_expr, visitor, user) ||
              !walk_node(entry->designators[d].range_end_expr, visitor,
                         user)) {
            return 0;
          }
        }
        for (int d = 0; d < entry->index_expr_count; d++) {
          if (!walk_node(entry->index_exprs[d], visitor, user)) return 0;
        }
      }
      return 1;
    }
    default:
      break;
  }

  return walk_node(node->lhs, visitor, user) &&
         walk_node(node->rhs, visitor, user);
}

int psx_walk_semantic_tree(
    const node_t *root, psx_semantic_tree_visitor_t visitor, void *user) {
  return visitor ? walk_node(root, visitor, user) : 0;
}

typedef struct {
  psx_semantic_tree_mutating_visitor_t visitor;
  void *user;
} mutating_walk_t;

static int visit_mutable_node(const node_t *node, void *user) {
  mutating_walk_t *walk = user;
  return walk->visitor((node_t *)node, walk->user);
}

int psx_walk_semantic_tree_mut(
    node_t *root, psx_semantic_tree_mutating_visitor_t visitor, void *user) {
  if (!visitor) return 0;
  mutating_walk_t walk = {
      .visitor = visitor,
      .user = user,
  };
  return walk_node(root, visit_mutable_node, &walk);
}
