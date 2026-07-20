#include "lowered_tree_validation.h"

#include "../diag/diag.h"
#include "../parser/ast.h"
#include "../parser/semantic_ctx.h"
#include "assignment_resolution.h"
#include "assignment_validation.h"
#include "resolved_function.h"
#include "resolved_node_kind.h"
#include "resolved_node_type.h"
#include "tree_walk.h"
#include "type_compatibility_view.h"

typedef struct {
  psx_semantic_context_t *semantic_context;
  ag_diagnostic_context_t *diagnostics;
  const token_t *fallback_diag_tok;
  const node_function_definition_t *current_function;
} lowered_tree_validation_t;

static psx_qual_type_t lowered_node_qual_type(
    psx_semantic_context_t *semantic_context,
    const node_t *node) {
  if (!semantic_context || !node)
    return (psx_qual_type_t){
        PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  psx_resolution_store_t *store =
      ps_ctx_resolution_store(semantic_context);
  psx_qual_type_t type = ps_node_qual_type(store, node);
  return type.type_id != PSX_TYPE_ID_INVALID
             ? type
             : ps_ctx_intern_qual_type_in(
                   semantic_context, ps_node_get_type(store, node));
}

static void validate_lowered_return(
    const node_t *node, lowered_tree_validation_t *validation) {
  if (!node || !validation || !node->lhs ||
      !validation->current_function)
    return;
  psx_semantic_context_t *semantic_context =
      validation->semantic_context;
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_qual_type_t return_type =
      ps_function_definition_return_qual_type(
          types, validation->current_function);
  const psx_type_t *canonical_return_type =
      psx_type_compatibility_view_for(types, return_type);
  if (!canonical_return_type ||
      canonical_return_type->kind == PSX_TYPE_VOID)
    return;

  const psx_resolution_store_t *store =
      ps_ctx_resolution_store(semantic_context);
  int value_is_null_pointer_constant =
      psx_resolution_node_kind(store, node->lhs) == ND_NUM &&
      ((const node_num_t *)node->lhs)->val == 0;
  psx_return_types_status_t status;
  psx_resolve_return_qual_types_in(
      semantic_context, return_type,
      lowered_node_qual_type(semantic_context, node->lhs),
      value_is_null_pointer_constant, &status);

  token_t *tok = node->tok
                     ? node->tok
                     : (token_t *)validation->fallback_diag_tok;
  if (status == PSX_RETURN_TYPES_INCOMPATIBLE) {
    diag_emit_tokf_in(
        validation->diagnostics,
        DIAG_ERR_PARSER_RETURN_TYPES_INCOMPATIBLE, tok, "%s",
        diag_message_for_in(
            validation->diagnostics,
            DIAG_ERR_PARSER_RETURN_TYPES_INCOMPATIBLE));
  } else if (status == PSX_RETURN_TYPES_DISCARDS_QUALIFIERS) {
    diag_emit_tokf_in(
        validation->diagnostics,
        DIAG_ERR_PARSER_RETURN_DISCARDS_QUALIFIERS, tok, "%s",
        diag_message_for_in(
            validation->diagnostics,
            DIAG_ERR_PARSER_RETURN_DISCARDS_QUALIFIERS));
  }
}

static int validate_lowered_node(
    const node_t *node, void *user) {
  lowered_tree_validation_t *validation = user;
  if (psx_resolution_node_kind(
          ps_ctx_resolution_store(validation->semantic_context),
          node) == ND_RETURN)
    validate_lowered_return(node, validation);
  psx_validate_assignment_in_context(
      validation->semantic_context, node,
      validation->diagnostics,
      validation->fallback_diag_tok);
  return 1;
}

void psx_validate_lowered_tree_in_context(
    psx_semantic_context_t *semantic_context, const node_t *root,
    const token_t *fallback_diag_tok) {
  if (!semantic_context || !root) return;
  const psx_resolution_store_t *store =
      ps_ctx_resolution_store(semantic_context);
  lowered_tree_validation_t validation = {
      .semantic_context = semantic_context,
      .diagnostics = ps_ctx_diagnostics(semantic_context),
      .fallback_diag_tok = fallback_diag_tok,
      .current_function =
          psx_resolution_node_kind(store, root) == ND_FUNCDEF
              ? (const node_function_definition_t *)root
              : NULL,
  };
  psx_walk_semantic_tree(
      ps_ctx_resolution_store(semantic_context), root,
      validate_lowered_node, &validation);
}
