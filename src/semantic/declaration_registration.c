#include "declaration_registration.h"

#include "../diag/diag.h"
#include "../diag/error_catalog.h"
#include "../parser/diag.h"
#include "enum_constant_resolution.h"
#include "static_assert_resolution.h"
#include "syntax_typed_hir_resolution.h"
#include "typed_hir_materialization.h"
#include "typedef_declaration_resolution.h"
#include "../parser/semantic_ctx.h"
#include "../parser/global_registry.h"
#include "../parser/local_registry.h"
#include "../source_manager.h"

static void note_current_declaration_source(
    psx_semantic_context_t *semantic_context,
    psx_c_namespace_t name_space, const char *name, int name_len,
    token_t *token) {
  if (!semantic_context || !name || name_len <= 0 || !token) return;
  psx_scope_graph_t *graph = ps_ctx_scope_graph(semantic_context);
  psx_decl_id_t declaration_id = psx_scope_graph_lookup_in_scope(
      graph, psx_scope_graph_current_scope(graph),
      name_space, name, name_len);
  ag_source_manager_t *sources = diag_context_source_manager(
      ps_ctx_diagnostics(semantic_context));
  (void)psx_scope_graph_note_declaration_source(
      graph, declaration_id,
      ag_source_manager_name(sources, token->file_name_id),
      token->source_input, token->byte_offset, token->byte_length);
}

void psx_apply_parsed_typedef_declaration_in(
    psx_semantic_context_t *semantic_context,
    char *name, int name_len, psx_qual_type_t decl_qual_type,
    token_t *diag_tok) {
  if (!semantic_context) return;
  ag_diagnostic_context_t *diagnostics =
      ps_ctx_diagnostics(semantic_context);
  psx_typedef_declaration_resolution_t resolution;
  psx_resolve_typedef_declaration(
      &(psx_typedef_declaration_resolution_request_t){
          .semantic_context = semantic_context,
          .name = name,
          .name_len = name_len,
          .decl_qual_type = decl_qual_type,
      },
      &resolution);
  if (resolution.status == PSX_TYPEDEF_DECLARATION_OK) {
    if (resolution.created)
      note_current_declaration_source(
          semantic_context, PSX_NAMESPACE_ORDINARY,
          name, name_len, diag_tok);
    return;
  }
  if (resolution.status == PSX_TYPEDEF_DECLARATION_TYPE_CONFLICT) {
    ps_diag_ctx_in(diagnostics, diag_tok, "typedef",
                 "typedef名 '%.*s' の型が以前の宣言と異なります (C11 6.7p3)",
                 name_len, name);
  }
  if (resolution.status == PSX_TYPEDEF_DECLARATION_OBJECT_NAME_CONFLICT) {
    ps_diag_ctx_in(diagnostics, diag_tok, "typedef",
                 "'%.*s' はオブジェクトとして既に宣言されています (C11 6.7p4)",
                 name_len, name);
  }
  if (resolution.status == PSX_TYPEDEF_DECLARATION_FUNCTION_NAME_CONFLICT) {
    ps_diag_ctx_in(diagnostics, diag_tok, "typedef",
                 "'%.*s' は関数として既に宣言されています (C11 6.7p4)",
                 name_len, name);
  }
  if (resolution.status == PSX_TYPEDEF_DECLARATION_ENUM_NAME_CONFLICT) {
    ps_diag_ctx_in(diagnostics, diag_tok, "typedef",
                 "'%.*s' はenum定数として既に宣言されています (C11 6.7p4)",
                 name_len, name);
  }
  ps_diag_ctx_in(diagnostics, diag_tok, "typedef",
               "canonical typedef declaration resolution failed for '%.*s'",
               name_len, name);
}

void psx_apply_parsed_enum_constant_in(
    psx_semantic_context_t *semantic_context,
    char *name, int name_len, long long value, token_t *diag_tok) {
  if (!semantic_context) return;
  ag_diagnostic_context_t *diagnostics =
      ps_ctx_diagnostics(semantic_context);
  psx_enum_constant_resolution_t resolution;
  psx_resolve_enum_constant(
      &(psx_enum_constant_resolution_request_t){
          .semantic_context = semantic_context,
          .name = name,
          .name_len = name_len,
          .value = value,
      },
      &resolution);
  if (resolution.status == PSX_ENUM_CONSTANT_OK) {
    if (resolution.created)
      note_current_declaration_source(
          semantic_context, PSX_NAMESPACE_ORDINARY,
          name, name_len, diag_tok);
    return;
  }
  if (resolution.status == PSX_ENUM_CONSTANT_DUPLICATE) {
    ps_diag_duplicate_with_name_in(diagnostics,
        diag_tok, "enum constant", name, name_len);
  }
  if (resolution.status == PSX_ENUM_CONSTANT_OBJECT_NAME_CONFLICT) {
    ps_diag_ctx_in(diagnostics, diag_tok, "enum",
                 "'%.*s' はオブジェクトとして既に宣言されています (C11 6.7p4)",
                 name_len, name);
  }
  if (resolution.status == PSX_ENUM_CONSTANT_FUNCTION_NAME_CONFLICT) {
    ps_diag_ctx_in(diagnostics, diag_tok, "enum",
                 "'%.*s' は関数として既に宣言されています (C11 6.7p4)",
                 name_len, name);
  }
  if (resolution.status == PSX_ENUM_CONSTANT_TYPEDEF_NAME_CONFLICT) {
    ps_diag_ctx_in(diagnostics, diag_tok, "enum",
                 "'%.*s' はtypedef名として既に宣言されています (C11 6.7p4)",
                 name_len, name);
  }
  ps_diag_ctx_in(diagnostics, diag_tok, "enum",
               "canonical enum constant resolution failed for '%.*s'",
               name_len, name);
}

