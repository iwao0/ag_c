#ifndef SEMANTIC_RESOLVED_NODE_KIND_H
#define SEMANTIC_RESOLVED_NODE_KIND_H

#include "../parser/node_fwd.h"
#include "../parser/syntax_node_kind.h"

/*
 * Resolver/lowering-created working node kinds. Values are kept outside the
 * Syntax AST range so accidental cross-phase construction is easy to detect.
 */
typedef enum {
  PSX_RESOLVED_NODE_INVALID = 0,
  ND_FUNCDEF = 0x1000,
  ND_LVAR,
  ND_FUNCREF,
  ND_DEREF,
  ND_ADDR,
  ND_GVAR,
  ND_VLA_ALLOC,
  ND_FP_TO_INT,
  ND_INT_TO_FP,
  ND_VARARG_CURSOR,
} psx_resolved_node_kind_t;

typedef int psx_resolution_node_kind_t;
typedef struct psx_resolution_store_t psx_resolution_store_t;

psx_resolution_node_kind_t psx_resolution_node_kind(
    const psx_resolution_store_t *store, const node_t *node);
int psx_resolution_node_set_kind(
    psx_resolution_store_t *store, node_t *node,
    psx_resolved_node_kind_t kind);

#endif
