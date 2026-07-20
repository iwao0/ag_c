#include "semantic_pass.h"
#include "../parser/decl.h"
#include "../parser/declaration_syntax.h"
#include "../parser/diag.h"
#include "../parser/arena.h"
#include "../parser/global_registry.h"
#include "resolution_state.h"
#include "../parser/node_utils.h"
#include "../parser/node_vla_public.h"
#include "../parser/local_registry.h"
#include "../parser/semantic_ctx.h"
#include "../parser/type_builder.h"
#include "../parser/vla_runtime.h"
#include "../diag/diag.h"
#include "assignment_validation.h"
#include "call_resolution.h"
#include "case_label_resolution.h"
#include "constant_expression.h"
#include "expression_operand_resolution.h"
#include "function_call_resolution.h"
#include "generic_selection_resolution.h"
#include "lvar_usage_analysis.h"
#include "literal_resolution.h"
#include "member_access_resolution.h"
#include "sizeof_query_resolution.h"
#include "static_assert_resolution.h"
#include "type_name_resolution.h"
#include "type_query_resolution.h"
#include "vla_runtime_plan.h"
#include "resolved_node_kind.h"
#include "resolved_node.h"
#include "resolved_node_type.h"
#include "resolved_lvalue.h"
#include "resolved_object_ref.h"

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_global_registry_t *global_registry;
  psx_local_registry_t *local_registry;
  node_function_definition_t *current_func;
  const token_t *fallback_diag_tok;
} psx_semantic_traversal_t;

static ag_diagnostic_context_t *semantic_diagnostics(
    psx_semantic_context_t *semantic_context) {
  return ps_ctx_diagnostics(semantic_context);
}

static void semantic_transform_node(
    node_t *node, const psx_semantic_traversal_t *traversal);

static void semantic_bind_result_type(
    psx_semantic_context_t *semantic_context,
    node_t *node, const psx_type_t *type) {
  ps_node_bind_type(
      ps_ctx_resolution_store(semantic_context), node, type);
}

static int semantic_bind_qual_type_result(
    psx_semantic_context_t *semantic_context,
    node_t *node, psx_qual_type_t qual_type) {
  if (!semantic_context || !node ||
      qual_type.type_id == PSX_TYPE_ID_INVALID)
    return 0;
  const psx_type_t *canonical = ps_ctx_type_by_id_in(
      semantic_context, qual_type.type_id);
  if (!canonical) return 0;
  ps_node_bind_qual_type(
      ps_ctx_resolution_store(semantic_context),
      node, canonical, qual_type);
  return 1;
}

static void semantic_bind_canonical_result_type(
    psx_semantic_context_t *semantic_context,
    node_t *node, const psx_type_t *type) {
  psx_qual_type_t qual_type =
      ps_ctx_intern_qual_type_in(semantic_context, type);
  const psx_type_t *canonical = ps_ctx_type_by_id_in(
      semantic_context, qual_type.type_id);
  if (canonical)
    ps_node_bind_qual_type(
        ps_ctx_resolution_store(semantic_context),
        node, canonical, qual_type);
}

static psx_qual_type_t semantic_node_qual_type_value(
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
                   semantic_context,
                   ps_node_get_type(store, node));
}

static const char *semantic_control_statement_name(int node_kind) {
  switch (node_kind) {
    case ND_IF: return "if";
    case ND_WHILE: return "while";
    case ND_DO_WHILE: return "do-while";
    case ND_FOR: return "for";
    default: return NULL;
  }
}

static void semantic_validate_control_expression(
    psx_semantic_context_t *semantic_context, node_t *control,
    const token_t *fallback_diag_tok) {
  if (!semantic_context || !control || !control->lhs) return;
  psx_control_expression_requirement_t requirement =
      control->kind == ND_SWITCH
          ? PSX_CONTROL_EXPRESSION_REQUIRES_INTEGER
          : PSX_CONTROL_EXPRESSION_REQUIRES_SCALAR;
  psx_control_expression_status_t status;
  psx_resolve_control_expression_qual_type_in(
      semantic_context,
      semantic_node_qual_type_value(
          semantic_context, control->lhs),
      requirement, &status);
  ag_diagnostic_context_t *diagnostics =
      semantic_diagnostics(semantic_context);
  token_t *token = control->tok
                       ? control->tok
                       : (token_t *)fallback_diag_tok;
  if (status == PSX_CONTROL_EXPRESSION_NOT_SCALAR) {
    const char *statement = semantic_control_statement_name(
        control->kind);
    if (!statement) return;
    diag_emit_tokf_in(
        diagnostics, DIAG_ERR_PARSER_CONTROL_CONDITION_NOT_SCALAR,
        token, diag_message_for_in(
                   diagnostics,
                   DIAG_ERR_PARSER_CONTROL_CONDITION_NOT_SCALAR),
        statement);
  } else if (status == PSX_CONTROL_EXPRESSION_NOT_INTEGER) {
    diag_emit_tokf_in(
        diagnostics, DIAG_ERR_PARSER_SWITCH_CONDITION_NOT_INTEGER,
        token, "%s", diag_message_for_in(
                         diagnostics,
                         DIAG_ERR_PARSER_SWITCH_CONDITION_NOT_INTEGER));
  }
}

static void semantic_resolve_number_literal(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    node_num_t *literal) {
  if (!semantic_context || !literal ||
      ps_node_get_type(
          ps_ctx_resolution_store(semantic_context),
          (node_t *)literal)) return;
  psx_literal_semantic_resolution_t resolution;
  if (!psx_resolve_number_literal_semantics_in_contexts(
          semantic_context, global_registry, literal, &resolution))
    return;
  const psx_type_t *canonical = ps_ctx_type_by_id_in(
      semantic_context, resolution.qual_type.type_id);
  if (canonical)
    ps_node_bind_qual_type(
        ps_ctx_resolution_store(semantic_context),
        (node_t *)literal, canonical, resolution.qual_type);
}

static void semantic_resolve_string_literal(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    node_string_t *literal) {
  if (!semantic_context || !literal ||
      ps_node_get_type(
          ps_ctx_resolution_store(semantic_context),
          (node_t *)literal)) return;
  psx_literal_semantic_resolution_t resolution;
  if (!psx_resolve_string_literal_semantics_in_contexts(
          semantic_context, global_registry, literal, &resolution))
    return;
  if (resolution.string_label)
    psx_string_literal_bind_label(
        ps_ctx_resolution_store(semantic_context),
        literal, resolution.string_label);
  const psx_type_t *canonical = ps_ctx_type_by_id_in(
      semantic_context, resolution.qual_type.type_id);
  if (canonical)
    ps_node_bind_qual_type(
        ps_ctx_resolution_store(semantic_context),
        (node_t *)literal, canonical, resolution.qual_type);
}

static void semantic_bind_size_query_result_type(
    psx_semantic_context_t *semantic_context, node_t *query) {
  if (!semantic_context || !query ||
      ps_node_get_type(ps_ctx_resolution_store(semantic_context), query))
    return;
  semantic_bind_canonical_result_type(
      semantic_context, query,
      ps_type_new_integer_kind_in(
          ps_ctx_arena(semantic_context),
          PSX_INTEGER_KIND_LONG, 1, 0));
}

