#include "type_identity_pass.h"

#include "../parser/node_utils.h"
#include "function_call_resolution.h"
#include "tree_walk.h"
#include "type_identity.h"
#include "../parser/semantic_ctx.h"

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_resolution_store_t *resolution_store;
  const node_t *failed_node;
} type_identity_pass_t;

static int intern_available_type(node_t *node, void *user) {
  type_identity_pass_t *pass = user;
  if (node->kind == ND_FUNCALL) {
    node_function_call_t *call = (node_function_call_t *)node;
    psx_qual_type_t callee_qual_type =
        psx_function_call_qual_type(pass->resolution_store, call);
    psx_qual_type_t expression_type = call->callee
        ? ps_node_qual_type(pass->resolution_store, call->callee)
        : (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                            PSX_TYPE_QUALIFIER_NONE};
    if (callee_qual_type.type_id == PSX_TYPE_ID_INVALID &&
        expression_type.type_id != PSX_TYPE_ID_INVALID) {
      callee_qual_type = psx_semantic_type_table_callable_function(
          ps_ctx_semantic_type_table_in(pass->semantic_context),
          expression_type);
    }
    psx_function_call_bind_qual_type(
        pass->resolution_store, call, callee_qual_type);
    if (expression_type.type_id != PSX_TYPE_ID_INVALID &&
        callee_qual_type.type_id == PSX_TYPE_ID_INVALID) {
      pass->failed_node = node;
      return 0;
    }
  }
  psx_qual_type_t node_qual_type =
      ps_node_qual_type(pass->resolution_store, node);
  if (node_qual_type.type_id == PSX_TYPE_ID_INVALID) {
    ps_node_clear_type(pass->resolution_store, node);
    return 1;
  }
  if (psx_semantic_type_table_qual_type_is_valid(
          ps_ctx_semantic_type_table_in(pass->semantic_context),
          node_qual_type)) {
    return 1;
  }
  pass->failed_node = node;
  return 0;
}

static int validate_interned_type(node_t *node, void *user) {
  type_identity_pass_t *pass = user;
  if (node->kind == ND_FUNCALL) {
    node_function_call_t *call = (node_function_call_t *)node;
    psx_qual_type_t callee_qual_type =
        psx_function_call_qual_type(pass->resolution_store, call);
    if (callee_qual_type.type_id != PSX_TYPE_ID_INVALID) {
      psx_function_call_bind_qual_type(
          pass->resolution_store, call, callee_qual_type);
      if (!psx_semantic_type_table_qual_type_is_valid(
              ps_ctx_semantic_type_table_in(pass->semantic_context),
              callee_qual_type)) {
        pass->failed_node = node;
        return 0;
      }
    }
  }
  psx_qual_type_t qual_type =
      ps_node_qual_type(pass->resolution_store, node);
  if (qual_type.type_id == PSX_TYPE_ID_INVALID) return 1;
  if (ps_node_bind_qual_type(
          pass->resolution_store, node, qual_type)) {
    return 1;
  }
  pass->failed_node = node;
  return 0;
}

static int finalize_type(node_t *node, void *user) {
  return intern_available_type(node, user) &&
         validate_interned_type(node, user);
}

int psx_intern_available_semantic_tree_types(
    psx_semantic_context_t *semantic_context, node_t *root,
    const node_t **failed_node) {
  if (failed_node) *failed_node = NULL;
  if (!semantic_context) {
    if (failed_node) *failed_node = root;
    return 0;
  }
  type_identity_pass_t pass = {
      .semantic_context = semantic_context,
      .resolution_store = ps_ctx_resolution_store(semantic_context),
  };
  int ok = psx_walk_semantic_tree_mut(
      pass.resolution_store, root, intern_available_type, &pass);
  if (!ok && failed_node) *failed_node = pass.failed_node;
  return ok;
}

int psx_finalize_semantic_tree_types(
    psx_semantic_context_t *semantic_context, node_t *root,
    const node_t **failed_node) {
  if (failed_node) *failed_node = NULL;
  if (!semantic_context) {
    if (failed_node) *failed_node = root;
    return 0;
  }
  type_identity_pass_t pass = {
      .semantic_context = semantic_context,
      .resolution_store = ps_ctx_resolution_store(semantic_context),
  };
  int ok = psx_walk_semantic_tree_mut(
      pass.resolution_store, root, finalize_type, &pass);
  if (!ok && failed_node) *failed_node = pass.failed_node;
  return ok;
}
