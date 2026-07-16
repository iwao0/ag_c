#include "typed_hir_materialization.h"

#include <stdlib.h>
#include <string.h>

#include "../hir/hir_internal.h"
#include "semantic_node_internal.h"
#include "typed_hir_tree_internal.h"

typedef struct {
  psx_hir_module_t *module;
  psx_resolved_hir_build_failure_t *failure;
} hir_emitter_t;

static void set_failure(
    hir_emitter_t *emitter, psx_resolved_hir_build_status_t status,
    const psx_semantic_node_t *source) {
  if (!emitter->failure ||
      emitter->failure->status != PSX_RESOLVED_HIR_BUILD_OK)
    return;
  emitter->failure->status = status;
  emitter->failure->source_node_kind =
      source ? source->source_node_kind : -1;
}

static psx_hir_node_id_t emit_node(
    hir_emitter_t *emitter, const psx_semantic_node_t *source) {
  if (!source) {
    set_failure(emitter, PSX_RESOLVED_HIR_BUILD_INVALID_INPUT, NULL);
    return PSX_HIR_NODE_ID_INVALID;
  }
  psx_hir_node_id_t *children = NULL;
  if (source->spec.child_count) {
    children = malloc(
        source->spec.child_count * sizeof(*children));
    if (!children) {
      set_failure(
          emitter, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY, source);
      return PSX_HIR_NODE_ID_INVALID;
    }
    for (size_t i = 0; i < source->spec.child_count; i++) {
      children[i] = emit_node(emitter, source->children[i]);
      if (children[i] == PSX_HIR_NODE_ID_INVALID) {
        free(children);
        return PSX_HIR_NODE_ID_INVALID;
      }
    }
  }

  psx_hir_node_spec_t spec = source->spec;
  spec.children = children;
  spec.child_edges = source->child_edges;
  if (source->has_symbol) {
    spec.symbol_id = psx_hir_module_intern_symbol(
        emitter->module, &source->symbol);
    if (spec.symbol_id == PSX_HIR_SYMBOL_ID_INVALID) {
      free(children);
      set_failure(
          emitter, PSX_RESOLVED_HIR_BUILD_MISSING_RESOLVED_SYMBOL,
          source);
      return PSX_HIR_NODE_ID_INVALID;
    }
  }

  psx_hir_node_id_t result = PSX_HIR_NODE_ID_INVALID;
  int is_expression =
      psx_hir_kind_is_expression(source->spec.kind);
  if (is_expression) {
    const psx_semantic_expression_t *typed_expression =
        (const psx_semantic_expression_t *)source;
    psx_hir_expression_spec_t expression = {
        .node = spec,
        .qual_type = typed_expression->qual_type,
    };
    result = psx_hir_module_add_expression(
        emitter->module, &expression);
  } else {
    psx_hir_statement_spec_t statement = {.node = spec};
    result = psx_hir_module_add_statement(
        emitter->module, &statement);
  }
  free(children);
  if (result == PSX_HIR_NODE_ID_INVALID) {
    set_failure(
        emitter,
        is_expression
            ? PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE
            : PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
        source);
  }
  return result;
}

psx_hir_node_id_t psx_typed_hir_tree_emit(
    psx_hir_module_t *module,
    const psx_typed_hir_tree_t *typed_tree,
    psx_resolved_hir_build_failure_t *failure) {
  if (failure) memset(failure, 0, sizeof(*failure));
  if (!module || !typed_tree) {
    if (failure) {
      failure->status = PSX_RESOLVED_HIR_BUILD_INVALID_INPUT;
      failure->source_node_kind = -1;
    }
    return PSX_HIR_NODE_ID_INVALID;
  }
  const psx_semantic_node_t *source = typed_tree->root;
  if (!source) {
    if (failure) {
      failure->status = PSX_RESOLVED_HIR_BUILD_UNMATERIALIZED;
      failure->source_node_kind = -1;
    }
    return PSX_HIR_NODE_ID_INVALID;
  }

  size_t node_checkpoint = psx_hir_module_checkpoint(module);
  size_t root_checkpoint = psx_hir_module_root_count(module);
  size_t symbol_checkpoint = psx_hir_module_symbol_checkpoint(module);
  hir_emitter_t emitter = {
      .module = module,
      .failure = failure,
  };
  psx_hir_node_id_t root = emit_node(&emitter, source);
  if (root == PSX_HIR_NODE_ID_INVALID ||
      !psx_hir_module_add_root(module, root)) {
    if (failure && failure->status == PSX_RESOLVED_HIR_BUILD_OK)
      failure->status = PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY;
    psx_hir_module_rollback(
        module, node_checkpoint, root_checkpoint, symbol_checkpoint);
    return PSX_HIR_NODE_ID_INVALID;
  }
  return root;
}
