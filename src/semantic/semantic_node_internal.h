#ifndef SEMANTIC_SEMANTIC_NODE_INTERNAL_H
#define SEMANTIC_SEMANTIC_NODE_INTERNAL_H

#include "../hir/hir_internal.h"

typedef struct psx_semantic_node_t psx_semantic_node_t;

struct psx_semantic_node_t {
  psx_hir_node_spec_t spec;
  psx_semantic_node_t **children;
  psx_hir_edge_kind_t *child_edges;
  psx_hir_symbol_spec_t symbol;
  int source_node_kind;
  unsigned char has_symbol;
};

typedef struct {
  psx_semantic_node_t node;
  psx_qual_type_t qual_type;
} psx_semantic_expression_t;

typedef struct {
  psx_semantic_node_t node;
} psx_semantic_statement_t;

#endif
