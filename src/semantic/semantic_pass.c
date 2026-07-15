#include "semantic_pass.h"
#include "../parser/decl.h"
#include "../parser/declaration_syntax.h"
#include "../parser/diag.h"
#include "../parser/arena.h"
#include "../parser/global_registry.h"
#include "../parser/node_utils.h"
#include "../parser/local_registry.h"
#include "../parser/semantic_ctx.h"
#include "../parser/type_builder.h"
#include "../diag/diag.h"
#include "expression_operand_resolution.h"
#include "function_call_resolution.h"
#include "generic_selection_resolution.h"
#include "lvar_usage_analysis.h"
#include "member_access_resolution.h"
#include "type_name_resolution.h"
#include "type_query_resolution.h"

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
    node_t *node, const psx_type_t *type) {
  ps_node_bind_type(node, type);
}

static int semantic_bind_indirection_result_type(
    psx_semantic_context_t *semantic_context,
    node_t *node, node_t *operand) {
  if (!semantic_context || !node || !operand) return 0;
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_qual_type_t operand_type = ps_node_qual_type(operand);
  if (operand_type.type_id == PSX_TYPE_ID_INVALID) {
    operand_type = ps_ctx_intern_qual_type_in(
        semantic_context, ps_node_get_type(operand));
  }
  psx_qual_type_t result_type = psx_semantic_type_table_base(
      types, operand_type.type_id);
  const psx_type_t *canonical = psx_semantic_type_table_lookup(
      types, result_type.type_id);
  if (!canonical) return 0;
  ps_node_bind_qual_type(node, canonical, result_type);
  return 1;
}

static int semantic_bind_address_result_type(
    psx_semantic_context_t *semantic_context,
    node_t *node, node_t *operand) {
  if (!semantic_context || !node || !operand) return 0;
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_qual_type_t operand_type = ps_node_qual_type(operand);
  if (operand_type.type_id == PSX_TYPE_ID_INVALID) {
    operand_type = ps_ctx_intern_qual_type_in(
        semantic_context, ps_node_get_type(operand));
  }
  const psx_type_t *operand_canonical = psx_semantic_type_table_lookup(
      types, operand_type.type_id);
  if (!operand_canonical) return 0;

  psx_qual_type_t pointee_type = operand_type;
  if (!node->is_explicit_addr_expr &&
      operand_canonical->kind == PSX_TYPE_ARRAY) {
    pointee_type = psx_semantic_type_table_base(
        types, operand_type.type_id);
  }
  psx_qual_type_t pointer_type =
      ps_ctx_intern_pointer_to_qual_type_in(
          semantic_context, pointee_type);
  const psx_type_t *pointer_canonical = psx_semantic_type_table_lookup(
      types, pointer_type.type_id);
  if (!pointer_canonical) return 0;
  ps_node_bind_qual_type(node, pointer_canonical, pointer_type);
  return 1;
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
    ag_diagnostic_context_t *diagnostics, node_t *node,
    node_function_definition_t *current_func,
    const token_t *fallback_diag_tok) {
  if (!node || node->kind != ND_RETURN || !current_func) return;
  const token_t *tok = node->tok ? node->tok : fallback_diag_tok;
  const psx_type_t *return_type =
      ps_function_definition_return_type(current_func);
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

  /* C11 6.8.6.4 / 6.5.16.1: NULL pointer constant 0 is allowed, but a nonzero
   * integer constant cannot be returned from a pointer-returning function. */
  if (return_type && ps_type_is_pointer(return_type) &&
      node->lhs->kind == ND_NUM) {
    node_num_t *num = (node_num_t *)node->lhs;
    if (num->val != 0) {
      ps_diag_ctx_in(diagnostics, (token_t *)tok, "return",
                   "ポインタを返す関数から非ゼロ整数定数 (%lld) を返却できません (C11 6.8.6.4)",
                   num->val);
    }
  }

}

static void semantic_transform_node_array(
    node_t **nodes, const psx_semantic_traversal_t *traversal) {
  if (!nodes) return;
  for (int i = 0; nodes[i]; i++) {
    semantic_transform_node(nodes[i], traversal);
  }
}

