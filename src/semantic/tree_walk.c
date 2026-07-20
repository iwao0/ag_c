#include "tree_walk.h"

#include "compound_literal_resolution.h"
#include "generic_selection_resolution.h"
#include "resolved_node_kind.h"
#include "resolved_node.h"
#include "resolved_function.h"
#include "sizeof_query_resolution.h"
#include "vla_runtime_plan.h"
#include "resolution_state.h"

static int walk_node(
    const psx_resolution_store_t *store, const node_t *node,
    psx_semantic_tree_visitor_t visitor, void *user);

static int walk_sizeof_vla_indices(
    const psx_resolution_store_t *store, const node_t *operand,
    psx_semantic_tree_visitor_t visitor, void *user) {
  if (!operand || operand->kind != ND_SUBSCRIPT) return 1;
  return walk_sizeof_vla_indices(store, operand->lhs, visitor, user) &&
         walk_node(store, operand->rhs, visitor, user);
}

static int walk_node(
    const psx_resolution_store_t *store, const node_t *node,
    psx_semantic_tree_visitor_t visitor, void *user) {
  if (!node) return 1;
  if (visitor && !visitor(node, user)) return 0;

  switch (psx_resolution_node_kind(store, node)) {
    case ND_COMPOUND_LITERAL: {
      const node_compound_literal_t *compound =
          (const node_compound_literal_t *)node;
      return psx_compound_literal_is_planned(store, compound)
                 ? 1 : walk_node(store, node->rhs, visitor, user);
    }
    case ND_BLOCK: {
      node_t *const *body = ((const node_block_t *)node)->body;
      for (int i = 0; body && body[i]; i++) {
        if (!walk_node(store, body[i], visitor, user)) return 0;
      }
      return 1;
    }
    case ND_STATIC_ASSERT:
      return walk_node(
          store,
          ((const node_static_assert_t *)node)->condition,
          visitor, user);
    case ND_VLA_ALLOC:
      return 1;
    case ND_FUNCDEF: {
      const node_function_definition_t *function =
          (const node_function_definition_t *)node;
      for (int i = 0; i < function->parameter_count; i++) {
        if (!walk_node(store, function->parameters[i], visitor, user))
          return 0;
      }
      break;
    }
    case ND_FUNCALL: {
      const node_function_call_t *call =
          (const node_function_call_t *)node;
      if (!walk_node(store, call->callee, visitor, user)) return 0;
      for (int i = 0; i < call->argument_count; i++) {
        if (!walk_node(store, call->arguments[i], visitor, user)) return 0;
      }
      break;
    }
    case ND_GENERIC_SELECTION: {
      const node_generic_selection_t *selection =
          (const node_generic_selection_t *)node;
      return walk_node(
          store,
          psx_generic_selection_selected_expression_const(store, selection),
          visitor, user);
    }
    case ND_CAST:
      break;
    case ND_SIZEOF_QUERY: {
      const node_sizeof_query_t *query =
          (const node_sizeof_query_t *)node;
      return !psx_sizeof_query_evaluates_vla_operand(store, query) ||
             walk_sizeof_vla_indices(
                 store, query->operand, visitor, user);
    }
    case ND_IF:
    case ND_FOR:
    case ND_TERNARY: {
      const node_ctrl_t *control = (const node_ctrl_t *)node;
      if (!walk_node(store, control->init, visitor, user) ||
          !walk_node(store, control->inc, visitor, user) ||
          !walk_node(store, control->els, visitor, user)) {
        return 0;
      }
      break;
    }
    case ND_INIT_LIST: {
      const node_init_list_t *list = (const node_init_list_t *)node;
      for (int i = 0; i < list->entry_count; i++) {
        const psx_initializer_entry_t *entry = &list->entries[i];
        if (!walk_node(store, entry->value, visitor, user)) return 0;
        for (int d = 0; d < entry->designator_count; d++) {
          if (!walk_node(
                  store, entry->designators[d].index_expr,
                  visitor, user) ||
              !walk_node(
                  store, entry->designators[d].range_end_expr,
                  visitor, user)) {
            return 0;
          }
        }
        for (int d = 0; d < entry->index_expr_count; d++) {
          if (!walk_node(store, entry->index_exprs[d], visitor, user))
            return 0;
        }
      }
      return 1;
    }
    default:
      break;
  }

  return walk_node(store, node->lhs, visitor, user) &&
         walk_node(store, node->rhs, visitor, user);
}

int psx_walk_semantic_tree(
    const psx_resolution_store_t *store, const node_t *root,
    psx_semantic_tree_visitor_t visitor, void *user) {
  return store && visitor ? walk_node(store, root, visitor, user) : 0;
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
    psx_resolution_store_t *store, node_t *root,
    psx_semantic_tree_mutating_visitor_t visitor, void *user) {
  if (!store || !visitor) return 0;
  mutating_walk_t walk = {
      .visitor = visitor,
      .user = user,
  };
  return walk_node(store, root, visit_mutable_node, &walk);
}
