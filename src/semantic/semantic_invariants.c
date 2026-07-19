#include "semantic_invariants.h"

#include <string.h>

#include "../diag/diag.h"
#include "resolved_node_type.h"
#include "../parser/node_vla_public.h"
#include "../parser/semantic_ctx.h"
#include "function_call_resolution.h"
#include "resolved_node_kind.h"
#include "resolved_object_ref.h"
#include "resolved_function.h"
#include "tree_walk.h"
#include "type_identity_pass.h"

typedef enum {
  NODE_SEMANTIC_ROLE_INVALID = 0,
  NODE_SEMANTIC_ROLE_NON_EXPRESSION,
  NODE_SEMANTIC_ROLE_RAW_EXPRESSION,
  NODE_SEMANTIC_ROLE_INITIALIZER_SYNTAX,
  NODE_SEMANTIC_ROLE_TYPED_EXPRESSION,
} node_semantic_role_t;

static node_semantic_role_t semantic_role(psx_resolution_node_kind_t kind) {
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
    case ND_STATIC_ASSERT:
      return NODE_SEMANTIC_ROLE_NON_EXPRESSION;

    case ND_IDENTIFIER:
    case ND_LOCAL_DECLARATION:
      return NODE_SEMANTIC_ROLE_RAW_EXPRESSION;

    case ND_INIT_LIST:
    case ND_DECL_INIT:
      return NODE_SEMANTIC_ROLE_INITIALIZER_SYNTAX;

    case ND_ADD: case ND_SUB: case ND_MUL: case ND_DIV: case ND_MOD:
    case ND_EQ: case ND_NE: case ND_LT: case ND_LE: case ND_GT: case ND_GE:
    case ND_BITAND: case ND_BITXOR: case ND_BITOR:
    case ND_SHL: case ND_SHR: case ND_LOGAND: case ND_LOGOR:
    case ND_TERNARY: case ND_COMMA: case ND_ASSIGN:
    case ND_COMPOUND_ASSIGN:
    case ND_LVAR: case ND_PRE_INC: case ND_PRE_DEC:
    case ND_POST_INC: case ND_POST_DEC:
    case ND_FUNCALL: case ND_FUNCREF:
    case ND_GENERIC_SELECTION:
    case ND_COMPOUND_LITERAL:
    case ND_ALIGNOF_QUERY:
    case ND_SIZEOF_QUERY:
    case ND_UNARY_PLUS:
    case ND_UNARY_NEGATE:
    case ND_LOGICAL_NOT:
    case ND_BITWISE_NOT:
    case ND_UNARY_DEREF: case ND_DEREF:
    case ND_SUBSCRIPT: case ND_MEMBER_ACCESS:
    case ND_ADDRESS_OF: case ND_ADDR: case ND_STRING: case ND_NUM:
    case ND_GVAR:
    case ND_FP_TO_INT: case ND_INT_TO_FP:
    case ND_VARARG_CURSOR: case ND_CAST: case ND_CREAL: case ND_CIMAG:
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
  const psx_resolution_store_t *resolution_store;
  psx_semantic_invariant_failure_t *failure;
  int allow_initializer_syntax;
} semantic_validation_t;