void psx_apply_parsed_tag_declaration_in(
    psx_semantic_context_t *semantic_context,
    token_kind_t kind, char *name, int name_len,
    psx_tag_declaration_mode_t mode, int member_count,
    token_t *diag_tok) {
  if (!semantic_context) return;
  ag_diagnostic_context_t *diagnostics =
      ps_ctx_diagnostics(semantic_context);
  psx_tag_declaration_resolution_t resolution;
  psx_resolve_tag_declaration(
      &(psx_tag_declaration_resolution_request_t){
          .semantic_context = semantic_context,
          .kind = kind,
          .name = name,
          .name_len = name_len,
          .mode = mode,
          .member_count = member_count,
      },
      &resolution);
  if (resolution.status == PSX_TAG_DECLARATION_OK) {
    if (resolution.registered)
      note_current_declaration_source(
          semantic_context, PSX_NAMESPACE_TAG,
          name, name_len, diag_tok);
    return;
  }
  if (resolution.status == PSX_TAG_DECLARATION_REDEFINITION) {
    ps_diag_ctx_in(diagnostics, diag_tok, "tag",
                 "タグ '%.*s' は同一スコープで再定義されています (C11 6.7.2)",
                 name_len, name);
  }
  if (resolution.status == PSX_TAG_DECLARATION_KIND_CONFLICT) {
    ps_diag_ctx_in(diagnostics, diag_tok, "tag",
                 "タグ '%.*s' は同一スコープで異なる種類として宣言されています (C11 6.7.2.3)",
                 name_len, name);
  }
  ps_diag_ctx_in(diagnostics, diag_tok, "tag",
               "canonical tag declaration resolution failed for '%.*s'",
               name_len, name);
}

