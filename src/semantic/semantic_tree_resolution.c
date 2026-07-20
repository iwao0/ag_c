#include "semantic_tree_resolution.h"
#include "../diag/diag.h"
#include "../parser/diag.h"
#include "../parser/function_definition_syntax.h"
#include "../parser/semantic_ctx.h"
#include "../type_system/type_shape.h"
#include "syntax_typed_hir_resolution.h"
#include "typed_hir_diagnostics.h"
#include "typed_hir_materialization.h"

static const char *direct_incdec_operator_name(int node_kind) {
  return node_kind == ND_PRE_INC || node_kind == ND_POST_INC
             ? "++"
         : node_kind == ND_PRE_DEC || node_kind == ND_POST_DEC
             ? "--"
             : NULL;
}

static const char *direct_control_statement_name(int node_kind) {
  switch (node_kind) {
    case ND_IF: return "if";
    case ND_WHILE: return "while";
    case ND_DO_WHILE: return "do-while";
    case ND_FOR: return "for";
    default: return NULL;
  }
}

static token_kind_t aggregate_token_kind(psx_type_kind_t type_kind) {
  return type_kind == PSX_TYPE_STRUCT ? TK_STRUCT
       : type_kind == PSX_TYPE_UNION ? TK_UNION
                                     : TK_EOF;
}

