#include "semantic_invariants.h"

#include <string.h>

#include "../diag/diag.h"
#include "../parser/node_type_public.h"
#include "../parser/semantic_ctx.h"
#include "tree_walk.h"
#include "type_identity_pass.h"

typedef enum {
  NODE_SEMANTIC_ROLE_INVALID = 0,
  NODE_SEMANTIC_ROLE_NON_EXPRESSION,
  NODE_SEMANTIC_ROLE_RAW_EXPRESSION,
  NODE_SEMANTIC_ROLE_INITIALIZER_SYNTAX,
  NODE_SEMANTIC_ROLE_TYPED_EXPRESSION,
} node_semantic_role_t;

static node_semantic_role_t semantic_role(node_kind_t kind) {
  switch (kind) {
    case ND_IF:
    case ND_WHILE:
    case ND_DO_WHILE:
    case ND_FOR:
    case ND_SWITCH:
    case ND_CASE:
    case ND_DEFAULT:
    case ND_BREAK:
    case ND_CONTINUE:
    case ND_GOTO:
    case ND_LABEL:
    case ND_RETURN:
    case ND_BLOCK:
    case ND_FUNCDEF:
    case ND_VLA_ALLOC:
      return NODE_SEMANTIC_ROLE_NON_EXPRESSION;

    case ND_IDENTIFIER:
    case ND_UNARY_NEGATE:
    case ND_UNARY_DEREF:
    case ND_SUBSCRIPT:
    case ND_MEMBER_ACCESS:
    case ND_GENERIC_SELECTION:
    case ND_SIZEOF_QUERY:
    case ND_ALIGNOF_QUERY:
    case ND_COMPOUND_LITERAL:
      return NODE_SEMANTIC_ROLE_RAW_EXPRESSION;

    case ND_INIT_LIST:
    case ND_DECL_INIT:
      return NODE_SEMANTIC_ROLE_INITIALIZER_SYNTAX;

    case ND_ADD: case ND_SUB: case ND_MUL: case ND_DIV: case ND_MOD:
    case ND_EQ: case ND_NE: case ND_LT: case ND_LE:
    case ND_BITAND: case ND_BITXOR: case ND_BITOR:
    case ND_SHL: case ND_SHR: case ND_LOGAND: case ND_LOGOR:
    case ND_TERNARY: case ND_COMMA: case ND_ASSIGN:
    case ND_LVAR: case ND_PRE_INC: case ND_PRE_DEC:
    case ND_POST_INC: case ND_POST_DEC:
    case ND_FUNCALL: case ND_FUNCREF:
    case ND_DEREF: case ND_ADDR: case ND_STRING: case ND_NUM: case ND_GVAR:
    case ND_FP_TO_INT: case ND_INT_TO_FP: case ND_FNEG:
    case ND_VA_ARG_AREA: case ND_CAST: case ND_CREAL: case ND_CIMAG:
    case ND_STMT_EXPR:
      return NODE_SEMANTIC_ROLE_TYPED_EXPRESSION;

    default:
      return NODE_SEMANTIC_ROLE_INVALID;
  }
}

static int fail(psx_semantic_invariant_failure_t *failure,
                psx_semantic_invariant_status_t status,
                const node_t *node) {
  if (failure) {
    failure->status = status;
    failure->node = node;
  }
  return 0;
}

static int is_implicit_function_result_type(const psx_type_t *type) {
  return type && type->kind == PSX_TYPE_INTEGER &&
         type->integer_kind == PSX_INTEGER_KIND_INT &&
         !type->is_unsigned;
}

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_semantic_invariant_failure_t *failure;
  int allow_initializer_syntax;
} semantic_validation_t;

