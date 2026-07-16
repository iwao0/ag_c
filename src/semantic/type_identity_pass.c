#include "type_identity_pass.h"

#include "../parser/node_utils.h"
#include "function_call_resolution.h"
#include "tree_walk.h"
#include "resolved_function.h"
#include "resolved_node_kind.h"
#include "../parser/semantic_ctx.h"

typedef struct {
  psx_semantic_context_t *semantic_context;
  const node_t *failed_node;
} type_identity_pass_t;

static int intern_available_type(node_t *node, void *user) {
  type_identity_pass_t *pass = user;
  if (node->kind == ND_FUNCDEF) {
    node_function_definition_t *function =
        (node_function_definition_t *)node;
    function->signature_qual_type = ps_ctx_intern_qual_type_in(
        pass->semantic_context, function->signature);
    if (function->signature_qual_type.type_id == PSX_TYPE_ID_INVALID) {
      pass->failed_node = node;
      return 0;
    }
  } else if (node->kind == ND_FUNCALL) {
    node_function_call_t *call = (node_function_call_t *)node;
    const psx_type_t *callee_type = psx_function_call_type(call);
    psx_qual_type_t callee_qual_type = callee_type
        ? ps_ctx_intern_qual_type_in(
              pass->semantic_context, callee_type)
        : (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                            PSX_TYPE_QUALIFIER_NONE};
    psx_function_call_bind_qual_type(
        call, callee_type, callee_qual_type);
    if (callee_type &&
        callee_qual_type.type_id == PSX_TYPE_ID_INVALID) {
      pass->failed_node = node;
      return 0;
    }
  }
  const psx_type_t *node_type = ps_node_get_type(node);
  if (!node_type) {
    ps_node_clear_type(node);
    return 1;
  }
  psx_qual_type_t node_qual_type = ps_node_qual_type(node);
  if (node_qual_type.type_id != PSX_TYPE_ID_INVALID &&
      node_type == ps_ctx_type_by_id_in(
                       pass->semantic_context,
                       node_qual_type.type_id)) {
    return 1;
  }
  psx_qual_type_t type =
      ps_ctx_intern_qual_type_in(pass->semantic_context, node_type);
  if (type.type_id != PSX_TYPE_ID_INVALID) {
    ps_node_set_qual_type_identity(node, type);
    return 1;
  }
  pass->failed_node = node;
  return 0;
}

static int materialize_interned_type(node_t *node, void *user) {
  type_identity_pass_t *pass = user;
  if (node->kind == ND_FUNCDEF) {
    node_function_definition_t *function =
        (node_function_definition_t *)node;
    function->signature = ps_ctx_type_by_id_in(
        pass->semantic_context, function->signature_qual_type.type_id);
    if (!function->signature) {
      pass->failed_node = node;
      return 0;
    }
  } else if (node->kind == ND_FUNCALL) {
    node_function_call_t *call = (node_function_call_t *)node;
    const psx_type_t *callee_type = psx_function_call_type(call);
    if (callee_type) {
      psx_qual_type_t callee_qual_type =
          psx_function_call_qual_type(call);
      callee_type = ps_ctx_type_by_id_in(
          pass->semantic_context, callee_qual_type.type_id);
      psx_function_call_bind_qual_type(
          call, callee_type, callee_qual_type);
      if (!callee_type) {
        pass->failed_node = node;
        return 0;
      }
    }
  }
  if (!ps_node_get_type(node)) return 1;
  psx_qual_type_t qual_type = ps_node_qual_type(node);
  const psx_type_t *canonical = ps_ctx_type_by_id_in(
      pass->semantic_context, qual_type.type_id);
  if (canonical) {
    ps_node_bind_qual_type(node, canonical, qual_type);
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
  };
  int ok = psx_walk_semantic_tree_mut(root, intern_available_type, &pass);
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
  };
  int ok = psx_walk_semantic_tree_mut(root, finalize_type, &pass);
  if (!ok && failed_node) *failed_node = pass.failed_node;
  return ok;
}
