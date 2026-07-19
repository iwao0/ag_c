#include "semantic_tree_resolution.h"
#include "../diag/diag.h"
#include "../parser/diag.h"
#include "../parser/function_definition_syntax.h"
#include "../parser/semantic_ctx.h"
#include "legacy_syntax_diagnostics.h"
#include "syntax_typed_hir_resolution.h"
#include "typed_hir_diagnostics.h"

static const char *direct_incdec_operator_name(int node_kind) {
  return node_kind == ND_PRE_INC || node_kind == ND_POST_INC
             ? "++"
         : node_kind == ND_PRE_DEC || node_kind == ND_POST_DEC
             ? "--"
             : NULL;
}

static int diagnose_direct_function_rejection(
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
          failure->source_node_kind == ND_UNARY_NEGATE
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
    default:
      return 0;
  }
}

const psx_typed_hir_tree_t *
psx_resolve_parsed_function_typed_hir_from_syntax_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
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
  if (diagnose_direct_function_rejection(
          semantic_context, &direct_failure, fallback_diag_tok))
    return NULL;
  if (!psx_legacy_syntax_diagnostics_accept_function_in_contexts(
          semantic_context, global_registry, local_registry,
          runtime_context, lowering_context, options,
          syntax_function, fallback_diag_tok))
    return NULL;
  ag_diagnostic_context_t *diagnostics =
      ps_ctx_diagnostics(semantic_context);
  if (direct_failure.source_token) {
    diag_emit_tokf_in(
        diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED,
        direct_failure.source_token,
        "%s: valid function '%.*s' reached the compatibility diagnostic "
        "path (node kind %d)",
        diag_message_for_in(
            diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED),
        syntax_function->declarator.identifier->len,
        syntax_function->declarator.identifier->str,
        direct_failure.source_node_kind);
  }
  diag_emit_internalf_in(
      diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED,
      "%s: valid function '%.*s' reached the compatibility diagnostic "
      "path (node kind %d)",
      diag_message_for_in(
          diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED),
      syntax_function->declarator.identifier->len,
      syntax_function->declarator.identifier->str,
      direct_failure.source_node_kind);
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
  if (!psx_legacy_syntax_diagnostics_accept_nonfunction_in_contexts(
          semantic_context, global_registry, local_registry,
          lowering_context, options, syntax,
          fallback_diag_tok, is_initializer))
    return NULL;
  ag_diagnostic_context_t *diagnostics =
      ps_ctx_diagnostics(semantic_context);
  diag_emit_internalf_in(
      diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED,
      "%s: valid %s reached the compatibility diagnostic path",
      diag_message_for_in(
          diagnostics, DIAG_ERR_INTERNAL_INVARIANT_FAILED),
      is_initializer ? "initializer" : "expression");
  return NULL;
}

const psx_typed_hir_tree_t *
psx_resolve_expression_typed_hir_from_syntax_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const node_t *syntax_expression,
    const token_t *fallback_diag_tok) {
  return resolve_nonfunction_typed_hir_from_syntax_in_contexts(
      semantic_context, global_registry, local_registry,
      lowering_context, options, syntax_expression,
      fallback_diag_tok, 0);
}

const psx_typed_hir_tree_t *
psx_resolve_initializer_typed_hir_from_syntax_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const node_t *syntax_initializer,
    const token_t *fallback_diag_tok) {
  return resolve_nonfunction_typed_hir_from_syntax_in_contexts(
      semantic_context, global_registry, local_registry,
      lowering_context, options, syntax_initializer,
      fallback_diag_tok, 1);
}
