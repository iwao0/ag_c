#include "declaration_registration.h"

#include "../diag/diag.h"
#include "../diag/error_catalog.h"
#include "../parser/diag.h"
#include "constant_expression.h"
#include "identifier_binding.h"
#include "semantic_pass.h"
#include "enum_constant_resolution.h"
#include "static_assert_resolution.h"
#include "typedef_declaration_resolution.h"

void psx_apply_parsed_typedef_declaration(
    char *name, int name_len, const psx_type_t *type, token_t *diag_tok) {
  psx_apply_parsed_typedef_declaration_in_context(
      NULL, name, name_len, type, diag_tok);
}

void psx_apply_parsed_typedef_declaration_in_context(
    psx_semantic_context_t *semantic_context,
    char *name, int name_len, const psx_type_t *type, token_t *diag_tok) {
  psx_typedef_declaration_resolution_t resolution;
  psx_resolve_typedef_declaration(
      &(psx_typedef_declaration_resolution_request_t){
          .semantic_context = semantic_context,
          .name = name,
          .name_len = name_len,
          .type = type,
      },
      &resolution);
  if (resolution.status == PSX_TYPEDEF_DECLARATION_OK) return;
  if (resolution.status == PSX_TYPEDEF_DECLARATION_TYPE_CONFLICT) {
    ps_diag_ctx(diag_tok, "typedef",
                 "typedef名 '%.*s' の型が以前の宣言と異なります (C11 6.7p3)",
                 name_len, name);
  }
  if (resolution.status == PSX_TYPEDEF_DECLARATION_OBJECT_NAME_CONFLICT) {
    ps_diag_ctx(diag_tok, "typedef",
                 "'%.*s' はオブジェクトとして既に宣言されています (C11 6.7p4)",
                 name_len, name);
  }
  if (resolution.status == PSX_TYPEDEF_DECLARATION_FUNCTION_NAME_CONFLICT) {
    ps_diag_ctx(diag_tok, "typedef",
                 "'%.*s' は関数として既に宣言されています (C11 6.7p4)",
                 name_len, name);
  }
  if (resolution.status == PSX_TYPEDEF_DECLARATION_ENUM_NAME_CONFLICT) {
    ps_diag_ctx(diag_tok, "typedef",
                 "'%.*s' はenum定数として既に宣言されています (C11 6.7p4)",
                 name_len, name);
  }
  ps_diag_ctx(diag_tok, "typedef",
               "canonical typedef declaration resolution failed for '%.*s'",
               name_len, name);
}

void psx_apply_parsed_enum_constant(
    char *name, int name_len, long long value, token_t *diag_tok) {
  psx_apply_parsed_enum_constant_in_context(
      NULL, name, name_len, value, diag_tok);
}

void psx_apply_parsed_enum_constant_in_context(
    psx_semantic_context_t *semantic_context,
    char *name, int name_len, long long value, token_t *diag_tok) {
  psx_enum_constant_resolution_t resolution;
  psx_resolve_enum_constant(
      &(psx_enum_constant_resolution_request_t){
          .semantic_context = semantic_context,
          .name = name,
          .name_len = name_len,
          .value = value,
      },
      &resolution);
  if (resolution.status == PSX_ENUM_CONSTANT_OK) return;
  if (resolution.status == PSX_ENUM_CONSTANT_DUPLICATE) {
    ps_diag_duplicate_with_name(
        diag_tok, "enum constant", name, name_len);
  }
  if (resolution.status == PSX_ENUM_CONSTANT_OBJECT_NAME_CONFLICT) {
    ps_diag_ctx(diag_tok, "enum",
                 "'%.*s' はオブジェクトとして既に宣言されています (C11 6.7p4)",
                 name_len, name);
  }
  if (resolution.status == PSX_ENUM_CONSTANT_FUNCTION_NAME_CONFLICT) {
    ps_diag_ctx(diag_tok, "enum",
                 "'%.*s' は関数として既に宣言されています (C11 6.7p4)",
                 name_len, name);
  }
  if (resolution.status == PSX_ENUM_CONSTANT_TYPEDEF_NAME_CONFLICT) {
    ps_diag_ctx(diag_tok, "enum",
                 "'%.*s' はtypedef名として既に宣言されています (C11 6.7p4)",
                 name_len, name);
  }
  ps_diag_ctx(diag_tok, "enum",
               "canonical enum constant resolution failed for '%.*s'",
               name_len, name);
}