static int validate_node(const node_t *node, void *user) {
  semantic_validation_t *validation = user;
  psx_semantic_context_t *semantic_context =
      validation->semantic_context;
  const psx_resolution_store_t *store =
      validation->resolution_store;
  psx_semantic_invariant_failure_t *failure = validation->failure;
  int allow_initializer_syntax = validation->allow_initializer_syntax;
  psx_resolution_node_kind_t resolved_kind =
      psx_resolved_object_ref_node_kind(store, node);
  node_semantic_role_t role = semantic_role(resolved_kind);
  if (role == NODE_SEMANTIC_ROLE_INVALID)
    return fail(failure, PSX_SEMANTIC_INVARIANT_INVALID_NODE_KIND, node);
  if (role == NODE_SEMANTIC_ROLE_RAW_EXPRESSION)
    return fail(failure, PSX_SEMANTIC_INVARIANT_RAW_EXPRESSION, node);
  if (role == NODE_SEMANTIC_ROLE_INITIALIZER_SYNTAX &&
      !allow_initializer_syntax)
    return fail(
        failure,
        PSX_SEMANTIC_INVARIANT_INTERMEDIATE_INITIALIZER_SYNTAX, node);
  const psx_type_t *node_type = ps_node_get_type(store, node);
  if (role == NODE_SEMANTIC_ROLE_TYPED_EXPRESSION && !node_type)
    return fail(failure, PSX_SEMANTIC_INVARIANT_MISSING_CANONICAL_TYPE, node);
  if (node_type && !ps_type_is_well_formed(node_type))
    return fail(failure, PSX_SEMANTIC_INVARIANT_INVALID_CANONICAL_TYPE, node);
  if (semantic_context && node_type) {
    psx_qual_type_t actual = ps_node_qual_type(store, node);
    if (actual.type_id == PSX_TYPE_ID_INVALID) {
      return fail(
          failure, PSX_SEMANTIC_INVARIANT_UNINTERNED_CANONICAL_TYPE, node);
    }
    if (node_type != psx_semantic_type_table_lookup_qual_type(
                         ps_ctx_semantic_type_table_in(semantic_context),
                         actual)) {
      return fail(
          failure, PSX_SEMANTIC_INVARIANT_NONCANONICAL_TYPE_OBJECT, node);
    }
  }
  if (resolved_kind == ND_FUNCREF &&
      (node_type->kind != PSX_TYPE_POINTER || !node_type->base ||
       node_type->base->kind != PSX_TYPE_FUNCTION)) {
    return fail(failure, PSX_SEMANTIC_INVARIANT_INVALID_CALLABLE_TYPE, node);
  }
  if (resolved_kind == ND_FUNCDEF) {
    const node_function_definition_t *function =
        (const node_function_definition_t *)node;
    psx_qual_type_t actual =
        ps_function_definition_signature_qual_type(function);
    if (node_type != NULL) {
      return fail(
          failure, PSX_SEMANTIC_INVARIANT_INVALID_CALLABLE_TYPE, node);
    }
    if (actual.type_id == PSX_TYPE_ID_INVALID || !semantic_context) {
      return fail(
          failure, PSX_SEMANTIC_INVARIANT_UNINTERNED_CANONICAL_TYPE,
          node);
    }
    psx_type_shape_t signature = {0};
    if (!psx_semantic_type_table_describe(
            ps_ctx_semantic_type_table_in(semantic_context),
            actual.type_id, &signature)) {
      return fail(
          failure, PSX_SEMANTIC_INVARIANT_UNINTERNED_CANONICAL_TYPE,
          node);
    }
    if (signature.kind != PSX_TYPE_FUNCTION) {
      return fail(
          failure, PSX_SEMANTIC_INVARIANT_INVALID_CALLABLE_TYPE, node);
    }
  }
  if (node->kind == ND_FUNCALL) {
    const node_function_call_t *call =
        (const node_function_call_t *)node;
    const psx_type_t *callee_type =
        call->callee ? ps_node_get_type(store, call->callee) : NULL;
    if (call->callee &&
        !ps_type_callable_function(callee_type)) {
      return fail(
          failure, PSX_SEMANTIC_INVARIANT_INVALID_CALLABLE_TYPE, node);
    }
    const psx_type_t *resolved_callee_type =
        psx_function_call_type(store, call);
    if (resolved_callee_type) {
      if (resolved_callee_type->kind != PSX_TYPE_FUNCTION ||
          !ps_type_is_well_formed(resolved_callee_type)) {
        return fail(
            failure, PSX_SEMANTIC_INVARIANT_INVALID_CALLABLE_TYPE, node);
      }
      if (semantic_context) {
        psx_qual_type_t actual =
            psx_function_call_qual_type(store, call);
        if (actual.type_id == PSX_TYPE_ID_INVALID) {
          return fail(
              failure, PSX_SEMANTIC_INVARIANT_UNINTERNED_CANONICAL_TYPE,
              node);
        }
        const psx_semantic_type_table_t *types =
            ps_ctx_semantic_type_table_in(semantic_context);
        if (resolved_callee_type !=
            psx_semantic_type_table_lookup_qual_type(types, actual)) {
          return fail(
              failure, PSX_SEMANTIC_INVARIANT_NONCANONICAL_TYPE_OBJECT,
              node);
        }
        psx_qual_type_t expected_result =
            psx_semantic_type_table_base(types, actual.type_id);
        psx_qual_type_t actual_result = ps_node_qual_type(store, node);
        if (expected_result.type_id == PSX_TYPE_ID_INVALID ||
            expected_result.type_id != actual_result.type_id ||
            expected_result.qualifiers != actual_result.qualifiers) {
          return fail(
              failure, PSX_SEMANTIC_INVARIANT_INVALID_CALLABLE_TYPE,
              node);
        }
      }
    } else if (!psx_function_call_is_implicit_declaration(store, call) ||
               !is_implicit_function_result_type(node_type)) {
      return fail(
          failure, PSX_SEMANTIC_INVARIANT_INVALID_CALLABLE_TYPE, node);
    }
  }
  psx_vla_runtime_view_t runtime_view =
      ps_node_vla_runtime_view(store, node);
  int runtime_stride_off = runtime_view.row_stride_frame_off;
  int runtime_strides_remaining = runtime_view.strides_remaining;
  if ((runtime_stride_off != 0 &&
       (runtime_stride_off < 0 || !node_type ||
        !ps_type_contains_vla_array(node_type))) ||
      (runtime_stride_off == 0 && runtime_strides_remaining != 0) ||
      runtime_strides_remaining < 0) {
    return fail(
        failure, PSX_SEMANTIC_INVARIANT_INVALID_VLA_RUNTIME_VIEW, node);
  }
  return 1;
}