static void semantic_validate_assignment(
    psx_semantic_context_t *semantic_context, node_t *node,
    ag_diagnostic_context_t *diagnostics,
    const token_t *fallback_diag_tok) {
  if (!node || node->kind != ND_ASSIGN || !node->lhs || !node->rhs) return;
  token_t *tok = node->tok ? node->tok : (token_t *)fallback_diag_tok;

  const psx_type_t *rhs_type = ps_node_get_type(node->rhs);
  if (rhs_type && rhs_type->kind == PSX_TYPE_VOID) {
    if (node->rhs->kind == ND_FUNCALL) {
      node_function_call_t *call =
          (node_function_call_t *)node->rhs;
      if (!call->callee && call->direct_name) {
        ps_diag_ctx_in(diagnostics, tok, "assign",
                     "void 戻り値関数の結果は代入/初期化に使えません: '%.*s' (C11 6.5.16)",
                     call->direct_name_len, call->direct_name);
      }
    }
    ps_diag_ctx_in(diagnostics, tok, "assign",
                 "void 戻り値関数の結果は代入/初期化に使えません (C11 6.5.16)");
  }

  if (node->is_decl_initializer) {
    const psx_type_t *lhs_type = ps_node_get_type(node->lhs);
    int lhs_is_pointer = lhs_type && ps_type_is_pointer(lhs_type);
    ps_node_reject_const_qual_discard_at_in(
        semantic_context, diagnostics, node->lhs, node->rhs, tok);
    if (lhs_is_pointer && node->rhs->kind == ND_NUM &&
        ((node_num_t *)node->rhs)->val != 0) {
      ps_diag_ctx_in(diagnostics, tok, "init",
                   "ポインタ変数を非ゼロ整数定数 (%lld) で初期化できません (C11 6.5.16.1)",
                   ((node_num_t *)node->rhs)->val);
    }
    if (!lhs_is_pointer && lhs_type &&
        !ps_type_is_tag_aggregate(lhs_type) &&
        lhs_type->kind != PSX_TYPE_ARRAY) {
      if (ps_node_value_is_pointer_like(node->rhs)) {
        ps_diag_ctx_in(diagnostics, tok, "init",
                     "スカラ変数をポインタ型で初期化できません (C11 6.5.16.1)");
      }
      if (ps_type_is_tag_aggregate(rhs_type)) {
        ps_diag_ctx_in(diagnostics, tok, "init",
                     "スカラ変数を %s 値で初期化できません (C11 6.5.16.1)",
                     ps_ctx_tag_kind_spelling(
                         ps_type_tag_token_kind(rhs_type)));
      }
    }
  }

  if (!node->is_source_assignment &&
      !node->is_source_compound_assignment) return;
  if (node->lhs->kind == ND_FUNCREF) {
    ps_diag_ctx_in(diagnostics, tok, "assign",
                 "関数識別子に代入することはできません (C11 6.5.16p2)");
  }
  ps_node_expect_lvalue_at_in(diagnostics, node->lhs, "=", tok);
  ps_node_reject_const_assign_at_in(
      semantic_context, diagnostics, node->lhs, "=", tok);
  if (node->is_source_assignment)
    ps_node_reject_const_qual_discard_at_in(
        semantic_context, diagnostics, node->lhs, node->rhs, tok);
}

static void semantic_resolve_subscript(
    psx_semantic_context_t *semantic_context, node_t *node,
    const token_t *fallback_diag_tok) {
  if (!node || node->kind != ND_SUBSCRIPT) return;
  ag_diagnostic_context_t *diagnostics =
      semantic_diagnostics(semantic_context);
  psx_subscript_operands_resolution_t operands;
  psx_resolve_subscript_operands(node->lhs, node->rhs, &operands);
  if (operands.status == PSX_SUBSCRIPT_OPERANDS_INVALID) {
    ps_diag_ctx_in(diagnostics, node->tok ? node->tok : (token_t *)fallback_diag_tok,
                "subscript",
                "サブスクリプトの両辺ともポインタ/配列ではありません (C11 6.5.2.1p1)");
  }
  node->lhs = operands.base;
  node->rhs = operands.index;
  if (!semantic_bind_indirection_result_type(
          semantic_context, node, node->lhs)) {
    semantic_bind_result_type(
        node, psx_resolve_indirection_result_type(
                  semantic_context, node->lhs));
  }
}