static int diagnose_direct_syntax_rejection(
    psx_semantic_context_t *semantic_context,
    const psx_resolved_hir_build_failure_t *failure,
    const token_t *fallback_diag_tok) {
  if (!semantic_context || !failure) return 0;
  ag_diagnostic_context_t *diagnostics =
      ps_ctx_diagnostics(semantic_context);
  token_t *token = (token_t *)(failure->source_token
                                   ? failure->source_token
                                   : fallback_diag_tok);
  switch (failure->rejection) {
    case PSX_SYNTAX_TYPED_HIR_REJECTION_DUPLICATE_LABEL:
      if (!failure->source_name || failure->source_name_length <= 0)
        return 0;
      ps_diag_duplicate_with_name_in(
          diagnostics, token,
          diag_text_for_in(diagnostics, DIAG_TEXT_LABEL),
          failure->source_name, failure->source_name_length);
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_UNDEFINED_GOTO:
      if (!failure->source_name || failure->source_name_length <= 0)
        return 0;
      ps_diag_ctx_in(
          diagnostics, token, "goto",
          diag_message_for_in(
              diagnostics, DIAG_ERR_PARSER_GOTO_LABEL_UNDEFINED),
          failure->source_name_length, failure->source_name);
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_BREAK_OUTSIDE_LOOP_OR_SWITCH:
      ps_diag_only_in_context(
          diagnostics, token,
          diag_text_for_in(diagnostics, DIAG_TEXT_BREAK),
          diag_text_for_in(
              diagnostics, DIAG_TEXT_LOOP_OR_SWITCH_SCOPE));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_CONTINUE_OUTSIDE_LOOP:
      ps_diag_only_in_context(
          diagnostics, token,
          diag_text_for_in(diagnostics, DIAG_TEXT_CONTINUE),
          diag_text_for_in(diagnostics, DIAG_TEXT_LOOP_SCOPE));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_CASE_OUTSIDE_SWITCH:
      ps_diag_only_in_context(
          diagnostics, token,
          diag_text_for_in(diagnostics, DIAG_TEXT_CASE),
          diag_text_for_in(diagnostics, DIAG_TEXT_SWITCH_SCOPE));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_DEFAULT_OUTSIDE_SWITCH:
      ps_diag_only_in_context(
          diagnostics, token,
          diag_text_for_in(diagnostics, DIAG_TEXT_DEFAULT),
          diag_text_for_in(diagnostics, DIAG_TEXT_SWITCH_SCOPE));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_DUPLICATE_CASE:
      diag_emit_tokf_in(
          diagnostics, DIAG_ERR_PARSER_SWITCH_DUPLICATE_CASE, token,
          diag_message_for_in(
              diagnostics, DIAG_ERR_PARSER_SWITCH_DUPLICATE_CASE),
          failure->source_integer_value);
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_DUPLICATE_DEFAULT:
      diag_emit_tokf_in(
          diagnostics, DIAG_ERR_PARSER_SWITCH_DUPLICATE_DEFAULT, token,
          "%s", diag_message_for_in(
                    diagnostics,
                    DIAG_ERR_PARSER_SWITCH_DUPLICATE_DEFAULT));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_RETURN_VALUE_REQUIRED:
      diag_emit_tokf_in(
          diagnostics, DIAG_ERR_PARSER_INVALID_CONTEXT, token, "%s",
          diag_message_for_in(
              diagnostics,
              DIAG_ERR_PARSER_RETURN_VALUE_REQUIRED_NONVOID));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_RETURN_VALUE_FORBIDDEN:
      diag_emit_tokf_in(
          diagnostics, DIAG_ERR_PARSER_INVALID_CONTEXT, token, "%s",
          diag_message_for_in(
              diagnostics,
              DIAG_ERR_PARSER_RETURN_VALUE_FORBIDDEN_VOID));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_STATIC_ASSERT_NOT_CONSTANT:
      diag_emit_tokf_in(
          diagnostics, DIAG_ERR_PARSER_STATIC_ASSERT_COND_NOT_CONST,
          token, "%s",
          diag_message_for_in(
              diagnostics,
              DIAG_ERR_PARSER_STATIC_ASSERT_COND_NOT_CONST));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_STATIC_ASSERT_FAILED:
      diag_emit_tokf_in(
          diagnostics, DIAG_ERR_PARSER_STATIC_ASSERT_FAILED, token,
          "%s", diag_message_for_in(
                    diagnostics, DIAG_ERR_PARSER_STATIC_ASSERT_FAILED));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_CASE_NOT_INTEGER_CONSTANT:
      ps_diag_ctx_in(
          diagnostics, token, "case",
          diag_message_for_in(
              diagnostics,
              DIAG_ERR_PARSER_NONNEG_CONSTEXPR_REQUIRED),
          "case label");
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_UNDEFINED_IDENTIFIER:
      if (!failure->source_name || failure->source_name_length <= 0)
        return 0;
      psx_diag_undefined_with_name_in(
          diagnostics, token, "variable", failure->source_name,
          failure->source_name_length);
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_DOT_BASE_NOT_AGGREGATE:
      diag_emit_tokf_in(
          diagnostics, DIAG_ERR_PARSER_INVALID_CONTEXT, token, "%s",
          diag_message_for_in(
              diagnostics, DIAG_ERR_PARSER_DOT_LHS_REQUIRES_STRUCT));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_ARROW_BASE_NOT_AGGREGATE_POINTER:
      diag_emit_tokf_in(
          diagnostics, DIAG_ERR_PARSER_INVALID_CONTEXT, token, "%s",
          diag_message_for_in(
              diagnostics,
              DIAG_ERR_PARSER_ARROW_LHS_REQUIRES_STRUCT_PTR));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_MEMBER_NOT_FOUND:
      if (!failure->source_name || failure->source_name_length <= 0)
        return 0;
      ps_diag_ctx_in(
          diagnostics, token, "member",
          diag_message_for_in(
              diagnostics, DIAG_ERR_PARSER_MEMBER_NOT_FOUND),
          failure->source_name_length, failure->source_name);
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_DEREF_REQUIRES_POINTER:
      ps_diag_ctx_in(
          diagnostics, token, "deref",
          "deref のオペランドはポインタ型でなければなりません "
          "(C11 6.5.3.2p2)");
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_DEREF_VOID_POINTER:
      ps_diag_ctx_in(
          diagnostics, token, "deref",
          "void* の deref はできません — キャストが必要です "
          "(C11 6.5.3.2)");
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_INVALID_SUBSCRIPT_OPERANDS:
      ps_diag_ctx_in(
          diagnostics, token, "subscript",
          "サブスクリプトの両辺ともポインタ/配列ではありません "
          "(C11 6.5.2.1p1)");
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_ARITHMETIC_UNARY_REQUIRES_ARITHMETIC: {
      const char *operator_name =
          failure->source_node_kind == ND_UNARY_PLUS
              ? "単項 +"
              : failure->source_node_kind == ND_UNARY_NEGATE
                    ? "単項 -"
              : failure->source_node_kind == ND_CREAL
                    ? "__real__"
                    : failure->source_node_kind == ND_CIMAG
                          ? "__imag__"
                          : NULL;
      if (!operator_name) return 0;
      ps_diag_ctx_in(
          diagnostics, token, "unary",
          "%s のオペランドは算術型でなければなりません",
          operator_name);
      return 1;
    }
    case PSX_SYNTAX_TYPED_HIR_REJECTION_LOGICAL_NOT_REQUIRES_SCALAR:
      ps_diag_ctx_in(
          diagnostics, token, "unary",
          "単項 ! のオペランドはスカラー型でなければなりません");
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_BITWISE_NOT_REQUIRES_INTEGER:
      ps_diag_ctx_in(
          diagnostics, token, "unary",
          "単項 ~ のオペランドは整数型でなければなりません");
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_INCDEC_REQUIRES_LVALUE: {
      const char *operator_name = direct_incdec_operator_name(
          failure->source_node_kind);
      if (!operator_name) return 0;
      diag_emit_tokf_in(
          diagnostics, DIAG_ERR_PARSER_LVALUE_REQUIRED, token,
          diag_message_for_in(
              diagnostics, DIAG_ERR_PARSER_LVALUE_REQUIRED),
          operator_name);
      return 1;
    }
    case PSX_SYNTAX_TYPED_HIR_REJECTION_INCDEC_CONST_OPERAND:
      diag_emit_tokf_in(
          diagnostics, DIAG_ERR_PARSER_CONST_ASSIGNMENT, token,
          "%s", diag_message_for_in(
                    diagnostics,
                    DIAG_ERR_PARSER_CONST_ASSIGNMENT));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_INCDEC_INVALID_OPERAND_TYPE: {
      const char *operator_name = direct_incdec_operator_name(
          failure->source_node_kind);
      if (!operator_name) return 0;
      ps_diag_ctx_in(
          diagnostics, token, "incdec",
          "%s のオペランドは実数型またはポインタ型でなければなりません",
          operator_name);
      return 1;
    }
    case PSX_SYNTAX_TYPED_HIR_REJECTION_ASSIGN_REQUIRES_LVALUE:
      diag_emit_tokf_in(
          diagnostics, DIAG_ERR_PARSER_LVALUE_REQUIRED, token,
          diag_message_for_in(
              diagnostics, DIAG_ERR_PARSER_LVALUE_REQUIRED),
          "=");
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_ASSIGN_CONST_TARGET:
      diag_emit_tokf_in(
          diagnostics, DIAG_ERR_PARSER_CONST_ASSIGNMENT, token,
          "%s", diag_message_for_in(
                    diagnostics,
                    DIAG_ERR_PARSER_CONST_ASSIGNMENT));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_ASSIGN_FUNCTION_TARGET:
      diag_emit_tokf_in(
          diagnostics, DIAG_ERR_PARSER_ASSIGN_FUNCTION_TARGET,
          token, "%s", diag_message_for_in(
                           diagnostics,
                           DIAG_ERR_PARSER_ASSIGN_FUNCTION_TARGET));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_ASSIGN_TARGET_NOT_MODIFIABLE:
      diag_emit_tokf_in(
          diagnostics,
          DIAG_ERR_PARSER_ASSIGN_TARGET_NOT_MODIFIABLE, token,
          "%s", diag_message_for_in(
                    diagnostics,
                    DIAG_ERR_PARSER_ASSIGN_TARGET_NOT_MODIFIABLE));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_ASSIGN_DISCARDS_QUALIFIERS:
      diag_emit_tokf_in(
          diagnostics, DIAG_ERR_PARSER_CONST_QUAL_DISCARD, token,
          "%s", diag_message_for_in(
                    diagnostics,
                    DIAG_ERR_PARSER_CONST_QUAL_DISCARD));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_ASSIGN_INCOMPATIBLE_TYPES:
      diag_emit_tokf_in(
          diagnostics, DIAG_ERR_PARSER_ASSIGN_TYPES_INCOMPATIBLE,
          token, "%s", diag_message_for_in(
                           diagnostics,
                           DIAG_ERR_PARSER_ASSIGN_TYPES_INCOMPATIBLE));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_CONDITIONAL_CONDITION_NOT_SCALAR:
      diag_emit_tokf_in(
          diagnostics,
          DIAG_ERR_PARSER_CONDITIONAL_CONDITION_NOT_SCALAR,
          token, "%s", diag_message_for_in(
                           diagnostics,
                           DIAG_ERR_PARSER_CONDITIONAL_CONDITION_NOT_SCALAR));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_CONDITIONAL_BRANCH_TYPES_INCOMPATIBLE:
      diag_emit_tokf_in(
          diagnostics,
          DIAG_ERR_PARSER_CONDITIONAL_BRANCH_TYPES_INCOMPATIBLE,
          token, "%s", diag_message_for_in(
                           diagnostics,
                           DIAG_ERR_PARSER_CONDITIONAL_BRANCH_TYPES_INCOMPATIBLE));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_CALL_NOT_CALLABLE:
      diag_emit_tokf_in(
          diagnostics, DIAG_ERR_PARSER_CALL_NOT_CALLABLE, token,
          "%s", diag_message_for_in(
                    diagnostics, DIAG_ERR_PARSER_CALL_NOT_CALLABLE));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_CALL_ARGUMENT_COUNT_MISMATCH:
      diag_emit_tokf_in(
          diagnostics,
          DIAG_ERR_PARSER_CALL_ARGUMENT_COUNT_MISMATCH, token,
          "%s", diag_message_for_in(
                    diagnostics,
                    DIAG_ERR_PARSER_CALL_ARGUMENT_COUNT_MISMATCH));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_GENERIC_DUPLICATE_DEFAULT:
      diag_emit_tokf_in(
          diagnostics, DIAG_ERR_PARSER_GENERIC_DUPLICATE_DEFAULT,
          token, "%s", diag_message_for_in(
                           diagnostics,
                           DIAG_ERR_PARSER_GENERIC_DUPLICATE_DEFAULT));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_GENERIC_DUPLICATE_COMPATIBLE_TYPE:
      diag_emit_tokf_in(
          diagnostics,
          DIAG_ERR_PARSER_GENERIC_DUPLICATE_COMPATIBLE_TYPE, token,
          "%s", diag_message_for_in(
                    diagnostics,
                    DIAG_ERR_PARSER_GENERIC_DUPLICATE_COMPATIBLE_TYPE));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_GENERIC_NO_MATCH:
      diag_emit_tokf_in(
          diagnostics, DIAG_ERR_PARSER_GENERIC_NO_MATCH, token,
          "%s", diag_message_for_in(
                    diagnostics, DIAG_ERR_PARSER_GENERIC_NO_MATCH));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_CONTROL_CONDITION_NOT_SCALAR: {
      const char *statement = direct_control_statement_name(
          failure->source_node_kind);
      if (!statement) return 0;
      diag_emit_tokf_in(
          diagnostics, DIAG_ERR_PARSER_CONTROL_CONDITION_NOT_SCALAR,
          token, diag_message_for_in(
                     diagnostics,
                     DIAG_ERR_PARSER_CONTROL_CONDITION_NOT_SCALAR),
          statement);
      return 1;
    }
    case PSX_SYNTAX_TYPED_HIR_REJECTION_SWITCH_CONDITION_NOT_INTEGER:
      diag_emit_tokf_in(
          diagnostics, DIAG_ERR_PARSER_SWITCH_CONDITION_NOT_INTEGER,
          token, "%s", diag_message_for_in(
                         diagnostics,
                         DIAG_ERR_PARSER_SWITCH_CONDITION_NOT_INTEGER));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_RETURN_TYPES_INCOMPATIBLE:
      diag_emit_tokf_in(
          diagnostics, DIAG_ERR_PARSER_RETURN_TYPES_INCOMPATIBLE,
          token, "%s", diag_message_for_in(
                         diagnostics,
                         DIAG_ERR_PARSER_RETURN_TYPES_INCOMPATIBLE));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_RETURN_DISCARDS_QUALIFIERS:
      diag_emit_tokf_in(
          diagnostics, DIAG_ERR_PARSER_RETURN_DISCARDS_QUALIFIERS,
          token, "%s", diag_message_for_in(
                         diagnostics,
                         DIAG_ERR_PARSER_RETURN_DISCARDS_QUALIFIERS));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_CAST_AGGREGATE_TYPE_MISMATCH:
      ps_diag_ctx_in(
          diagnostics, token, "cast",
          diag_message_for_in(
              diagnostics,
              DIAG_ERR_PARSER_CAST_NONSCALAR_TYPE_MISMATCH),
          ps_ctx_tag_kind_spelling(
              aggregate_token_kind(
                  (psx_type_kind_t)failure->source_integer_value)));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_CAST_STRUCT_EXTENSION_DISABLED:
      ps_diag_ctx_in(
          diagnostics, token, "cast", "%s",
          diag_message_for_in(
              diagnostics,
              DIAG_ERR_PARSER_CAST_STRUCT_SCALAR_POINTER_DISABLED));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_CAST_UNION_EXTENSION_DISABLED:
      ps_diag_ctx_in(
          diagnostics, token, "cast", "%s",
          diag_message_for_in(
              diagnostics,
              DIAG_ERR_PARSER_CAST_UNION_SCALAR_POINTER_DISABLED));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_CAST_AGGREGATE_UNSUPPORTED:
      ps_diag_ctx_in(
          diagnostics, token, "cast",
          diag_message_for_in(
              diagnostics,
              DIAG_ERR_PARSER_CAST_NONSCALAR_UNSUPPORTED),
          ps_ctx_tag_kind_spelling(
              aggregate_token_kind(
                  (psx_type_kind_t)failure->source_integer_value)));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_CAST_AGGREGATE_MEMBER_NOT_FOUND:
      ps_diag_ctx_in(
          diagnostics, token, "cast", "%s",
          diag_message_for_in(
              diagnostics,
              DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_CAST_TARGET_NOT_VOID_OR_SCALAR:
      ps_diag_ctx_in(
          diagnostics, token, "cast", "%s",
          diag_message_for_in(
              diagnostics,
              DIAG_ERR_PARSER_CAST_TARGET_NOT_VOID_OR_SCALAR));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_CAST_OPERAND_NOT_SCALAR:
      ps_diag_ctx_in(
          diagnostics, token, "cast", "%s",
          diag_message_for_in(
              diagnostics,
              DIAG_ERR_PARSER_CAST_OPERAND_NOT_SCALAR));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_ADDRESS_REQUIRES_ADDRESSABLE_VALUE:
      diag_emit_tokf_in(
          diagnostics,
          DIAG_ERR_PARSER_ADDRESS_REQUIRES_ADDRESSABLE_VALUE,
          token, "%s", diag_message_for_in(
                         diagnostics,
                         DIAG_ERR_PARSER_ADDRESS_REQUIRES_ADDRESSABLE_VALUE));
      return 1;
    case PSX_SYNTAX_TYPED_HIR_REJECTION_ADDRESS_OF_BITFIELD:
      diag_emit_tokf_in(
          diagnostics, DIAG_ERR_PARSER_ADDRESS_OF_BITFIELD,
          token, "%s", diag_message_for_in(
                         diagnostics,
                         DIAG_ERR_PARSER_ADDRESS_OF_BITFIELD));
      return 1;
    default:
      return 0;
  }
}

static const psx_typed_hir_tree_t *
resolve_parsed_function_typed_hir_from_syntax_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const psx_parsed_function_definition_t *syntax_function,
    const token_t *fallback_diag_tok) {
  const psx_typed_hir_tree_t *direct_typed_hir = NULL;
  psx_resolved_hir_build_failure_t direct_failure;
  psx_syntax_typed_hir_resolution_status_t direct_status =
      psx_resolve_syntax_function_direct_to_typed_hir_in_contexts(
          semantic_context, global_registry, local_registry,
          lowering_context, options, syntax_function, &direct_typed_hir,
          &direct_failure);
  if (direct_status == PSX_SYNTAX_TYPED_HIR_RESOLVED) {
    psx_emit_typed_hir_warnings(
        semantic_context, direct_typed_hir, fallback_diag_tok);
    return direct_typed_hir;
  }
  if (direct_status == PSX_SYNTAX_TYPED_HIR_FAILED) {
    ag_diagnostic_context_t *diagnostics =
        ps_ctx_diagnostics(semantic_context);
    diag_emit_internalf_in(
        diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED,
        "%s: direct function Syntax to Typed HIR resolution failed "
        "(status %d, node kind %d)",
        diag_message_for_in(
            diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED),
        (int)direct_failure.status,
        direct_failure.source_node_kind);
    return NULL;
  }
  if (diagnose_direct_syntax_rejection(
          semantic_context, &direct_failure, fallback_diag_tok))
    return NULL;
  ag_diagnostic_context_t *diagnostics =
      ps_ctx_diagnostics(semantic_context);
  diag_emit_internalf_in(
      diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED,
      "%s: direct function Syntax rejection has no diagnostic "
      "(rejection %d, node kind %d, function '%.*s')",
      diag_message_for_in(
          diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED),
      (int)direct_failure.rejection,
      direct_failure.source_node_kind,
      syntax_function->declarator.identifier->len,
      syntax_function->declarator.identifier->str);
  return NULL;
}

static const psx_typed_hir_tree_t *
resolve_nonfunction_typed_hir_from_syntax_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const node_t *syntax, const token_t *fallback_diag_tok,
    int is_initializer) {
  if (!semantic_context || !syntax) return NULL;
  const psx_typed_hir_tree_t *direct_typed_hir = NULL;
  psx_resolved_hir_build_failure_t direct_failure;
  psx_syntax_typed_hir_resolution_status_t direct_status =
      is_initializer
          ? psx_resolve_syntax_initializer_direct_to_typed_hir_with_lowering_in_contexts(
                semantic_context, global_registry, local_registry,
                lowering_context, options, syntax,
                &direct_typed_hir, &direct_failure)
          : psx_resolve_syntax_expression_direct_to_typed_hir_with_lowering_in_contexts(
                semantic_context, global_registry, local_registry,
                lowering_context, options, syntax,
                &direct_typed_hir, &direct_failure);
  if (direct_status == PSX_SYNTAX_TYPED_HIR_RESOLVED)
    return direct_typed_hir;
  if (direct_status == PSX_SYNTAX_TYPED_HIR_FAILED) {
    ag_diagnostic_context_t *diagnostics =
        ps_ctx_diagnostics(semantic_context);
    diag_emit_internalf_in(
        diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED,
        "%s: direct Syntax %s to Typed HIR resolution failed "
        "(status %d, node kind %d)",
        diag_message_for_in(
            diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED),
        is_initializer ? "initializer" : "expression",
        (int)direct_failure.status,
        direct_failure.source_node_kind);
    return NULL;
  }
  if (diagnose_direct_syntax_rejection(
          semantic_context, &direct_failure, fallback_diag_tok))
    return NULL;
  ag_diagnostic_context_t *diagnostics =
      ps_ctx_diagnostics(semantic_context);
  diag_emit_internalf_in(
      diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED,
      "%s: direct Syntax %s rejection has no diagnostic "
      "(rejection %d, node kind %d)",
      diag_message_for_in(
          diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED),
      is_initializer ? "initializer" : "expression",
      (int)direct_failure.rejection,
      direct_failure.source_node_kind);
  return NULL;
}