static int validate_tree(
    psx_semantic_context_t *semantic_context,
    const psx_resolution_store_t *store, const node_t *root,
    psx_semantic_invariant_failure_t *failure,
    int allow_initializer_syntax) {
  semantic_validation_t validation = {
      .semantic_context = semantic_context,
      .resolution_store = store,
      .failure = failure,
      .allow_initializer_syntax = allow_initializer_syntax,
  };
  return psx_walk_semantic_tree(
      store, root, validate_node, &validation);
}

int psx_semantic_tree_has_canonical_expression_types(
    psx_semantic_context_t *semantic_context, const node_t *root,
    psx_semantic_invariant_failure_t *failure) {
  if (failure) memset(failure, 0, sizeof(*failure));
  if (!semantic_context)
    return fail(failure,
                PSX_SEMANTIC_INVARIANT_UNINTERNED_CANONICAL_TYPE, root);
  return validate_tree(
      semantic_context, ps_ctx_resolution_store(semantic_context),
      root, failure, 0);
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
  return validate_tree(
      semantic_context, ps_ctx_resolution_store(semantic_context),
      root, failure,
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
    const psx_resolution_store_t *store,
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
        psx_resolution_node_kind(store, node));
  }
  diag_emit_internalf_in(
      diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED,
      "%s: %s (node kind %d)",
      diag_message_for_in(diagnostics,
                          DIAG_ERR_INTERNAL_INVARIANT_FAILED), detail,
      psx_resolution_node_kind(store, node));
}

void psx_require_semantic_tree_has_canonical_expression_types(
    psx_semantic_context_t *semantic_context,
    ag_diagnostic_context_t *diagnostics, const node_t *root,
    const token_t *fallback_diag_tok) {
  psx_semantic_invariant_failure_t failure;
  const psx_resolution_store_t *store =
      ps_ctx_resolution_store(semantic_context);
  if (validate_tree(semantic_context, store, root, &failure, 0)) return;
  emit_semantic_invariant_failure(
      store, diagnostics, &failure, fallback_diag_tok);
}

void psx_require_semantic_initializer_has_canonical_expression_types(
    psx_semantic_context_t *semantic_context,
    ag_diagnostic_context_t *diagnostics, const node_t *root,
    const token_t *fallback_diag_tok) {
  psx_semantic_invariant_failure_t failure = {0};
  const psx_resolution_store_t *store =
      ps_ctx_resolution_store(semantic_context);
  if (validate_tree(semantic_context, store, root, &failure, 1)) return;
  emit_semantic_invariant_failure(
      store, diagnostics, &failure, fallback_diag_tok);
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
      ps_ctx_resolution_store(semantic_context),
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
      ps_ctx_resolution_store(semantic_context),
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
      ps_ctx_resolution_store(semantic_context),
      diagnostics, &failure, fallback_diag_tok);
}
