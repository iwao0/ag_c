#ifndef SEMANTIC_SEMANTIC_NODE_BUILDER_H
#define SEMANTIC_SEMANTIC_NODE_BUILDER_H

#include <stddef.h>

#include "../hir/hir_internal.h"
#include "typed_hir_materialization.h"

typedef struct arena_context_t arena_context_t;
typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_semantic_node_t psx_semantic_node_t;
typedef struct psx_vla_runtime_plan_t psx_vla_runtime_plan_t;

typedef struct {
  arena_context_t *arena_context;
  const psx_semantic_context_t *semantic_context;
  psx_resolved_hir_build_failure_t *failure;
} psx_semantic_node_builder_t;

void psx_semantic_node_builder_init(
    psx_semantic_node_builder_t *builder,
    arena_context_t *arena_context,
    const psx_semantic_context_t *semantic_context,
    psx_resolved_hir_build_failure_t *failure);
void psx_semantic_node_builder_fail(
    psx_semantic_node_builder_t *builder,
    psx_resolved_hir_build_status_t status,
    int source_node_kind);
int psx_semantic_node_builder_has_canonical_type(
    const psx_semantic_node_builder_t *builder,
    psx_qual_type_t qual_type);
psx_semantic_node_t *psx_semantic_node_builder_expression(
    psx_semantic_node_builder_t *builder,
    const psx_hir_node_spec_t *spec,
    psx_qual_type_t qual_type,
    psx_semantic_node_t *const *children,
    const psx_hir_edge_kind_t *child_edges,
    size_t child_count,
    const psx_hir_symbol_spec_t *symbol,
    int source_node_kind);
psx_semantic_node_t *psx_semantic_node_builder_leaf_expression(
    psx_semantic_node_builder_t *builder,
    const psx_hir_node_spec_t *spec,
    psx_qual_type_t qual_type,
    const psx_hir_symbol_spec_t *symbol,
    int source_node_kind);
psx_semantic_node_t *psx_semantic_node_builder_statement(
    psx_semantic_node_builder_t *builder,
    const psx_hir_node_spec_t *spec,
    psx_semantic_node_t *const *children,
    const psx_hir_edge_kind_t *child_edges,
    size_t child_count,
    int source_node_kind);
psx_semantic_node_t *psx_semantic_node_builder_vla_runtime(
    psx_semantic_node_builder_t *builder,
    const psx_vla_runtime_plan_t *plan,
    int source_node_kind);
psx_qual_type_t psx_semantic_node_expression_qual_type(
    const psx_semantic_node_t *node);

#endif