static int emit_resolved_typed_hir(
    psx_semantic_context_t *semantic_context,
    const psx_typed_hir_tree_t *typed_hir,
    const token_t *fallback_diag_tok, const char *subject,
    psx_hir_module_t *hir, psx_hir_node_id_t *hir_root) {
  if (hir_root) *hir_root = PSX_HIR_NODE_ID_INVALID;
  if (!semantic_context || !typed_hir || !subject || !hir || !hir_root)
    return 0;
  psx_resolved_hir_build_failure_t failure;
  *hir_root = psx_typed_hir_tree_emit(hir, typed_hir, &failure);
  if (*hir_root != PSX_HIR_NODE_ID_INVALID) return 1;

  ag_diagnostic_context_t *diagnostics =
      ps_ctx_diagnostics(semantic_context);
  if (fallback_diag_tok) {
    diag_emit_tokf_in(
        diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED,
        fallback_diag_tok,
        "%s: %s Typed HIR emission failed (status %d, node kind %d)",
        diag_message_for_in(
            diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED),
        subject, (int)failure.status, failure.source_node_kind);
  }
  diag_emit_internalf_in(
      diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED,
      "%s: %s Typed HIR emission failed (status %d, node kind %d)",
      diag_message_for_in(
          diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED),
      subject, (int)failure.status, failure.source_node_kind);
  return 0;
}