static void semantic_resolve_unary_deref(
    psx_semantic_context_t *semantic_context,
    node_t *node, const token_t *fallback_diag_tok) {
  if (!node || node->kind != ND_UNARY_DEREF) return;
  ag_diagnostic_context_t *diagnostics =
      semantic_diagnostics(semantic_context);
  token_t *tok = node->tok ? node->tok : (token_t *)fallback_diag_tok;
  psx_deref_operand_status_t status = psx_resolve_deref_operand(node->lhs);
  if (status == PSX_DEREF_OPERAND_NOT_POINTER) {
    ps_diag_ctx_in(diagnostics, tok, "deref",
                "deref のオペランドはポインタ型でなければなりません (C11 6.5.3.2p2)");
  }
  if (status == PSX_DEREF_OPERAND_VOID_POINTER) {
    ps_diag_ctx_in(diagnostics, tok, "deref",
                "void* の deref はできません — キャストが必要です (C11 6.5.3.2)");
  }
  if (!semantic_bind_indirection_result_type(
          semantic_context, node, node->lhs)) {
    semantic_bind_result_type(
        node, psx_resolve_indirection_result_type(
                  semantic_context, node->lhs));
  }
}

static void semantic_resolve_arithmetic_unary(
    psx_semantic_context_t *semantic_context,
    node_t *node, const char *operator_name,
    const token_t *fallback_diag_tok) {
  if (!node) return;
  ag_diagnostic_context_t *diagnostics =
      semantic_diagnostics(semantic_context);
  semantic_bind_result_type(
      node, psx_resolve_arithmetic_unary_result_type(
                semantic_context, node->kind, node->lhs));
  if (node->type) return;
  ps_diag_ctx_in(diagnostics, node->tok ? node->tok : (token_t *)fallback_diag_tok,
              "unary", "%s のオペランドは算術型でなければなりません",
              operator_name);
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
  ps_node_expect_lvalue_at_in(diagnostics, node->lhs, op, tok);
  ps_node_reject_const_assign_at_in(
      semantic_context, diagnostics, node->lhs, op, tok);
  const psx_type_t *type = psx_resolve_incdec_result_type(
      semantic_context, node->lhs);
  if (!type) {
    ps_diag_ctx_in(diagnostics, tok, "incdec",
                "%s のオペランドは実数型またはポインタ型でなければなりません",
                op);
  }
  semantic_bind_result_type(node, type);
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

  access->resolved_member = arena_alloc_in(
      ps_ctx_arena(semantic_context), sizeof(*access->resolved_member));
  *access->resolved_member = resolution.declaration;
  access->resolved_record_id = resolution.record_id;
  access->resolved_member_index = resolution.member_index;

  const psx_type_t *decl_type =
      psx_record_member_decl_type(access->resolved_member);
  psx_type_t *access_type = decl_type
                                ? ps_type_clone_in(
                                      ps_ctx_arena(semantic_context),
                                      decl_type)
                                : NULL;
  if (access_type && resolution.base_object_type)
    ps_type_set_decl_spec_qualifiers(
        access_type,
        (resolution.base_object_qual_type.qualifiers &
         PSX_TYPE_QUALIFIER_CONST) != 0,
        (resolution.base_object_qual_type.qualifiers &
         PSX_TYPE_QUALIFIER_VOLATILE) != 0);
  ps_node_bind_type((node_t *)access, access_type);
  const psx_type_t *base_type = ps_node_get_type(access->base.lhs);
  if (access->from_pointer) {
    access->base_address_qual_type = ps_node_qual_type(access->base.lhs);
    if (access->base_address_qual_type.type_id == PSX_TYPE_ID_INVALID) {
      access->base_address_qual_type = ps_ctx_intern_qual_type_in(
          semantic_context, base_type);
    }
  } else {
    const psx_type_t *address_type = ps_type_address_result_in(
        ps_ctx_arena(semantic_context), base_type);
    access->base_address_qual_type = ps_ctx_intern_qual_type_in(
        semantic_context, address_type);
  }
  access->base.type_state.bit_width =
      (unsigned char)access->resolved_member->bit_width;
  access->base.type_state.bit_offset = 0;
  access->base.type_state.bit_is_signed =
      access->resolved_member->bit_is_signed ? 1 : 0;
}