static int semantic_bind_address_result_type(
    psx_semantic_context_t *semantic_context,
    node_t *node, node_t *operand) {
  if (!semantic_context || !node || !operand) return 0;
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_qual_type_t operand_type = ps_node_qual_type(
      ps_ctx_resolution_store(semantic_context), operand);
  if (operand_type.type_id == PSX_TYPE_ID_INVALID) {
    operand_type = ps_ctx_intern_qual_type_in(
        semantic_context,
        ps_node_get_type(
            ps_ctx_resolution_store(semantic_context), operand));
  }
  const psx_type_t *operand_canonical = psx_semantic_type_table_lookup(
      types, operand_type.type_id);
  if (!operand_canonical) return 0;

  psx_qual_type_t pointee_type = operand_type;
  if (operand_canonical->kind == PSX_TYPE_ARRAY) {
    pointee_type = psx_semantic_type_table_base(
        types, operand_type.type_id);
  }
  psx_qual_type_t pointer_type =
      ps_ctx_intern_pointer_to_qual_type_in(
          semantic_context, pointee_type);
  return semantic_bind_qual_type_result(
      semantic_context, node, pointer_type);
}

static void semantic_resolve_explicit_address(
    psx_semantic_context_t *semantic_context,
    node_t *node, const token_t *fallback_diag_tok) {
  if (!semantic_context || !node || !node->lhs)
    return;
  psx_resolution_store_t *store =
      ps_ctx_resolution_store(semantic_context);
  psx_resolution_node_kind_t operand_kind =
      psx_resolved_object_ref_node_kind(store, node->lhs);
  psx_address_operand_category_t category =
      operand_kind == ND_FUNCREF
          ? PSX_ADDRESS_OPERAND_FUNCTION_DESIGNATOR
          : ps_node_is_lvalue_in(store, node->lhs)
                ? PSX_ADDRESS_OPERAND_OBJECT_LVALUE
                : PSX_ADDRESS_OPERAND_NOT_ADDRESSABLE;
  psx_address_operand_resolution_t resolution;
  psx_resolve_address_operand_qual_type_in(
      semantic_context,
      semantic_node_qual_type_value(semantic_context, node->lhs),
      category, ps_node_bitfield_width(store, node->lhs) > 0,
      &resolution);
  ag_diagnostic_context_t *diagnostics =
      semantic_diagnostics(semantic_context);
  token_t *tok = node->tok
                     ? node->tok
                     : (token_t *)fallback_diag_tok;
  if (resolution.status ==
      PSX_ADDRESS_OPERAND_REQUIRES_ADDRESSABLE_VALUE) {
    diag_emit_tokf_in(
        diagnostics,
        DIAG_ERR_PARSER_ADDRESS_REQUIRES_ADDRESSABLE_VALUE,
        tok, "%s", diag_message_for_in(
                       diagnostics,
                       DIAG_ERR_PARSER_ADDRESS_REQUIRES_ADDRESSABLE_VALUE));
    return;
  }
  if (resolution.status == PSX_ADDRESS_OPERAND_IS_BITFIELD) {
    diag_emit_tokf_in(
        diagnostics, DIAG_ERR_PARSER_ADDRESS_OF_BITFIELD,
        tok, "%s", diag_message_for_in(
                       diagnostics,
                       DIAG_ERR_PARSER_ADDRESS_OF_BITFIELD));
    return;
  }
  if (resolution.status != PSX_ADDRESS_OPERAND_OK) return;
  const psx_type_t *canonical = ps_ctx_type_by_id_in(
      semantic_context, resolution.result_qual_type.type_id);
  if (canonical)
    ps_node_bind_qual_type(
        store, node, canonical, resolution.result_qual_type);
}

static void semantic_transform_initializer_syntax(
    node_t *syntax, const psx_semantic_traversal_t *traversal) {
  if (!syntax) return;
  if (syntax->kind != ND_INIT_LIST) {
    semantic_transform_node(syntax, traversal);
    return;
  }
  node_init_list_t *list = (node_init_list_t *)syntax;
  for (int i = 0; i < list->entry_count; i++) {
    if (list->entries[i].designator_count > 0) {
      for (int d = 0; d < list->entries[i].designator_count; d++) {
        psx_initializer_designator_t *designator =
            &list->entries[i].designators[d];
        if (designator->kind == PSX_INIT_DESIGNATOR_INDEX) {
          semantic_transform_node(
              designator->index_expr, traversal);
          semantic_transform_node(
              designator->range_end_expr, traversal);
        }
      }
    } else {
      for (int d = 0; d < list->entries[i].index_expr_count; d++) {
        semantic_transform_node(
            list->entries[i].index_exprs[d], traversal);
      }
    }
    semantic_transform_initializer_syntax(
        list->entries[i].value, traversal);
  }
}

static void semantic_transform_return(
    psx_semantic_context_t *semantic_context,
    ag_diagnostic_context_t *diagnostics, node_t *node,
    node_function_definition_t *current_func,
    const token_t *fallback_diag_tok) {
  if (!node || node->kind != ND_RETURN || !current_func) return;
  const token_t *tok = node->tok ? node->tok : fallback_diag_tok;
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_qual_type_t return_qual_type =
      ps_function_definition_return_qual_type(types, current_func);
  const psx_type_t *return_type =
      psx_semantic_type_table_lookup_qual_type(types, return_qual_type);
  int returns_void = return_type && return_type->kind == PSX_TYPE_VOID;

  if (!node->lhs) {
    if (!returns_void) {
      diag_emit_tokf_in(diagnostics, DIAG_ERR_PARSER_INVALID_CONTEXT, tok,
                     "%s",
                     diag_message_for_in(diagnostics, DIAG_ERR_PARSER_RETURN_VALUE_REQUIRED_NONVOID));
    }
    return;
  }

  if (returns_void) {
    diag_emit_tokf_in(diagnostics, DIAG_ERR_PARSER_INVALID_CONTEXT, tok,
                   "%s",
                   diag_message_for_in(diagnostics, DIAG_ERR_PARSER_RETURN_VALUE_FORBIDDEN_VOID));
  }
}

static void semantic_transform_node_array(
    node_t **nodes, const psx_semantic_traversal_t *traversal) {
  if (!nodes) return;
  for (int i = 0; nodes[i]; i++) {
    semantic_transform_node(nodes[i], traversal);
  }
}