int psx_resolve_parsed_function_hir_from_syntax_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const psx_parsed_function_definition_t *syntax_function,
    const token_t *fallback_diag_tok,
    psx_hir_module_t *hir, psx_hir_node_id_t *hir_root) {
  if (hir_root) *hir_root = PSX_HIR_NODE_ID_INVALID;
  if (!semantic_context || !syntax_function || !syntax_function->body ||
      !hir || !hir_root)
    return 0;
  const psx_typed_hir_tree_t *typed_hir =
      resolve_parsed_function_typed_hir_from_syntax_in_contexts(
          semantic_context, global_registry, local_registry,
          lowering_context, options, syntax_function,
          fallback_diag_tok);
  return emit_resolved_typed_hir(
      semantic_context, typed_hir, fallback_diag_tok,
      "function", hir, hir_root);
}

int psx_resolve_expression_hir_from_syntax_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const node_t *syntax_expression,
    const token_t *fallback_diag_tok,
    psx_hir_module_t *hir, psx_hir_node_id_t *hir_root) {
  if (hir_root) *hir_root = PSX_HIR_NODE_ID_INVALID;
  if (!semantic_context || !syntax_expression || !hir || !hir_root)
    return 0;
  const psx_typed_hir_tree_t *typed_hir =
      resolve_nonfunction_typed_hir_from_syntax_in_contexts(
          semantic_context, global_registry, local_registry,
          lowering_context, options, syntax_expression,
          fallback_diag_tok, 0);
  return emit_resolved_typed_hir(
      semantic_context, typed_hir, fallback_diag_tok,
      "expression", hir, hir_root);
}

int psx_resolve_initializer_hir_from_syntax_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const node_t *syntax_initializer,
    const token_t *fallback_diag_tok,
    psx_hir_module_t *hir, psx_hir_node_id_t *hir_root) {
  if (hir_root) *hir_root = PSX_HIR_NODE_ID_INVALID;
  if (!semantic_context || !syntax_initializer || !hir || !hir_root)
    return 0;
  const psx_typed_hir_tree_t *typed_hir =
      resolve_nonfunction_typed_hir_from_syntax_in_contexts(
          semantic_context, global_registry, local_registry,
          lowering_context, options, syntax_initializer,
          fallback_diag_tok, 1);
  return emit_resolved_typed_hir(
      semantic_context, typed_hir, fallback_diag_tok,
      "initializer", hir, hir_root);
}