static void semantic_resolve_function_reference(
    psx_semantic_context_t *semantic_context,
    node_funcref_t *reference,
    const token_t *fallback_diag_tok) {
  if (!reference) return;
  ag_diagnostic_context_t *diagnostics =
      semantic_diagnostics(semantic_context);
  const psx_type_t *source_type = ps_node_get_type((node_t *)reference);
  const psx_type_t *type = source_type && source_type->kind == PSX_TYPE_FUNCTION
      ? psx_resolve_function_reference_type(
            semantic_context, source_type)
      : NULL;
  if (!type && ps_type_callable_function(source_type))
    type = ps_type_clone_in(
        ps_ctx_arena(semantic_context), source_type);
  if (!type) {
    ps_diag_ctx_in(diagnostics, reference->base.tok
                    ? reference->base.tok
                    : (token_t *)fallback_diag_tok,
                "funcref", "canonical function type is not bound");
  }
  semantic_bind_result_type((node_t *)reference, type);
}

static node_t *semantic_normalize_call_deref_chain(
    node_t *callee, const psx_semantic_traversal_t *traversal) {
  int deref_count = 0;
  node_t *bottom = callee;
  while (bottom &&
         (bottom->kind == ND_UNARY_DEREF ||
          bottom->kind == ND_DEREF)) {
    deref_count++;
    bottom = bottom->lhs;
  }
  if (deref_count == 0 || !bottom) return callee;

  semantic_transform_node(
      bottom, traversal);
  const psx_type_t *type = ps_node_get_type(bottom);
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
  psx_function_call_resolution_t resolution;
  psx_resolve_function_call_type(
      call->callee_type,
      call->callee ? ps_node_get_type(call->callee) : NULL,
      call->base.is_implicit_func_decl, &resolution);
  if (resolution.status == PSX_FUNCTION_CALL_RESOLUTION_OK) {
    if (resolution.function_type) {
      const psx_semantic_type_table_t *types =
          ps_ctx_semantic_type_table_in(semantic_context);
      psx_qual_type_t callee_type = call->callee
          ? ps_node_qual_type(call->callee)
          : (psx_qual_type_t){0};
      if (callee_type.type_id == PSX_TYPE_ID_INVALID) {
        callee_type = ps_ctx_intern_qual_type_in(
            semantic_context,
            call->callee ? ps_node_get_type(call->callee)
                         : call->callee_type);
      }
      psx_qual_type_t function_type =
          psx_semantic_type_table_callable_function(types, callee_type);
      if (function_type.type_id == PSX_TYPE_ID_INVALID) {
        function_type = ps_ctx_intern_qual_type_in(
            semantic_context, resolution.function_type);
      }
      const psx_type_t *canonical_function =
          psx_semantic_type_table_lookup(types, function_type.type_id);
      if (canonical_function &&
          canonical_function->kind == PSX_TYPE_FUNCTION) {
        call->callee_type = canonical_function;
      }
      psx_qual_type_t return_type = psx_semantic_type_table_base(
          types, function_type.type_id);
      const psx_type_t *canonical_return =
          psx_semantic_type_table_lookup(types, return_type.type_id);
      if (canonical_return) {
        ps_node_bind_qual_type(
            (node_t *)call, canonical_return, return_type);
      } else {
        semantic_bind_result_type(
            (node_t *)call,
            ps_type_function_return_type(call->callee_type));
      }
      return;
    }
    if (call->base.is_implicit_func_decl) {
      semantic_bind_result_type(
          (node_t *)call,
          ps_type_new_integer_kind_in(
              ps_ctx_arena(semantic_context),
              PSX_INTEGER_KIND_INT, 0, 0));
      return;
    }
  }
  ps_diag_ctx_in(diagnostics, call->base.tok
                  ? call->base.tok
                  : (token_t *)fallback_diag_tok,
              "funcall", "canonical callable type is not bound");
}

static const psx_type_t *semantic_resolve_type_name_ref(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_type_name_ref_t *type_name) {
  return psx_resolve_bound_type_name_ref_in_contexts(
      semantic_context, global_registry, local_registry, type_name);
}

static void semantic_resolve_source_cast(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    node_source_cast_t *cast) {
  if (!cast || !cast->base.is_source_cast) return;
  semantic_bind_result_type(
      (node_t *)cast,
      ps_type_clone_in(
          ps_ctx_arena(semantic_context),
          semantic_resolve_type_name_ref(
          semantic_context, global_registry, local_registry,
          &cast->type_name)));
}