int psx_apply_aggregate_member_declaration(
    psx_aggregate_layout_state_t *layout,
    const psx_aggregate_member_declaration_request_t *request,
    token_t *diag_tok) {
  if (!request || !request->semantic_context) return 0;
  ag_diagnostic_context_t *diagnostics =
      ps_ctx_diagnostics(request->semantic_context);
  psx_aggregate_member_declaration_resolution_t resolution;
  psx_resolve_aggregate_member_declaration(layout, request, &resolution);
  if (resolution.status == PSX_AGGREGATE_MEMBER_OK)
    return resolution.registered_member_count;
  if (resolution.status == PSX_AGGREGATE_MEMBER_MISSING_NAME) {
    ps_diag_missing_in(diagnostics, diag_tok, diag_text_for_in(diagnostics, DIAG_TEXT_MEMBER_NAME));
  }
  if (resolution.status == PSX_AGGREGATE_MEMBER_INCOMPLETE_TYPE) {
    ps_diag_ctx_in(diagnostics, diag_tok, "decl", "%s",
                 diag_message_for_in(diagnostics,
                     DIAG_ERR_PARSER_INCOMPLETE_MEMBER_FORBIDDEN));
  }
  if (resolution.status == PSX_AGGREGATE_MEMBER_FUNCTION_TYPE) {
    ps_diag_ctx_in(diagnostics, diag_tok, "decl", "%s",
                 diag_message_for_in(diagnostics,
                     DIAG_ERR_PARSER_FUNCTION_MEMBER_FORBIDDEN));
  }
  if (resolution.status ==
      PSX_AGGREGATE_MEMBER_BIT_WIDTH_EXCEEDS_STORAGE) {
    ps_diag_ctx_in(diagnostics, diag_tok, "member",
                 "bit-field width %d exceeds its %d-bit storage type",
                 request ? request->bit_width : 0,
                 resolution.bit_capacity);
  }
  if (resolution.status == PSX_AGGREGATE_MEMBER_INVALID_BITFIELD_TYPE) {
    ps_diag_ctx_in(diagnostics, diag_tok, "member",
                 "bit-field has non-integer canonical type");
  }
  if (resolution.status ==
      PSX_AGGREGATE_MEMBER_ATOMIC_BITFIELD_UNSUPPORTED) {
    ps_diag_ctx_in(diagnostics, diag_tok, "member",
                 "atomic-qualified bit-fields are not supported");
  }
  if (resolution.status == PSX_AGGREGATE_MEMBER_NEGATIVE_BIT_WIDTH) {
    ps_diag_ctx_in(diagnostics, diag_tok, "member",
                 "bit-field width %d is negative",
                 request ? request->bit_width : 0);
  }
  if (resolution.status ==
      PSX_AGGREGATE_MEMBER_NAMED_ZERO_WIDTH_BITFIELD) {
    ps_diag_ctx_in(diagnostics, diag_tok, "member",
                 "named bit-field '%.*s' has zero width",
                 request ? request->member_name_len : 0,
                 request && request->member_name
                     ? request->member_name : "");
  }
  if (resolution.status ==
      PSX_AGGREGATE_MEMBER_FLEXIBLE_ARRAY_IN_UNION) {
    ps_diag_ctx_in(diagnostics, diag_tok, "member",
                 "a flexible array member is not permitted in a union");
  }
  if (resolution.status ==
      PSX_AGGREGATE_MEMBER_FLEXIBLE_ARRAY_NEEDS_PRIOR_NAMED_MEMBER) {
    ps_diag_ctx_in(
        diagnostics, diag_tok, "member",
        "a flexible array member requires a prior named struct member");
  }
  if (resolution.status ==
      PSX_AGGREGATE_MEMBER_AFTER_FLEXIBLE_ARRAY) {
    ps_diag_ctx_in(
        diagnostics, diag_tok, "member",
        "a flexible array member must be the last struct member");
  }
  if (resolution.status ==
      PSX_AGGREGATE_MEMBER_FLEXIBLE_ARRAY_NESTED_IN_STRUCT) {
    ps_diag_ctx_in(
        diagnostics, diag_tok, "member",
        "a structure or union containing a flexible array member "
        "cannot be a struct member");
  }
  if (resolution.status ==
      PSX_AGGREGATE_MEMBER_FLEXIBLE_ARRAY_ELEMENT) {
    ps_diag_ctx_in(
        diagnostics, diag_tok, "member",
        "a structure or union containing a flexible array member "
        "cannot be an array element");
  }
  if (resolution.status == PSX_AGGREGATE_MEMBER_DUPLICATE) {
    ps_diag_ctx_in(diagnostics,
        diag_tok, "member",
        "メンバ '%.*s' は同じaggregate内で重複しています (C11 6.7.2.1)",
        resolution.conflicting_name_len,
        resolution.conflicting_name ? resolution.conflicting_name : "");
  }
  ps_diag_ctx_in(diagnostics, diag_tok, "member",
               "aggregate member declaration resolution failed");
  return 0;
}

void psx_apply_static_assert_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const node_t *condition, token_t *diag_tok) {
  if (!semantic_context || !global_registry || !local_registry || !condition)
    return;
  ag_diagnostic_context_t *diagnostics =
      ps_ctx_diagnostics(semantic_context);
  const psx_typed_hir_tree_t *typed_hir = NULL;
  psx_syntax_integer_constant_result_t constant_result;
  psx_resolved_hir_build_failure_t failure;
  psx_syntax_typed_hir_resolution_status_t status =
      psx_resolve_syntax_integer_constant_expression_direct_to_typed_hir_in_contexts(
          semantic_context, global_registry, local_registry,
          NULL, condition, &typed_hir, &constant_result, &failure);
  int is_constant =
      status == PSX_SYNTAX_TYPED_HIR_RESOLVED &&
      typed_hir && constant_result.is_constant;
  psx_static_assert_resolution_t resolution;
  psx_resolve_static_assert(
      &(psx_static_assert_request_t){
          .is_constant = is_constant,
          .value = constant_result.value,
      },
      &resolution);
  if (resolution.status == PSX_STATIC_ASSERT_NOT_CONSTANT) {
    diag_emit_tokf_in(diagnostics, DIAG_ERR_PARSER_STATIC_ASSERT_COND_NOT_CONST,
                   diag_tok, "%s",
                   diag_message_for_in(diagnostics,
                       DIAG_ERR_PARSER_STATIC_ASSERT_COND_NOT_CONST));
  }
  if (resolution.status == PSX_STATIC_ASSERT_FAILED) {
    diag_emit_tokf_in(diagnostics, DIAG_ERR_PARSER_STATIC_ASSERT_FAILED,
                   diag_tok, "%s",
                   diag_message_for_in(diagnostics, DIAG_ERR_PARSER_STATIC_ASSERT_FAILED));
  }
}
