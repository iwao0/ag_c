#ifndef SEMANTIC_RESOLVED_NODE_KIND_H
#define SEMANTIC_RESOLVED_NODE_KIND_H

/*
 * Resolver/lowering-created working node kinds. Values are kept outside the
 * Syntax AST range so accidental cross-phase construction is easy to detect.
 */
typedef enum {
  ND_FUNCDEF = 0x1000,
  ND_LVAR,
  ND_FUNCREF,
  ND_DEREF,
  ND_GVAR,
  ND_VLA_ALLOC,
  ND_FP_TO_INT,
  ND_INT_TO_FP,
  ND_VA_ARG_AREA,
} psx_resolved_node_kind_t;

#endif