static void semantic_resolve_compound_literal(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    node_compound_literal_t *compound) {
  if (!compound) return;
  const psx_type_t *object_type = compound->type_name.resolved_type;
  if (!object_type) {
    psx_type_t *resolved = ps_type_clone_in(
        ps_ctx_arena(semantic_context),
        semantic_resolve_type_name_ref(
            semantic_context, global_registry, local_registry,
            &compound->type_name));
    ps_ctx_bind_record_ids_in(
        semantic_context, resolved);
    compound->type_name.resolved_type = resolved;
    object_type = resolved;
  }
  const psx_type_t *result = ps_type_clone_in(
      ps_ctx_arena(semantic_context), object_type);
  if (compound->requires_addressable_object)
    result = psx_resolve_address_result_type(
        semantic_context, &(node_t){.type = result});
  semantic_bind_result_type((node_t *)compound, result);
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
      local_registry, selection->control, NULL);
  token_t *tok = selection->base.tok
                     ? selection->base.tok
                     : (token_t *)fallback_diag_tok;
  selection->base.type = NULL;
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
      ps_diag_ctx_in(diagnostics, conflict_tok, "generic",
                  "_Generic に default association を複数指定できません (C11 6.5.1.1p2)");
      return;
    case PSX_GENERIC_SELECTION_RESOLUTION_DUPLICATE_COMPATIBLE_TYPE:
      ps_diag_ctx_in(diagnostics, conflict_tok, "generic",
                  "_Generic に互換な型associationを複数指定できません (C11 6.5.1.1p2)");
      return;
    case PSX_GENERIC_SELECTION_RESOLUTION_NO_MATCH:
      ps_diag_ctx_in(diagnostics, tok, "generic", "%s",
                  diag_message_for_in(diagnostics, DIAG_ERR_PARSER_GENERIC_NO_MATCH));
      return;
    case PSX_GENERIC_SELECTION_RESOLUTION_TYPE_UNRESOLVED:
      ps_diag_ctx_in(diagnostics, conflict_tok, "generic",
                  "canonical generic association/result type is not bound");
      return;
    case PSX_GENERIC_SELECTION_RESOLUTION_OK:
      break;
  }
  selection->selected_index = resolution.selected_index;
  semantic_bind_result_type(
      (node_t *)selection,
      ps_type_clone_in(
          ps_ctx_arena(semantic_context), ps_node_get_type(
          selection->associations[resolution.selected_index].expression)));
}

static void semantic_mark_usage_evaluated(node_t *node) {
  if (!node) return;
  if (node->records_lvar_usage) node->lvar_usage_unevaluated = 0;
  switch (node->kind) {
    case ND_BLOCK:
      for (node_t **body = ((node_block_t *)node)->body;
           body && *body; body++) {
        semantic_mark_usage_evaluated(*body);
      }
      return;
    case ND_FUNCALL: {
      node_function_call_t *call = (node_function_call_t *)node;
      semantic_mark_usage_evaluated(call->callee);
      for (int i = 0; i < call->argument_count; i++)
        semantic_mark_usage_evaluated(call->arguments[i]);
      return;
    }
    case ND_GENERIC_SELECTION: {
      node_generic_selection_t *selection =
          (node_generic_selection_t *)node;
      int selected = selection->selected_index;
      if (selected >= 0 && selected < selection->association_count) {
        semantic_mark_usage_evaluated(
            selection->associations[selected].expression);
      }
      return;
    }
    case ND_IF:
    case ND_FOR:
    case ND_TERNARY: {
      node_ctrl_t *control = (node_ctrl_t *)node;
      semantic_mark_usage_evaluated(control->init);
      semantic_mark_usage_evaluated(node->lhs);
      semantic_mark_usage_evaluated(node->rhs);
      semantic_mark_usage_evaluated(control->inc);
      semantic_mark_usage_evaluated(control->els);
      return;
    }
    default:
      semantic_mark_usage_evaluated(node->lhs);
      semantic_mark_usage_evaluated(node->rhs);
      return;
  }
}

static void semantic_mark_sizeof_indices_evaluated(node_t *operand) {
  if (!operand || operand->kind != ND_SUBSCRIPT) return;
  semantic_mark_sizeof_indices_evaluated(operand->lhs);
  semantic_mark_usage_evaluated(operand->rhs);
}