void psx_apply_parsed_tag_declaration_in_context(
    psx_semantic_context_t *semantic_context,
    token_kind_t kind, char *name, int name_len,
    psx_tag_declaration_mode_t mode, int member_count,
    int size, int alignment, token_t *diag_tok) {
  psx_tag_declaration_resolution_t resolution;
  psx_resolve_tag_declaration(
      &(psx_tag_declaration_resolution_request_t){
          .semantic_context = semantic_context,
          .kind = kind,
          .name = name,
          .name_len = name_len,
          .mode = mode,
          .member_count = member_count,
          .size = size,
          .alignment = alignment,
      },
      &resolution);
  if (resolution.status == PSX_TAG_DECLARATION_OK) return;
  if (resolution.status == PSX_TAG_DECLARATION_REDEFINITION) {
    ps_diag_ctx(diag_tok, "tag",
                 "タグ '%.*s' は同一スコープで再定義されています (C11 6.7.2)",
                 name_len, name);
  }
  if (resolution.status == PSX_TAG_DECLARATION_KIND_CONFLICT) {
    ps_diag_ctx(diag_tok, "tag",
                 "タグ '%.*s' は同一スコープで異なる種類として宣言されています (C11 6.7.2.3)",
                 name_len, name);
  }
  ps_diag_ctx(diag_tok, "tag",
               "canonical tag declaration resolution failed for '%.*s'",
               name_len, name);
}

void psx_apply_parsed_tag_declaration(
    token_kind_t kind, char *name, int name_len,
    psx_tag_declaration_mode_t mode, int member_count,
    int size, int alignment, token_t *diag_tok) {
  psx_apply_parsed_tag_declaration_in_context(
      NULL, kind, name, name_len, mode, member_count,
      size, alignment, diag_tok);
}

int psx_apply_aggregate_member_declaration(
    psx_aggregate_layout_state_t *layout,
    const psx_aggregate_member_declaration_request_t *request,
    token_t *diag_tok) {
  psx_aggregate_member_declaration_resolution_t resolution;
  psx_resolve_aggregate_member_declaration(layout, request, &resolution);
  if (resolution.status == PSX_AGGREGATE_MEMBER_OK)
    return resolution.registered_member_count;
  if (resolution.status == PSX_AGGREGATE_MEMBER_MISSING_NAME) {
    ps_diag_missing(diag_tok, diag_text_for(DIAG_TEXT_MEMBER_NAME));
  }
  if (resolution.status == PSX_AGGREGATE_MEMBER_INCOMPLETE_TYPE) {
    ps_diag_ctx(diag_tok, "decl", "%s",
                 diag_message_for(
                     DIAG_ERR_PARSER_INCOMPLETE_MEMBER_FORBIDDEN));
  }
  if (resolution.status == PSX_AGGREGATE_MEMBER_FUNCTION_TYPE) {
    ps_diag_ctx(diag_tok, "decl", "%s",
                 diag_message_for(
                     DIAG_ERR_PARSER_FUNCTION_MEMBER_FORBIDDEN));
  }
  if (resolution.status ==
      PSX_AGGREGATE_MEMBER_BIT_WIDTH_EXCEEDS_STORAGE) {
    ps_diag_ctx(diag_tok, "member",
                 "bit-field width %d exceeds its %d-bit storage type",
                 request ? request->bit_width : 0,
                 resolution.storage_size * 8);
  }
  if (resolution.status == PSX_AGGREGATE_MEMBER_INVALID_BITFIELD_TYPE) {
    ps_diag_ctx(diag_tok, "member",
                 "bit-field has non-integer canonical type");
  }
  if (resolution.status == PSX_AGGREGATE_MEMBER_DUPLICATE) {
    ps_diag_ctx(
        diag_tok, "member",
        "メンバ '%.*s' は同じaggregate内で重複しています (C11 6.7.2.1)",
        resolution.conflicting_name_len,
        resolution.conflicting_name ? resolution.conflicting_name : "");
  }
  ps_diag_ctx(diag_tok, "member",
               "aggregate member declaration resolution failed");
  return 0;
}

void psx_apply_static_assert_in_context(
    psx_semantic_context_t *semantic_context,
    node_t *condition, token_t *diag_tok) {
  if (!condition) return;
  condition = psx_bind_identifier_tree_in(
      semantic_context, condition, diag_tok);
  psx_semantic_resolve_tree_in_context(
      semantic_context, condition, NULL, diag_tok);
  int is_constant = 1;
  long long value = psx_eval_const_int(condition, &is_constant);
  psx_static_assert_resolution_t resolution;
  psx_resolve_static_assert(
      &(psx_static_assert_request_t){
          .is_constant = is_constant,
          .value = value,
      },
      &resolution);
  if (resolution.status == PSX_STATIC_ASSERT_NOT_CONSTANT) {
    diag_emit_tokf(DIAG_ERR_PARSER_STATIC_ASSERT_COND_NOT_CONST,
                   diag_tok, "%s",
                   diag_message_for(
                       DIAG_ERR_PARSER_STATIC_ASSERT_COND_NOT_CONST));
  }
  if (resolution.status == PSX_STATIC_ASSERT_FAILED) {
    diag_emit_tokf(DIAG_ERR_PARSER_STATIC_ASSERT_FAILED,
                   diag_tok, "%s",
                   diag_message_for(DIAG_ERR_PARSER_STATIC_ASSERT_FAILED));
  }
}

void psx_apply_static_assert(node_t *condition, token_t *diag_tok) {
  psx_apply_static_assert_in_context(NULL, condition, diag_tok);
}
