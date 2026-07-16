#include "semantic_node_builder.h"

#include <string.h>

#include "../parser/arena.h"
#include "../parser/semantic_ctx.h"
#include "semantic_node_internal.h"

void psx_semantic_node_builder_init(
    psx_semantic_node_builder_t *builder,
    arena_context_t *arena_context,
    const psx_semantic_context_t *semantic_context,
    psx_resolved_hir_build_failure_t *failure) {
  if (!builder) return;
  *builder = (psx_semantic_node_builder_t){
      .arena_context = arena_context,
      .semantic_context = semantic_context,
      .failure = failure,
  };
}

void psx_semantic_node_builder_fail(
    psx_semantic_node_builder_t *builder,
    psx_resolved_hir_build_status_t status,
    int source_node_kind) {
  if (!builder || !builder->failure ||
      builder->failure->status != PSX_RESOLVED_HIR_BUILD_OK)
    return;
  builder->failure->status = status;
  builder->failure->source_node_kind = source_node_kind;
}

int psx_semantic_node_builder_has_canonical_type(
    const psx_semantic_node_builder_t *builder,
    psx_qual_type_t qual_type) {
  return builder && builder->semantic_context &&
         qual_type.type_id != PSX_TYPE_ID_INVALID &&
         ps_ctx_type_by_id_in(
             builder->semantic_context, qual_type.type_id) != NULL;
}

static psx_semantic_node_t *allocate_node(
    psx_semantic_node_builder_t *builder,
    const psx_hir_node_spec_t *spec,
    psx_semantic_node_t *const *children,
    const psx_hir_edge_kind_t *child_edges,
    size_t child_count,
    const psx_hir_symbol_spec_t *symbol,
    int source_node_kind,
    size_t storage_size) {
  if (!builder || !builder->arena_context || !spec ||
      (child_count && (!children || !child_edges))) {
    psx_semantic_node_builder_fail(
        builder, PSX_RESOLVED_HIR_BUILD_INVALID_INPUT,
        source_node_kind);
    return NULL;
  }
  for (size_t i = 0; i < child_count; i++) {
    if (children[i]) continue;
    psx_semantic_node_builder_fail(
        builder, PSX_RESOLVED_HIR_BUILD_INVALID_INPUT,
        source_node_kind);
    return NULL;
  }
  psx_semantic_node_t *node = arena_alloc_in(
      builder->arena_context, storage_size);
  if (!node) {
    psx_semantic_node_builder_fail(
        builder, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
        source_node_kind);
    return NULL;
  }
  memset(node, 0, storage_size);
  node->spec = *spec;
  node->source_node_kind = source_node_kind;
  node->spec.children = NULL;
  node->spec.child_edges = NULL;
  node->spec.child_count = child_count;
  if (child_count) {
    node->children = arena_alloc_in(
        builder->arena_context,
        child_count * sizeof(*node->children));
    node->child_edges = arena_alloc_in(
        builder->arena_context,
        child_count * sizeof(*node->child_edges));
    if (!node->children || !node->child_edges) {
      psx_semantic_node_builder_fail(
          builder, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
          source_node_kind);
      return NULL;
    }
    memcpy(
        node->children, children,
        child_count * sizeof(*node->children));
    memcpy(
        node->child_edges, child_edges,
        child_count * sizeof(*node->child_edges));
  }
  if (symbol) {
    node->symbol = *symbol;
    node->has_symbol = 1;
  }
  return node;
}

psx_semantic_node_t *psx_semantic_node_builder_expression(
    psx_semantic_node_builder_t *builder,
    const psx_hir_node_spec_t *spec,
    psx_qual_type_t qual_type,
    psx_semantic_node_t *const *children,
    const psx_hir_edge_kind_t *child_edges,
    size_t child_count,
    const psx_hir_symbol_spec_t *symbol,
    int source_node_kind) {
  if (!spec || !psx_hir_kind_is_expression(spec->kind)) {
    psx_semantic_node_builder_fail(
        builder, PSX_RESOLVED_HIR_BUILD_INVALID_INPUT,
        source_node_kind);
    return NULL;
  }
  if (!psx_semantic_node_builder_has_canonical_type(
          builder, qual_type)) {
    psx_semantic_node_builder_fail(
        builder, PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE,
        source_node_kind);
    return NULL;
  }
  psx_semantic_expression_t *expression =
      (psx_semantic_expression_t *)allocate_node(
          builder, spec, children, child_edges, child_count,
          symbol, source_node_kind, sizeof(*expression));
  if (!expression) return NULL;
  expression->qual_type = qual_type;
  return &expression->node;
}

psx_semantic_node_t *psx_semantic_node_builder_leaf_expression(
    psx_semantic_node_builder_t *builder,
    const psx_hir_node_spec_t *spec,
    psx_qual_type_t qual_type,
    const psx_hir_symbol_spec_t *symbol,
    int source_node_kind) {
  return psx_semantic_node_builder_expression(
      builder, spec, qual_type, NULL, NULL, 0,
      symbol, source_node_kind);
}

psx_semantic_node_t *psx_semantic_node_builder_statement(
    psx_semantic_node_builder_t *builder,
    const psx_hir_node_spec_t *spec,
    psx_semantic_node_t *const *children,
    const psx_hir_edge_kind_t *child_edges,
    size_t child_count,
    int source_node_kind) {
  if (!spec || psx_hir_kind_is_expression(spec->kind)) {
    psx_semantic_node_builder_fail(
        builder, PSX_RESOLVED_HIR_BUILD_INVALID_INPUT,
        source_node_kind);
    return NULL;
  }
  psx_semantic_statement_t *statement =
      (psx_semantic_statement_t *)allocate_node(
          builder, spec, children, child_edges, child_count,
          NULL, source_node_kind, sizeof(*statement));
  return statement ? &statement->node : NULL;
}

psx_qual_type_t psx_semantic_node_expression_qual_type(
    const psx_semantic_node_t *node) {
  if (!node || !psx_hir_kind_is_expression(node->spec.kind)) {
    return (psx_qual_type_t){
        PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  }
  return ((const psx_semantic_expression_t *)node)->qual_type;
}