static int validate_node(const node_t *node, void *user) {
  semantic_validation_t *validation = user;
  psx_semantic_context_t *semantic_context =
      validation->semantic_context;
  psx_semantic_invariant_failure_t *failure = validation->failure;
  int allow_initializer_syntax = validation->allow_initializer_syntax;
  node_semantic_role_t role = semantic_role(node->kind);
  if (role == NODE_SEMANTIC_ROLE_INVALID)
    return fail(failure, PSX_SEMANTIC_INVARIANT_INVALID_NODE_KIND, node);
  if (role == NODE_SEMANTIC_ROLE_RAW_EXPRESSION)
    return fail(failure, PSX_SEMANTIC_INVARIANT_RAW_EXPRESSION, node);
  if (role == NODE_SEMANTIC_ROLE_INITIALIZER_SYNTAX &&
      !allow_initializer_syntax)
    return fail(
        failure,
        PSX_SEMANTIC_INVARIANT_INTERMEDIATE_INITIALIZER_SYNTAX, node);
  if (role == NODE_SEMANTIC_ROLE_TYPED_EXPRESSION && !node->type)
    return fail(failure, PSX_SEMANTIC_INVARIANT_MISSING_CANONICAL_TYPE, node);
  if (node->type && !ps_type_is_well_formed(node->type))
    return fail(failure, PSX_SEMANTIC_INVARIANT_INVALID_CANONICAL_TYPE, node);
  if (semantic_context && node->type) {
    psx_qual_type_t actual = ps_node_qual_type(node);
    if (actual.type_id == PSX_TYPE_ID_INVALID) {
      return fail(
          failure, PSX_SEMANTIC_INVARIANT_UNINTERNED_CANONICAL_TYPE, node);
    }
    if (node->type != ps_ctx_type_by_id_in(
                          semantic_context, actual.type_id)) {
      return fail(
          failure, PSX_SEMANTIC_INVARIANT_NONCANONICAL_TYPE_OBJECT, node);
    }
  }
  if (node->kind == ND_FUNCREF &&
      (node->type->kind != PSX_TYPE_POINTER || !node->type->base ||
       node->type->base->kind != PSX_TYPE_FUNCTION)) {
    return fail(failure, PSX_SEMANTIC_INVARIANT_INVALID_CALLABLE_TYPE, node);
  }
  if (node->kind == ND_FUNCDEF) {
    const node_function_definition_t *function =
        (const node_function_definition_t *)node;
    if (!function->signature ||
        function->signature->kind != PSX_TYPE_FUNCTION ||
        !ps_type_is_well_formed(function->signature) ||
        node->type != NULL) {
      return fail(
          failure, PSX_SEMANTIC_INVARIANT_INVALID_CALLABLE_TYPE, node);
    }
    if (semantic_context) {
      psx_qual_type_t actual =
          ps_function_definition_signature_qual_type(function);
      if (actual.type_id == PSX_TYPE_ID_INVALID) {
        return fail(
            failure, PSX_SEMANTIC_INVARIANT_UNINTERNED_CANONICAL_TYPE,
            node);
      }
      if (function->signature != ps_ctx_type_by_id_in(
                                     semantic_context, actual.type_id)) {
        return fail(
            failure, PSX_SEMANTIC_INVARIANT_NONCANONICAL_TYPE_OBJECT,
            node);
      }
    }
  }
  if (node->kind == ND_FUNCALL) {
    const node_function_call_t *call =
        (const node_function_call_t *)node;
    if (call->callee &&
        !ps_type_callable_function(call->callee->type)) {
      return fail(
          failure, PSX_SEMANTIC_INVARIANT_INVALID_CALLABLE_TYPE, node);
    }
    if (call->callee_type) {
      if (call->callee_type->kind != PSX_TYPE_FUNCTION ||
          !ps_type_is_well_formed(call->callee_type) ||
          node->type != call->callee_type->base) {
        return fail(
            failure, PSX_SEMANTIC_INVARIANT_INVALID_CALLABLE_TYPE, node);
      }
      if (semantic_context) {
        psx_qual_type_t actual =
            ps_function_call_callee_qual_type(call);
        if (actual.type_id == PSX_TYPE_ID_INVALID) {
          return fail(
              failure, PSX_SEMANTIC_INVARIANT_UNINTERNED_CANONICAL_TYPE,
              node);
        }
        if (call->callee_type != ps_ctx_type_by_id_in(
                                     semantic_context, actual.type_id)) {
          return fail(
              failure, PSX_SEMANTIC_INVARIANT_NONCANONICAL_TYPE_OBJECT,
              node);
        }
      }
    } else if (!node->is_implicit_func_decl ||
               !is_implicit_function_result_type(node->type)) {
      return fail(
          failure, PSX_SEMANTIC_INVARIANT_INVALID_CALLABLE_TYPE, node);
    }
  }
  int runtime_stride_off =
      node->type_state.vla_runtime.row_stride_frame_off;
  int runtime_strides_remaining =
      node->type_state.vla_runtime.strides_remaining;
  if ((runtime_stride_off != 0 &&
       (runtime_stride_off < 0 || !node->type ||
        !ps_type_contains_vla_array(node->type))) ||
      (runtime_stride_off == 0 && runtime_strides_remaining != 0) ||
      runtime_strides_remaining < 0) {
    return fail(
        failure, PSX_SEMANTIC_INVARIANT_INVALID_VLA_RUNTIME_VIEW, node);
  }
  return 1;
}

