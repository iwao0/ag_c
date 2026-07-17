#ifndef SEMANTIC_RESOLVED_LVALUE_H
#define SEMANTIC_RESOLVED_LVALUE_H

typedef struct arena_context_t arena_context_t;
typedef struct node_t node_t;

void ps_node_bind_symbol_decl_type_if_missing(node_t *node);

#endif