static void semantic_resolve_subscript(
    psx_semantic_context_t *semantic_context, node_t *node,
    const token_t *fallback_diag_tok) {
  if (!node || node->kind != ND_SUBSCRIPT) return;
  ag_diagnostic_context_t *diagnostics =
      semantic_diagnostics(semantic_context);
  psx_resolution_store_t *store =
      ps_ctx_resolution_store(semantic_context);
  psx_subscript_qual_types_resolution_t operands;
  psx_resolve_subscript_qual_types_in(
      semantic_context,
      semantic_node_qual_type_value(semantic_context, node->lhs),
      semantic_node_qual_type_value(semantic_context, node->rhs),
      &operands);
  if (operands.status == PSX_SUBSCRIPT_OPERANDS_INVALID) {
    ps_diag_ctx_in(diagnostics, node->tok ? node->tok : (token_t *)fallback_diag_tok,
                "subscript",
                "サブスクリプトの両辺ともポインタ/配列ではありません (C11 6.5.2.1p1)");
  }
  if (operands.swapped) {
    node_t *base = node->rhs;
    node->rhs = node->lhs;
    node->lhs = base;
  }
  semantic_bind_qual_type_result(
      semantic_context, node, operands.result_qual_type);
  int frame_off = ps_node_vla_row_stride_frame_off(store, node->lhs);
  int remaining = ps_node_vla_strides_remaining(store, node->lhs);
  ps_node_set_vla_runtime_view(
      store, node,
      frame_off != 0 && remaining > 0
          ? frame_off + PSX_VLA_RUNTIME_SLOT_SIZE
          : 0,
      remaining > 0 ? remaining - 1 : 0);
}

static void semantic_resolve_unary_deref(
    psx_semantic_context_t *semantic_context,
    node_t *node, const token_t *fallback_diag_tok) {
  if (!node || node->kind != ND_UNARY_DEREF) return;
  ag_diagnostic_context_t *diagnostics =
      semantic_diagnostics(semantic_context);
  token_t *tok = node->tok ? node->tok : (token_t *)fallback_diag_tok;
  psx_qual_type_t operand_type = semantic_node_qual_type_value(
      semantic_context, node->lhs);
  psx_deref_operand_status_t status =
      psx_resolve_deref_operand_qual_type_in(
          semantic_context, operand_type);
  if (status == PSX_DEREF_OPERAND_NOT_POINTER) {
    ps_diag_ctx_in(diagnostics, tok, "deref",
                "deref のオペランドはポインタ型でなければなりません (C11 6.5.3.2p2)");
  }
  if (status == PSX_DEREF_OPERAND_VOID_POINTER) {
    ps_diag_ctx_in(diagnostics, tok, "deref",
                "void* の deref はできません — キャストが必要です (C11 6.5.3.2)");
  }
  semantic_bind_qual_type_result(
      semantic_context, node,
      psx_resolve_indirection_result_qual_type_in(
          semantic_context, operand_type));
}

static int semantic_arithmetic_unary_operator(
    int kind, psx_type_arithmetic_unary_op_t *operator) {
  if (!operator) return 0;
  switch (kind) {
    case ND_UNARY_PLUS:
      *operator = PSX_TYPE_UNARY_PLUS;
      return 1;
    case ND_UNARY_NEGATE:
      *operator = PSX_TYPE_UNARY_NEGATE;
      return 1;
    case ND_CREAL:
      *operator = PSX_TYPE_UNARY_REAL;
      return 1;
    case ND_CIMAG:
      *operator = PSX_TYPE_UNARY_IMAGINARY;
      return 1;
    default:
      return 0;
  }
}

static void semantic_resolve_arithmetic_unary(
    psx_semantic_context_t *semantic_context,
    node_t *node, const char *operator_name,
    const token_t *fallback_diag_tok) {
  if (!node) return;
  ag_diagnostic_context_t *diagnostics =
      semantic_diagnostics(semantic_context);
  psx_type_arithmetic_unary_op_t operator;
  if (semantic_arithmetic_unary_operator(node->kind, &operator)) {
    semantic_bind_qual_type_result(
        semantic_context, node,
        psx_resolve_arithmetic_unary_result_qual_type_in(
            semantic_context, operator,
            semantic_node_qual_type_value(
                semantic_context, node->lhs)));
  }
  if (ps_node_get_type(
          ps_ctx_resolution_store(semantic_context), node)) return;
  ps_diag_ctx_in(diagnostics, node->tok ? node->tok : (token_t *)fallback_diag_tok,
              "unary", "%s のオペランドは算術型でなければなりません",
              operator_name);
}

static void semantic_resolve_logical_not(
    psx_semantic_context_t *semantic_context,
    node_t *node, const token_t *fallback_diag_tok) {
  if (!node || node->kind != ND_LOGICAL_NOT) return;
  semantic_bind_qual_type_result(
      semantic_context, node,
      psx_resolve_logical_not_result_qual_type_in(
          semantic_context,
          semantic_node_qual_type_value(
              semantic_context, node->lhs)));
  if (ps_node_get_type(
          ps_ctx_resolution_store(semantic_context), node))
    return;
  ps_diag_ctx_in(
      semantic_diagnostics(semantic_context),
      node->tok ? node->tok : (token_t *)fallback_diag_tok,
      "unary", "単項 ! のオペランドはスカラー型でなければなりません");
}

static void semantic_resolve_bitwise_not(
    psx_semantic_context_t *semantic_context,
    node_t *node, const token_t *fallback_diag_tok) {
  if (!node || node->kind != ND_BITWISE_NOT) return;
  semantic_bind_qual_type_result(
      semantic_context, node,
      psx_resolve_bitwise_not_result_qual_type_in(
          semantic_context,
          semantic_node_qual_type_value(
              semantic_context, node->lhs)));
  if (ps_node_get_type(
          ps_ctx_resolution_store(semantic_context), node))
    return;
  ps_diag_ctx_in(
      semantic_diagnostics(semantic_context),
      node->tok ? node->tok : (token_t *)fallback_diag_tok,
      "unary", "単項 ~ のオペランドは整数型でなければなりません");
}

static void semantic_resolve_incdec(
    psx_semantic_context_t *semantic_context,
    node_t *node, const token_t *fallback_diag_tok) {
  if (!node) return;
  ag_diagnostic_context_t *diagnostics =
      semantic_diagnostics(semantic_context);
  const char *op = node->kind == ND_PRE_INC || node->kind == ND_POST_INC
                       ? "++"
                       : "--";
  token_t *tok = node->tok
                     ? node->tok
                     : (token_t *)fallback_diag_tok;
  ps_node_expect_lvalue_at_in(
      ps_ctx_resolution_store(semantic_context),
      diagnostics, node->lhs, op, tok);
  ps_node_reject_const_assign_at_in(
      semantic_context, diagnostics, node->lhs, op, tok);
  psx_incdec_operand_resolution_t resolution;
  psx_resolve_incdec_operand_qual_type_in(
      semantic_context,
      semantic_node_qual_type_value(semantic_context, node->lhs),
      &resolution);
  if (resolution.status != PSX_INCDEC_OPERAND_OK) {
    ps_diag_ctx_in(diagnostics, tok, "incdec",
                "%s のオペランドは実数型またはポインタ型でなければなりません",
                op);
  }
  semantic_bind_qual_type_result(
      semantic_context, node, resolution.result_qual_type);
}