static int validate_tree(
    psx_semantic_context_t *semantic_context, const node_t *root,
    psx_semantic_invariant_failure_t *failure,
    int allow_initializer_syntax) {
  semantic_validation_t validation = {
      .semantic_context = semantic_context,
      .failure = failure,
      .allow_initializer_syntax = allow_initializer_syntax,
  };
  return psx_walk_semantic_tree(root, validate_node, &validation);
}

int psx_semantic_tree_has_canonical_expression_types(
    const node_t *root, psx_semantic_invariant_failure_t *failure) {
  if (failure) memset(failure, 0, sizeof(*failure));
  return validate_tree(NULL, root, failure, 0);
}

int psx_finalize_semantic_tree_type_identities(
    psx_semantic_context_t *semantic_context, node_t *root,
    psx_semantic_invariant_failure_t *failure,
    int allow_initializer_syntax) {
  if (failure) memset(failure, 0, sizeof(*failure));
  if (!semantic_context) {
    return fail(failure,
                PSX_SEMANTIC_INVARIANT_UNINTERNED_CANONICAL_TYPE, root);
  }
  const node_t *failed_node = NULL;
  if (!psx_finalize_semantic_tree_types(
          semantic_context, root, &failed_node)) {
    return fail(failure,
                PSX_SEMANTIC_INVARIANT_UNINTERNED_CANONICAL_TYPE,
                failed_node ? failed_node : root);
  }
  return validate_tree(semantic_context, root, failure,
                       allow_initializer_syntax ? 1 : 0);
}

static const char *semantic_invariant_status_name(
    psx_semantic_invariant_status_t status) {
  switch (status) {
    case PSX_SEMANTIC_INVARIANT_OK:
      return "ok";
    case PSX_SEMANTIC_INVARIANT_RAW_EXPRESSION:
      return "raw expression remains";
    case PSX_SEMANTIC_INVARIANT_INTERMEDIATE_INITIALIZER_SYNTAX:
      return "intermediate initializer syntax remains";
    case PSX_SEMANTIC_INVARIANT_MISSING_CANONICAL_TYPE:
      return "expression has no canonical type";
    case PSX_SEMANTIC_INVARIANT_INVALID_CANONICAL_TYPE:
      return "expression has an invalid canonical type";
    case PSX_SEMANTIC_INVARIANT_UNINTERNED_CANONICAL_TYPE:
      return "expression type could not be interned as a TypeId";
    case PSX_SEMANTIC_INVARIANT_NONCANONICAL_TYPE_OBJECT:
      return "expression type is not the canonical TypeId object";
    case PSX_SEMANTIC_INVARIANT_INVALID_CALLABLE_TYPE:
      return "callable has inconsistent canonical types";
    case PSX_SEMANTIC_INVARIANT_INVALID_VLA_RUNTIME_VIEW:
      return "VLA runtime view is inconsistent with its canonical type";
    case PSX_SEMANTIC_INVARIANT_INVALID_NODE_KIND:
      return "node kind is not classified by the semantic invariant";
  }
  return "unknown semantic invariant failure";
}

