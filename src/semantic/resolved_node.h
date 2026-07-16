#ifndef SEMANTIC_RESOLVED_NODE_H
#define SEMANTIC_RESOLVED_NODE_H

#include "../parser/ast.h"
#include "resolved_node_kind.h"

typedef struct {
  node_t base;
  struct psx_vla_runtime_plan_t *runtime_plan;
} node_vla_alloc_t;

#endif
