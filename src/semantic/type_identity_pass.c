#include "type_identity_pass.h"

#include "../parser/node_utils.h"
#include "function_call_resolution.h"
#include "tree_walk.h"
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
    const psx_type_t *callee_type =
        psx_function_call_type(pass->resolution_store, call);
    if (!callee_type && call->callee)
      callee_type = ps_type_callable_function(
          ps_node_get_type(pass->resolution_store, call->callee));
    psx_qual_type_t callee_qual_type = callee_type
        ? ps_ctx_intern_qual_type_in(
              pass->semantic_context, callee_type)
        : (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                            PSX_TYPE_QUALIFIER_NONE};
    psx_function_call_bind_qual_type(
        pass->resolution_store, call,
        ps_ctx_semantic_type_table_in(pass->semantic_context),
        callee_qual_type);
    if (callee_type &&
        callee_qual_type.type_id == PSX_TYPE_ID_INVALID) {
      pass->failed_node = node;
      return 0;
    }
  }
  const psx_type_t *node_type =
      ps_node_get_type(pass->resolution_store, node);
  if (!node_type) {
    ps_node_clear_type(pass->resolution_store, node);
    return 1;
  }
  psx_qual_type_t node_qual_type =
      ps_node_qual_type(pass->resolution_store, node);
  if (node_qual_type.type_id != PSX_TYPE_ID_INVALID &&
      node_type == ps_ctx_type_by_id_in(
                       pass->semantic_context,
                       node_qual_type.type_id)) {
    return 1;
  }
  psx_qual_type_t type =
      ps_ctx_intern_qual_type_in(pass->semantic_context, node_type);
  if (type.type_id != PSX_TYPE_ID_INVALID) {
    ps_node_set_qual_type_identity(pass->resolution_store, node, type);
    return 1;
  }
  pass->failed_node = node;
  return 0;
}

static int materialize_interned_type(node_t *node, void *user) {
  type_identity_pass_t *pass = user;
  if (node->kind == ND_FUNCALL) {
    node_function_call_t *call = (node_function_call_t *)node;
    psx_qual_type_t callee_qual_type =
        psx_function_call_qual_type(pass->resolution_store, call);
    if (callee_qual_type.type_id != PSX_TYPE_ID_INVALID) {
      const psx_type_t *callee_type = ps_ctx_type_by_id_in(
          pass->semantic_context, callee_qual_type.type_id);
      psx_function_call_bind_qual_type(
          pass->resolution_store, call,
          ps_ctx_semantic_type_table_in(pass->semantic_context),
          callee_qual_type);
      if (!callee_type) {
        pass->failed_node = node;
        return 0;
      }
    }
  }
  if (!ps_node_get_type(pass->resolution_store, node)) return 1;
  psx_qual_type_t qual_type =
      ps_node_qual_type(pass->resolution_store, node);
  const psx_type_t *canonical = ps_ctx_type_by_id_in(
      pass->semantic_context, qual_type.type_id);
  if (canonical) {
    ps_node_bind_qual_type(
        pass->resolution_store, node, canonical, qual_type);
    return 1;
  }
  pass->failed_node = node;
  return 0;
}

static int finalize_type(node_t *node, void *user) {
  return intern_available_type(node, user) &&
         materialize_interned_type(node, user);
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
