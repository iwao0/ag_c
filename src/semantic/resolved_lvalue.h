#ifndef SEMANTIC_RESOLVED_LVALUE_H
#define SEMANTIC_RESOLVED_LVALUE_H

typedef struct arena_context_t arena_context_t;
typedef struct node_t node_t;

void ps_node_bind_symbol_decl_type_if_missing(node_t *node);
node_t *ps_node_clone_lvalue_with_lhs_in(
    arena_context_t *arena_context, node_t *target, node_t *lhs);

#endif