static void semantic_resolve_conditional(
    psx_semantic_context_t *semantic_context,
    node_ctrl_t *conditional,
    const token_t *fallback_diag_tok) {
  if (!semantic_context || !conditional ||
      conditional->base.kind != ND_TERNARY)
    return;
  node_t *node = &conditional->base;
  psx_conditional_types_resolution_t resolution;
  psx_resolve_conditional_qual_types_in(
      semantic_context,
      semantic_node_qual_type_value(
          semantic_context, node->lhs),
      semantic_node_qual_type_value(
          semantic_context, node->rhs),
      semantic_node_qual_type_value(
          semantic_context, conditional->els),
      &resolution);
  ag_diagnostic_context_t *diagnostics =
      semantic_diagnostics(semantic_context);
  token_t *token = node->tok
                       ? node->tok
                       : (token_t *)fallback_diag_tok;
  if (resolution.status ==
      PSX_CONDITIONAL_CONDITION_NOT_SCALAR) {
    diag_emit_tokf_in(
        diagnostics,
        DIAG_ERR_PARSER_CONDITIONAL_CONDITION_NOT_SCALAR,
        token, "%s", diag_message_for_in(
                         diagnostics,
                         DIAG_ERR_PARSER_CONDITIONAL_CONDITION_NOT_SCALAR));
    return;
  }
  if (resolution.status ==
      PSX_CONDITIONAL_BRANCH_TYPES_INCOMPATIBLE) {
    diag_emit_tokf_in(
        diagnostics,
        DIAG_ERR_PARSER_CONDITIONAL_BRANCH_TYPES_INCOMPATIBLE,
        token, "%s", diag_message_for_in(
                         diagnostics,
                         DIAG_ERR_PARSER_CONDITIONAL_BRANCH_TYPES_INCOMPATIBLE));
    return;
  }
  if (resolution.status != PSX_CONDITIONAL_TYPES_OK)
    return;
  const psx_type_t *canonical = ps_ctx_type_by_id_in(
      semantic_context, resolution.result_qual_type.type_id);
  if (canonical)
    ps_node_bind_qual_type(
        ps_ctx_resolution_store(semantic_context), node,
        canonical, resolution.result_qual_type);
}

static void semantic_resolve_member_access(
    psx_semantic_context_t *semantic_context,
    node_member_access_t *access,
    const token_t *fallback_diag_tok) {
  if (!access || !access->base.lhs) return;
  ag_diagnostic_context_t *diagnostics =
      semantic_diagnostics(semantic_context);
  token_t *tok = access->base.tok
                     ? access->base.tok
                     : (token_t *)fallback_diag_tok;
  psx_member_access_resolution_t resolution;
  psx_resolve_member_access(
      &(psx_member_access_resolution_request_t){
          .semantic_context = semantic_context,
          .base = access->base.lhs,
          .member_name = access->member_name,
          .member_name_len = access->member_name_len,
          .from_pointer = access->from_pointer,
      },
      &resolution);
  if (resolution.status == PSX_MEMBER_ACCESS_INVALID_BASE) {
    diag_emit_tokf_in(diagnostics,
        DIAG_ERR_PARSER_INVALID_CONTEXT, tok, "%s",
        diag_message_for_in(diagnostics,
            access->from_pointer
                ? DIAG_ERR_PARSER_ARROW_LHS_REQUIRES_STRUCT_PTR
                : DIAG_ERR_PARSER_DOT_LHS_REQUIRES_STRUCT));
  }
  if (resolution.status == PSX_MEMBER_ACCESS_NOT_FOUND) {
    ps_diag_ctx_in(diagnostics, tok, "member",
                diag_message_for_in(diagnostics, DIAG_ERR_PARSER_MEMBER_NOT_FOUND),
                access->member_name_len, access->member_name);
  }

  if (!ps_node_prepare_resolution_state_for_size_in(
          ps_ctx_resolution_store(semantic_context),
          ps_ctx_arena(semantic_context), (node_t *)access,
          sizeof(*access)))
    return;
  psx_member_access_state_t *state =
      psx_member_access_state_mut(
          ps_ctx_resolution_store(semantic_context), access);
  *state = (psx_member_access_state_t){
      .declaration = resolution.declaration,
      .record_id = resolution.record_id,
      .member_index = resolution.member_index,
      .is_resolved = 1,
  };

  const psx_type_t *access_type =
      psx_semantic_type_table_lookup_qual_type(
          ps_ctx_semantic_type_table_in(semantic_context),
          resolution.member_qual_type);
  if (access_type)
    ps_node_bind_qual_type(
        ps_ctx_resolution_store(semantic_context),
        (node_t *)access, access_type,
        resolution.member_qual_type);
  const psx_type_t *base_type = ps_node_get_type(
      ps_ctx_resolution_store(semantic_context), access->base.lhs);
  if (access->from_pointer) {
    state->base_address_qual_type =
        ps_node_qual_type(
            ps_ctx_resolution_store(semantic_context), access->base.lhs);
    if (state->base_address_qual_type.type_id == PSX_TYPE_ID_INVALID) {
      state->base_address_qual_type = ps_ctx_intern_qual_type_in(
          semantic_context, base_type);
    }
  } else {
    const psx_type_t *address_type = ps_type_address_result_in(
        ps_ctx_arena(semantic_context), base_type);
    state->base_address_qual_type = ps_ctx_intern_qual_type_in(
        semantic_context, address_type);
  }
  ps_node_set_bitfield_info(
      ps_ctx_resolution_store(semantic_context),
      (node_t *)access, state->declaration.bit_width, 0,
      state->declaration.bit_is_signed);
}