static void emit_semantic_invariant_failure(
    ag_diagnostic_context_t *diagnostics,
    const psx_semantic_invariant_failure_t *failure,
    const token_t *fallback_diag_tok) {
  const node_t *node = failure ? failure->node : NULL;
  const token_t *tok = node && node->tok ? node->tok : fallback_diag_tok;
  const char *detail = semantic_invariant_status_name(
      failure ? failure->status : PSX_SEMANTIC_INVARIANT_INVALID_NODE_KIND);
  if (tok) {
    diag_emit_tokf_in(
        diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED, tok,
        "%s: %s (node kind %d)",
        diag_message_for_in(diagnostics,
                            DIAG_ERR_INTERNAL_INVARIANT_FAILED), detail,
        node ? (int)node->kind : -1);
  }
  diag_emit_internalf_in(
      diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED,
      "%s: %s (node kind %d)",
      diag_message_for_in(diagnostics,
                          DIAG_ERR_INTERNAL_INVARIANT_FAILED), detail,
      node ? (int)node->kind : -1);
}

void psx_require_semantic_tree_has_canonical_expression_types(
    ag_diagnostic_context_t *diagnostics, const node_t *root,
    const token_t *fallback_diag_tok) {
  psx_semantic_invariant_failure_t failure;
  if (validate_tree(NULL, root, &failure, 0)) return;
  emit_semantic_invariant_failure(
      diagnostics, &failure, fallback_diag_tok);
}

void psx_require_semantic_initializer_has_canonical_expression_types(
    ag_diagnostic_context_t *diagnostics, const node_t *root,
    const token_t *fallback_diag_tok) {
  psx_semantic_invariant_failure_t failure = {0};
  if (validate_tree(NULL, root, &failure, 1)) return;
  emit_semantic_invariant_failure(
      diagnostics, &failure, fallback_diag_tok);
}

void psx_require_available_semantic_tree_types_interned(
    psx_semantic_context_t *semantic_context,
    ag_diagnostic_context_t *diagnostics, node_t *root,
    const token_t *fallback_diag_tok) {
  const node_t *failed_node = NULL;
  if (psx_intern_available_semantic_tree_types(
          semantic_context, root, &failed_node)) {
    return;
  }
  psx_semantic_invariant_failure_t failure = {
      .status = PSX_SEMANTIC_INVARIANT_UNINTERNED_CANONICAL_TYPE,
      .node = failed_node ? failed_node : root,
  };
  emit_semantic_invariant_failure(
      diagnostics, &failure, fallback_diag_tok);
}

void psx_require_semantic_tree_has_interned_expression_types(
    psx_semantic_context_t *semantic_context,
    ag_diagnostic_context_t *diagnostics, node_t *root,
    const token_t *fallback_diag_tok) {
  psx_semantic_invariant_failure_t failure = {0};
  if (psx_finalize_semantic_tree_type_identities(
          semantic_context, root, &failure, 0)) {
    return;
  }
  emit_semantic_invariant_failure(
      diagnostics, &failure, fallback_diag_tok);
}

void psx_require_semantic_initializer_has_interned_expression_types(
    psx_semantic_context_t *semantic_context,
    ag_diagnostic_context_t *diagnostics, node_t *root,
    const token_t *fallback_diag_tok) {
  psx_semantic_invariant_failure_t failure = {0};
  if (psx_finalize_semantic_tree_type_identities(
          semantic_context, root, &failure, 1)) {
    return;
  }
  emit_semantic_invariant_failure(
      diagnostics, &failure, fallback_diag_tok);
}