static void semantic_resolve_sizeof_query(
    node_sizeof_query_t *query,
    const psx_semantic_traversal_t *traversal) {
  if (!query) return;
  ag_diagnostic_context_t *diagnostics =
      semantic_diagnostics(traversal->semantic_context);
  const token_t *fallback_diag_tok = traversal->fallback_diag_tok;
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
      ps_ctx_record_unsupported_gnu_extension_warning_in(
          traversal->semantic_context,
          bound && bound->tok ? bound->tok : query->base.tok,
          "zero-length array");
    }
  }
  if (resolution.usage_root)
    psx_collect_lvar_usage_events_in(
        traversal->local_registry, resolution.usage_root, NULL);
  if (resolution.evaluates_vla_operand)
    semantic_mark_sizeof_indices_evaluated(query->operand);
  if (query->runtime_size_slot != 0)
    return;
}

static void semantic_transform_node(
    node_t *node, const psx_semantic_traversal_t *traversal) {
  if (!node) return;
  node_function_definition_t *current_func = traversal->current_func;
  const token_t *fallback_diag_tok = traversal->fallback_diag_tok;
  ag_diagnostic_context_t *diagnostics =
      semantic_diagnostics(traversal->semantic_context);

  switch (node->kind) {
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
          node, ps_type_clone_in(
                    ps_ctx_arena(traversal->semantic_context),
                    ps_node_get_type(node->lhs)));
      semantic_validate_assignment(
          traversal->semantic_context, node, diagnostics,
          fallback_diag_tok);
      break;
    }
    case ND_RETURN:
      semantic_transform_return(
          diagnostics, node, current_func, fallback_diag_tok);
      semantic_transform_node(node->lhs, traversal);
      break;
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
          traversal->semantic_context,
          (node_funcref_t *)node, fallback_diag_tok);
      break;
    case ND_VA_ARG_AREA:
      if (!node->type)
        semantic_bind_result_type(
            node, ps_type_new_pointer_in(
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
    case ND_UNARY_NEGATE:
      semantic_transform_node(node->lhs, traversal);
      semantic_resolve_arithmetic_unary(
          traversal->semantic_context,
          node, "単項 -", fallback_diag_tok);
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
      break;
    }
    case ND_ALIGNOF_QUERY:
      psx_resolve_alignof_query_in_contexts(
          traversal->semantic_context, traversal->global_registry,
          traversal->local_registry,
          (node_alignof_query_t *)node);
      break;
    case ND_CAST:
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
    case ND_ADDR:
      semantic_transform_node(node->lhs, traversal);
      if (!semantic_bind_address_result_type(
              traversal->semantic_context, node, node->lhs) &&
          !node->type)
        semantic_bind_result_type(
            node, psx_resolve_address_result_type(
                      traversal->semantic_context, node->lhs));
      if (node->is_explicit_addr_expr &&
          ps_node_bitfield_width(node->lhs) > 0) {
        ps_diag_ctx_in(diagnostics, node->tok
                        ? node->tok
                        : (token_t *)fallback_diag_tok,
                    "addr",
                    "ビットフィールドのアドレスは取得できません (C11 6.5.3.2p1)");
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
    case ND_LOGAND:
    case ND_LOGOR:
      semantic_transform_node(node->lhs, traversal);
      semantic_transform_node(node->rhs, traversal);
      if (!ps_node_get_type(node)) {
        semantic_bind_result_type(
            node, psx_resolve_binary_result_type(
                      traversal->semantic_context,
                      node->kind, node->lhs, node->rhs));
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
        semantic_bind_result_type(
            node, psx_resolve_conditional_result_type(
                      traversal->semantic_context,
                      node->rhs, ctrl->els));
      break;
    }
    case ND_STMT_EXPR: {
      semantic_transform_node(node->lhs, traversal);
      semantic_transform_node(node->rhs, traversal);
      semantic_bind_result_type(
          node, psx_resolve_sequence_result_type(
                    traversal->semantic_context, node->rhs));
      break;
    }
    default:
      semantic_transform_node(node->lhs, traversal);
      semantic_transform_node(node->rhs, traversal);
      semantic_validate_assignment(
          traversal->semantic_context, node, diagnostics,
          fallback_diag_tok);
      if (node->kind == ND_ASSIGN)
        ps_node_bind_type(node, ps_node_get_type(node->lhs));
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