static void semantic_resolve_function_reference(
    psx_semantic_context_t *semantic_context,
    node_t *reference,
    const token_t *fallback_diag_tok) {
  if (!reference) return;
  ag_diagnostic_context_t *diagnostics =
      semantic_diagnostics(semantic_context);
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_qual_type_t source_type = semantic_node_qual_type_value(
      semantic_context, reference);
  psx_type_shape_t source_shape = {0};
  psx_qual_type_t type = {
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  if (psx_semantic_type_table_describe(
          types, source_type.type_id, &source_shape)) {
    if (source_shape.kind == PSX_TYPE_FUNCTION) {
      type = psx_resolve_function_reference_qual_type(
          semantic_context, source_type);
    } else if (psx_semantic_type_table_callable_function(
                   types, source_type).type_id !=
               PSX_TYPE_ID_INVALID) {
      type = source_type;
    }
  }
  if (type.type_id == PSX_TYPE_ID_INVALID) {
    ps_diag_ctx_in(diagnostics, reference->tok
                    ? reference->tok
                    : (token_t *)fallback_diag_tok,
                "funcref", "canonical function type is not bound");
  }
  semantic_bind_qual_type_result(
      semantic_context, reference, type);
}

static node_t *semantic_normalize_call_deref_chain(
    node_t *callee, const psx_semantic_traversal_t *traversal) {
  int deref_count = 0;
  node_t *bottom = callee;
  while (bottom &&
         (bottom->kind == ND_UNARY_DEREF ||
          psx_resolution_node_kind(
              ps_ctx_resolution_store(traversal->semantic_context),
              bottom) == ND_DEREF)) {
    deref_count++;
    bottom = bottom->lhs;
  }
  if (deref_count == 0 || !bottom) return callee;

  semantic_transform_node(
      bottom, traversal);
  const psx_type_t *type = ps_node_get_type(
      ps_ctx_resolution_store(traversal->semantic_context), bottom);
  int pointer_depth = 0;
  while (type && type->kind == PSX_TYPE_POINTER) {
    pointer_depth++;
    type = type->base;
  }
  if (!type || type->kind != PSX_TYPE_FUNCTION) return callee;

  int real_derefs = pointer_depth > 0 ? pointer_depth - 1 : 0;
  if (real_derefs > deref_count) real_derefs = deref_count;
  node_t *result = callee;
  for (int i = 0; i < deref_count - real_derefs; i++)
    result = result->lhs;
  return result;
}

static void semantic_resolve_function_call(
    psx_semantic_context_t *semantic_context,
    node_function_call_t *call,
    const token_t *fallback_diag_tok) {
  if (!call) return;
  ag_diagnostic_context_t *diagnostics =
      semantic_diagnostics(semantic_context);
  psx_resolution_store_t *store =
      ps_ctx_resolution_store(semantic_context);
  psx_qual_type_t bound_call_type =
      psx_function_call_qual_type(store, call);
  int is_implicit_declaration =
      psx_function_call_is_implicit_declaration(store, call);
  if (!is_implicit_declaration) {
    const psx_semantic_type_table_t *types =
        ps_ctx_semantic_type_table_in(semantic_context);
    psx_qual_type_t callee_type = call->callee
        ? semantic_node_qual_type_value(
              semantic_context, call->callee)
        : bound_call_type;
    if (callee_type.type_id == PSX_TYPE_ID_INVALID) {
      callee_type = bound_call_type;
    }
    psx_call_types_resolution_t call_types;
    psx_resolve_call_qual_types_in(
        semantic_context, callee_type, call->argument_count,
        &call_types);
    if (call_types.function_qual_type.type_id !=
        PSX_TYPE_ID_INVALID) {
      const psx_type_t *canonical_function =
          psx_semantic_type_table_lookup(
              types, call_types.function_qual_type.type_id);
      const psx_type_t *canonical_return =
          psx_semantic_type_table_lookup(
              types, call_types.return_qual_type.type_id);
      if (canonical_function)
        psx_function_call_bind_qual_type(
            store, call, call_types.function_qual_type);
      if (canonical_return)
        ps_node_bind_qual_type(
            store, (node_t *)call, canonical_return,
            call_types.return_qual_type);
    }
    if (call_types.status == PSX_CALL_TYPES_OK)
      return;
    if (call_types.status ==
        PSX_CALL_TYPES_ARGUMENT_COUNT_MISMATCH) {
      diag_emit_tokf_in(
          diagnostics, DIAG_ERR_PARSER_CALL_ARGUMENT_COUNT_MISMATCH,
          call->base.tok
              ? call->base.tok
              : (token_t *)fallback_diag_tok,
          "%s", diag_message_for_in(
                    diagnostics,
                    DIAG_ERR_PARSER_CALL_ARGUMENT_COUNT_MISMATCH));
      return;
    }
  }
  if (is_implicit_declaration) {
    semantic_bind_result_type(
        semantic_context, (node_t *)call,
        ps_type_new_integer_kind_in(
            ps_ctx_arena(semantic_context),
            PSX_INTEGER_KIND_INT, 0, 0));
    return;
  }
  diag_emit_tokf_in(
      diagnostics, DIAG_ERR_PARSER_CALL_NOT_CALLABLE,
      call->base.tok ? call->base.tok : (token_t *)fallback_diag_tok,
      "%s", diag_message_for_in(
                diagnostics, DIAG_ERR_PARSER_CALL_NOT_CALLABLE));
}

static const psx_type_t *semantic_resolve_type_name_ref(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_type_name_ref_t *type_name,
    psx_type_name_resolution_state_t *state) {
  return psx_resolve_bound_type_name_ref_in_contexts(
      semantic_context, global_registry, local_registry,
      type_name, state);
}

static void semantic_resolve_source_cast(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    node_source_cast_t *cast) {
  if (!cast || cast->base.kind != ND_SOURCE_CAST) return;
  if (!ps_node_prepare_resolution_state_for_size_in(
          ps_ctx_resolution_store(semantic_context),
          ps_ctx_arena(semantic_context), (node_t *)cast,
          sizeof(*cast)))
    return;
  psx_qual_type_t target_type;
  if (!psx_resolve_bound_type_name_qual_type_in_contexts(
          semantic_context, global_registry, local_registry,
          &cast->type_name,
          psx_node_type_name_state_mut(
              ps_ctx_resolution_store(semantic_context), &cast->base),
          &target_type))
    return;
  const psx_type_t *canonical =
      ps_ctx_type_by_id_in(semantic_context, target_type.type_id);
  if (canonical) {
    ps_node_bind_qual_type(
        ps_ctx_resolution_store(semantic_context),
        (node_t *)cast, canonical, target_type);
    psx_resolution_node_set_kind(
        ps_ctx_resolution_store(semantic_context),
        (node_t *)cast, ND_CAST);
  }
}

static void semantic_resolve_compound_literal(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
  psx_local_registry_t *local_registry,
    node_compound_literal_t *compound) {
  if (!compound) return;
  if (!ps_node_prepare_resolution_state_for_size_in(
          ps_ctx_resolution_store(semantic_context),
          ps_ctx_arena(semantic_context), (node_t *)compound,
          sizeof(*compound)))
    return;
  psx_type_name_resolution_state_t *type_name_state =
      psx_node_type_name_state_mut(
          ps_ctx_resolution_store(semantic_context), &compound->base);
  const psx_type_t *object_type =
      psx_type_name_resolved_type(type_name_state);
  if (!object_type) {
    object_type = semantic_resolve_type_name_ref(
        semantic_context, global_registry, local_registry,
        &compound->type_name, type_name_state);
  }
  const psx_type_t *result = ps_type_clone_in(
      ps_ctx_arena(semantic_context), object_type);
  psx_qual_type_t result_qual_type =
      ps_ctx_intern_qual_type_in(semantic_context, result);
  semantic_bind_qual_type_result(
      semantic_context, (node_t *)compound, result_qual_type);
}

static void semantic_resolve_generic_selection(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    node_generic_selection_t *selection,
    const token_t *fallback_diag_tok) {
  if (!selection) return;
  ag_diagnostic_context_t *diagnostics =
      semantic_diagnostics(semantic_context);
  psx_collect_lvar_usage_events_in(
      ps_ctx_resolution_store(semantic_context),
      local_registry, selection->control, NULL);
  token_t *tok = selection->base.tok
                     ? selection->base.tok
                     : (token_t *)fallback_diag_tok;
  psx_resolution_store_t *store =
      ps_ctx_resolution_store(semantic_context);
  if (!ps_node_prepare_resolution_state_for_size_in(
          store, ps_ctx_arena(semantic_context),
          (node_t *)selection, sizeof(*selection)))
    return;
  psx_node_resolution_state_t *node_state =
      ps_node_resolution_state(store, &selection->base);
  if (!node_state) return;
  psx_generic_selection_resolution_state_t *selection_resolution =
      &node_state->generic_selection;
  *selection_resolution =
      (psx_generic_selection_resolution_state_t){
          .selected_index = -1,
      };
  ps_node_clear_type(store, (node_t *)selection);
  psx_generic_selection_resolution_t resolution;
  psx_resolve_generic_selection_in_contexts(
      semantic_context, global_registry, local_registry,
      selection, &resolution);
  token_t *conflict_tok = tok;
  if (resolution.conflict_index >= 0 &&
      resolution.conflict_index < selection->association_count &&
      selection->associations[resolution.conflict_index].tok) {
    conflict_tok = selection->associations[resolution.conflict_index].tok;
  }
  switch (resolution.status) {
    case PSX_GENERIC_SELECTION_RESOLUTION_DUPLICATE_DEFAULT:
      diag_emit_tokf_in(
          diagnostics, DIAG_ERR_PARSER_GENERIC_DUPLICATE_DEFAULT,
          conflict_tok, "%s", diag_message_for_in(
                                diagnostics,
                                DIAG_ERR_PARSER_GENERIC_DUPLICATE_DEFAULT));
      return;
    case PSX_GENERIC_SELECTION_RESOLUTION_DUPLICATE_COMPATIBLE_TYPE:
      diag_emit_tokf_in(
          diagnostics,
          DIAG_ERR_PARSER_GENERIC_DUPLICATE_COMPATIBLE_TYPE,
          conflict_tok, "%s", diag_message_for_in(
                                diagnostics,
                                DIAG_ERR_PARSER_GENERIC_DUPLICATE_COMPATIBLE_TYPE));
      return;
    case PSX_GENERIC_SELECTION_RESOLUTION_NO_MATCH:
      diag_emit_tokf_in(
          diagnostics, DIAG_ERR_PARSER_GENERIC_NO_MATCH, tok, "%s",
          diag_message_for_in(
              diagnostics, DIAG_ERR_PARSER_GENERIC_NO_MATCH));
      return;
    case PSX_GENERIC_SELECTION_RESOLUTION_TYPE_UNRESOLVED:
      ps_diag_ctx_in(diagnostics, conflict_tok, "generic",
                  "canonical generic association/result type is not bound");
      return;
    case PSX_GENERIC_SELECTION_RESOLUTION_OK:
      break;
  }
  selection_resolution->selected_index = resolution.selected_index;
  selection_resolution->is_resolved = 1;
  node_t *selected_expression =
      selection->associations[resolution.selected_index].expression;
  semantic_bind_result_type(
      semantic_context, (node_t *)selection,
      ps_type_clone_in(
          ps_ctx_arena(semantic_context),
          ps_node_get_type(store, selected_expression)));
}

static void semantic_mark_usage_evaluated(
    psx_resolution_store_t *store, node_t *node) {
  if (!node) return;
  if (ps_node_records_lvar_usage(store, node))
    ps_node_set_lvar_usage_unevaluated(store, node, 0);
  switch (psx_resolved_object_ref_node_kind(store, node)) {
    case ND_BLOCK:
      for (node_t **body = ((node_block_t *)node)->body;
           body && *body; body++) {
        semantic_mark_usage_evaluated(store, *body);
      }
      return;
    case ND_FUNCALL: {
      node_function_call_t *call = (node_function_call_t *)node;
      semantic_mark_usage_evaluated(store, call->callee);
      for (int i = 0; i < call->argument_count; i++)
        semantic_mark_usage_evaluated(store, call->arguments[i]);
      return;
    }
    case ND_GENERIC_SELECTION: {
      node_generic_selection_t *selection =
          (node_generic_selection_t *)node;
      semantic_mark_usage_evaluated(
          store, psx_generic_selection_selected_expression(
                     store, selection));
      return;
    }
    case ND_IF:
    case ND_FOR:
    case ND_TERNARY: {
      node_ctrl_t *control = (node_ctrl_t *)node;
      semantic_mark_usage_evaluated(store, control->init);
      semantic_mark_usage_evaluated(store, node->lhs);
      semantic_mark_usage_evaluated(store, node->rhs);
      semantic_mark_usage_evaluated(store, control->inc);
      semantic_mark_usage_evaluated(store, control->els);
      return;
    }
    default:
      semantic_mark_usage_evaluated(store, node->lhs);
      semantic_mark_usage_evaluated(store, node->rhs);
      return;
  }
}

static void semantic_mark_sizeof_indices_evaluated(
    psx_resolution_store_t *store, node_t *operand) {
  if (!operand || operand->kind != ND_SUBSCRIPT) return;
  semantic_mark_sizeof_indices_evaluated(store, operand->lhs);
  semantic_mark_usage_evaluated(store, operand->rhs);
}

static void semantic_resolve_sizeof_query(
    node_sizeof_query_t *query,
    const psx_semantic_traversal_t *traversal) {
  if (!query) return;
  ag_diagnostic_context_t *diagnostics =
      semantic_diagnostics(traversal->semantic_context);
  const token_t *fallback_diag_tok = traversal->fallback_diag_tok;
  psx_resolution_store_t *store =
      ps_ctx_resolution_store(traversal->semantic_context);
  psx_parsed_type_name_t *syntax = query->type_name.syntax;
  if (query->is_type_name && syntax) {
    for (int i = 0; i < syntax->declarator.array_bound_count; i++) {
      semantic_transform_node(
          syntax->declarator.array_bounds[i].expression.node,
          traversal);
    }
  }

  psx_sizeof_query_resolution_t resolution;
  psx_resolve_sizeof_query_in_contexts(
      traversal->semantic_context, traversal->global_registry,
      traversal->local_registry,
      query, &resolution);
  node_t *issue_bound = NULL;
  if (syntax && resolution.issue_bound_index >= 0 &&
      resolution.issue_bound_index < syntax->declarator.array_bound_count) {
    issue_bound = syntax->declarator
                      .array_bounds[resolution.issue_bound_index]
                      .expression.node;
  }
  token_t *issue_tok = issue_bound && issue_bound->tok
                           ? issue_bound->tok
                           : (query->base.tok
                                  ? query->base.tok
                                  : (token_t *)fallback_diag_tok);
  switch (resolution.status) {
    case PSX_TYPE_QUERY_RESOLUTION_NEGATIVE_ARRAY_BOUND:
      ps_diag_ctx_in(diagnostics, issue_tok, "sizeof", "%s",
                  diag_message_for_in(diagnostics,
                      DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
      return;
    case PSX_TYPE_QUERY_RESOLUTION_INVALID_ARRAY_BOUND_TARGET:
      ps_diag_ctx_in(diagnostics, issue_tok, "sizeof",
                  "invalid deferred sizeof array bound target");
      return;
    case PSX_TYPE_QUERY_RESOLUTION_TYPE_UNRESOLVED:
      ps_diag_ctx_in(diagnostics, issue_tok, "sizeof",
                  "canonical sizeof query type is not bound");
      return;
    case PSX_TYPE_QUERY_RESOLUTION_OK:
      break;
  }
  if (syntax) {
    for (int i = 0; i < resolution.zero_length_bound_count; i++) {
      int bound_index = resolution.zero_length_bound_indices[i];
      if (bound_index < 0 ||
          bound_index >= syntax->declarator.array_bound_count)
        continue;
      node_t *bound =
          syntax->declarator.array_bounds[bound_index].expression.node;
      ps_ctx_record_unsupported_gnu_extension_in(
          traversal->semantic_context,
          bound && bound->tok ? bound->tok : query->base.tok,
          "zero-length array");
    }
  }
  if (resolution.usage_root)
    psx_collect_lvar_usage_events_in(
        store, traversal->local_registry,
        resolution.usage_root, NULL);
  if (resolution.evaluates_vla_operand)
    semantic_mark_sizeof_indices_evaluated(store, query->operand);
  if (psx_sizeof_query_runtime_size_slot(store, query) != 0)
    return;
}

static void semantic_transform_node(
    node_t *node, const psx_semantic_traversal_t *traversal) {
  if (!node) return;
  psx_resolution_store_t *store =
      ps_ctx_resolution_store(traversal->semantic_context);
  if (!ps_node_prepare_resolution_state_in(
          store, ps_ctx_arena(traversal->semantic_context), node))
    return;
  ps_node_bind_symbol_decl_type_if_missing(store, node);
  node_function_definition_t *current_func = traversal->current_func;
  const token_t *fallback_diag_tok = traversal->fallback_diag_tok;
  ag_diagnostic_context_t *diagnostics =
      semantic_diagnostics(traversal->semantic_context);

  switch (psx_resolved_object_ref_node_kind(store, node)) {
    case ND_STATIC_ASSERT: {
      node_static_assert_t *assertion =
          (node_static_assert_t *)node;
      semantic_transform_node(assertion->condition, traversal);
      int is_constant = 1;
      long long value =
          psx_eval_const_int(
              store, assertion->condition, &is_constant);
      psx_static_assert_resolution_t resolution;
      psx_resolve_static_assert(
          &(psx_static_assert_request_t){
              .is_constant = is_constant,
              .value = value,
          },
          &resolution);
      token_t *diagnostic_token = node->tok
                                      ? node->tok
                                      : (token_t *)fallback_diag_tok;
      if (resolution.status == PSX_STATIC_ASSERT_NOT_CONSTANT) {
        diag_emit_tokf_in(
            diagnostics, DIAG_ERR_PARSER_STATIC_ASSERT_COND_NOT_CONST,
            diagnostic_token, "%s",
            diag_message_for_in(
                diagnostics,
                DIAG_ERR_PARSER_STATIC_ASSERT_COND_NOT_CONST));
      } else if (resolution.status == PSX_STATIC_ASSERT_FAILED) {
        diag_emit_tokf_in(
            diagnostics, DIAG_ERR_PARSER_STATIC_ASSERT_FAILED,
            diagnostic_token, "%s",
            diag_message_for_in(
                diagnostics, DIAG_ERR_PARSER_STATIC_ASSERT_FAILED));
      }
      break;
    }
    case ND_VLA_ALLOC:
      break;
    case ND_NUM:
      semantic_resolve_number_literal(
          traversal->semantic_context, traversal->global_registry,
          (node_num_t *)node);
      break;
    case ND_STRING:
      semantic_resolve_string_literal(
          traversal->semantic_context, traversal->global_registry,
          (node_string_t *)node);
      break;
    case ND_DECL_INIT: {
      node_decl_init_t *init = (node_decl_init_t *)node;
      semantic_transform_node(node->lhs, traversal);
      if (init->init_kind == PSX_DECL_INIT_LIST) {
        semantic_transform_initializer_syntax(
            node->rhs, traversal);
      } else {
        semantic_transform_node(node->rhs, traversal);
      }
      semantic_bind_result_type(
          traversal->semantic_context, node, ps_type_clone_in(
                    ps_ctx_arena(traversal->semantic_context),
                    ps_node_get_type(store, node->lhs)));
      psx_validate_assignment_in_context(
          traversal->semantic_context, node, diagnostics,
          fallback_diag_tok);
      break;
    }
    case ND_RETURN:
      semantic_transform_return(
          traversal->semantic_context, diagnostics, node,
          current_func, fallback_diag_tok);
      semantic_transform_node(node->lhs, traversal);
      break;
    case ND_CASE: {
      node_case_t *case_node = (node_case_t *)node;
      semantic_transform_node(node->lhs, traversal);
      int is_constant = 1;
      long long value =
          psx_eval_const_int(store, node->lhs, &is_constant);
      if (!is_constant) {
        ps_diag_ctx_in(
            diagnostics,
            node->tok ? node->tok : (token_t *)fallback_diag_tok,
            "case",
            diag_message_for_in(
                diagnostics,
                DIAG_ERR_PARSER_NONNEG_CONSTEXPR_REQUIRED),
            "case label");
      }
      psx_case_label_bind_value(store, case_node, value);
      semantic_transform_node(node->rhs, traversal);
      break;
    }
    case ND_BLOCK:
      semantic_transform_node_array(
          ((node_block_t *)node)->body, traversal);
      break;
    case ND_FUNCDEF: {
      node_function_definition_t *function =
          (node_function_definition_t *)node;
      psx_semantic_traversal_t function_traversal = *traversal;
      function_traversal.current_func = function;
      semantic_transform_node_array(
          function->parameters, &function_traversal);
      semantic_transform_node(
          node->rhs, &function_traversal);
      break;
    }
    case ND_FUNCALL: {
      node_function_call_t *call = (node_function_call_t *)node;
      call->callee = semantic_normalize_call_deref_chain(
          call->callee, traversal);
      semantic_transform_node(
          call->callee, traversal);
      for (int i = 0; i < call->argument_count; i++) {
        semantic_transform_node(
            call->arguments[i], traversal);
      }
      semantic_resolve_function_call(
          traversal->semantic_context, call, fallback_diag_tok);
      break;
    }
    case ND_FUNCREF:
      semantic_resolve_function_reference(
          traversal->semantic_context, node, fallback_diag_tok);
      break;
    case ND_VARARG_CURSOR:
      if (!ps_node_get_type(store, node))
        semantic_bind_result_type(
            traversal->semantic_context, node, ps_type_new_pointer_in(
                      ps_ctx_arena(traversal->semantic_context),
                      ps_type_new_in(
                          ps_ctx_arena(traversal->semantic_context),
                          PSX_TYPE_VOID)));
      break;
    case ND_SUBSCRIPT:
      semantic_transform_node(node->lhs, traversal);
      semantic_transform_node(node->rhs, traversal);
      semantic_resolve_subscript(
          traversal->semantic_context, node, fallback_diag_tok);
      break;
    case ND_MEMBER_ACCESS:
      semantic_transform_node(node->lhs, traversal);
      semantic_resolve_member_access(
          traversal->semantic_context,
          (node_member_access_t *)node, fallback_diag_tok);
      break;
    case ND_UNARY_DEREF:
      semantic_transform_node(node->lhs, traversal);
      semantic_resolve_unary_deref(
          traversal->semantic_context, node, fallback_diag_tok);
      break;
    case ND_UNARY_PLUS:
    case ND_UNARY_NEGATE:
      semantic_transform_node(node->lhs, traversal);
      semantic_resolve_arithmetic_unary(
          traversal->semantic_context,
          node, node->kind == ND_UNARY_PLUS ? "単項 +" : "単項 -",
          fallback_diag_tok);
      break;
    case ND_LOGICAL_NOT:
      semantic_transform_node(node->lhs, traversal);
      semantic_resolve_logical_not(
          traversal->semantic_context, node, fallback_diag_tok);
      break;
    case ND_BITWISE_NOT:
      semantic_transform_node(node->lhs, traversal);
      semantic_resolve_bitwise_not(
          traversal->semantic_context, node, fallback_diag_tok);
      break;
    case ND_CREAL:
    case ND_CIMAG:
      semantic_transform_node(node->lhs, traversal);
      semantic_resolve_arithmetic_unary(
          traversal->semantic_context, node,
          node->kind == ND_CREAL ? "__real__" : "__imag__",
          fallback_diag_tok);
      break;
    case ND_GENERIC_SELECTION: {
      node_generic_selection_t *selection =
          (node_generic_selection_t *)node;
      semantic_transform_node(
          selection->control, traversal);
      for (int i = 0; i < selection->association_count; i++) {
        semantic_transform_node(
            selection->associations[i].expression,
            traversal);
      }
      semantic_resolve_generic_selection(
          traversal->semantic_context, traversal->global_registry,
          traversal->local_registry,
          selection, fallback_diag_tok);
      break;
    }
    case ND_SIZEOF_QUERY: {
      node_sizeof_query_t *query = (node_sizeof_query_t *)node;
      if (!query->is_type_name)
        semantic_transform_node(
            query->operand, traversal);
      semantic_resolve_sizeof_query(query, traversal);
      semantic_bind_size_query_result_type(
          traversal->semantic_context, node);
      break;
    }
    case ND_ALIGNOF_QUERY:
      psx_resolve_alignof_query_in_contexts(
          traversal->semantic_context, traversal->global_registry,
          traversal->local_registry,
          (node_alignof_query_t *)node);
      semantic_bind_size_query_result_type(
          traversal->semantic_context, node);
      break;
    case ND_SOURCE_CAST:
      semantic_transform_node(node->lhs, traversal);
      semantic_resolve_source_cast(
          traversal->semantic_context, traversal->global_registry,
          traversal->local_registry,
          (node_source_cast_t *)node);
      break;
    case ND_COMPOUND_LITERAL:
      semantic_resolve_compound_literal(
          traversal->semantic_context, traversal->global_registry,
          traversal->local_registry,
          (node_compound_literal_t *)node);
      semantic_transform_initializer_syntax(
          node->rhs, traversal);
      break;
    case ND_ADDRESS_OF:
      semantic_transform_node(node->lhs, traversal);
      semantic_resolve_explicit_address(
          traversal->semantic_context, node, fallback_diag_tok);
      break;
    case ND_ADDR:
      semantic_transform_node(node->lhs, traversal);
      if (!semantic_bind_address_result_type(
              traversal->semantic_context, node, node->lhs) &&
          !ps_node_get_type(store, node)) {
        semantic_bind_qual_type_result(
            traversal->semantic_context, node,
            psx_resolve_address_result_qual_type_in(
                traversal->semantic_context,
                semantic_node_qual_type_value(
                    traversal->semantic_context, node->lhs)));
      }
      break;
    case ND_PRE_INC:
    case ND_PRE_DEC:
    case ND_POST_INC:
    case ND_POST_DEC:
      semantic_transform_node(node->lhs, traversal);
      semantic_resolve_incdec(
          traversal->semantic_context, node, fallback_diag_tok);
      break;
    case ND_COMMA:
    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
    case ND_DIV:
    case ND_MOD:
    case ND_BITAND:
    case ND_BITXOR:
    case ND_BITOR:
    case ND_SHL:
    case ND_SHR:
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE:
    case ND_GT:
    case ND_GE:
    case ND_LOGAND:
    case ND_LOGOR:
      semantic_transform_node(node->lhs, traversal);
      semantic_transform_node(node->rhs, traversal);
      if (!ps_node_get_type(store, node)) {
        psx_type_binary_op_t operator;
        if (ps_node_binary_type_op(node->kind, &operator)) {
          semantic_bind_qual_type_result(
              traversal->semantic_context, node,
              psx_resolve_binary_result_qual_type_in(
                  traversal->semantic_context, operator,
                  semantic_node_qual_type_value(
                      traversal->semantic_context, node->lhs),
                  semantic_node_qual_type_value(
                      traversal->semantic_context, node->rhs)));
        }
      }
      break;
    case ND_IF:
    case ND_FOR:
    case ND_TERNARY: {
      node_ctrl_t *ctrl = (node_ctrl_t *)node;
      semantic_transform_node(ctrl->init, traversal);
      semantic_transform_node(node->lhs, traversal);
      semantic_transform_node(node->rhs, traversal);
      semantic_transform_node(ctrl->inc, traversal);
      semantic_transform_node(ctrl->els, traversal);
      if (node->kind == ND_TERNARY)
        semantic_resolve_conditional(
            traversal->semantic_context, ctrl,
            fallback_diag_tok);
      else
        semantic_validate_control_expression(
            traversal->semantic_context, node,
            fallback_diag_tok);
      break;
    }
    case ND_WHILE:
    case ND_DO_WHILE:
    case ND_SWITCH:
      semantic_transform_node(node->lhs, traversal);
      semantic_transform_node(node->rhs, traversal);
      semantic_validate_control_expression(
          traversal->semantic_context, node, fallback_diag_tok);
      break;
    case ND_STMT_EXPR: {
      semantic_transform_node(node->lhs, traversal);
      semantic_transform_node(node->rhs, traversal);
      semantic_bind_qual_type_result(
          traversal->semantic_context, node,
          semantic_node_qual_type_value(
              traversal->semantic_context, node->rhs));
      break;
    }
    default:
      semantic_transform_node(node->lhs, traversal);
      semantic_transform_node(node->rhs, traversal);
      psx_validate_assignment_in_context(
          traversal->semantic_context, node, diagnostics,
          fallback_diag_tok);
      if (node->kind == ND_ASSIGN ||
          node->kind == ND_COMPOUND_ASSIGN)
        ps_node_bind_type(
            store, node, ps_node_get_type(store, node->lhs));
      break;
  }
}

void psx_semantic_resolve_tree_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    node_t *node, node_function_definition_t *current_func,
    const token_t *fallback_diag_tok) {
  if (!semantic_context || !global_registry || !local_registry) return;
  psx_semantic_traversal_t traversal = {
      .semantic_context = semantic_context,
      .global_registry = global_registry,
      .local_registry = local_registry,
      .current_func = current_func,
      .fallback_diag_tok = fallback_diag_tok,
  };
  semantic_transform_node(node, &traversal);
}

void psx_semantic_resolve_initializer_tree_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    node_t *syntax, node_function_definition_t *current_func,
    const token_t *fallback_diag_tok) {
  if (!semantic_context || !global_registry || !local_registry) return;
  psx_semantic_traversal_t traversal = {
      .semantic_context = semantic_context,
      .global_registry = global_registry,
      .local_registry = local_registry,
      .current_func = current_func,
      .fallback_diag_tok = fallback_diag_tok,
  };
  semantic_transform_initializer_syntax(
      syntax, &traversal);
}
