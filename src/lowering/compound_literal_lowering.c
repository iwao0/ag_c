#include "compound_literal_lowering.h"
#include "runtime_context.h"

#include "../declaration_pipeline.h"
#include "../diag/diag.h"
#include "../diag/error_catalog.h"
#include "../parser/diag.h"
#include "../parser/node_utils.h"
#include "../parser/type.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *new_compound_object_name(
    psx_lowering_context_t *lowering_context, int file_scope) {
  int sequence = file_scope
                     ? lowering_context->file_scope_compound_sequence++
                     : lowering_context->local_compound_sequence++;
  const char *format = file_scope ? "__compound_lit_%d"
                                  : "__compound_object_%d";
  int len = snprintf(NULL, 0, format, sequence);
  char *name = calloc((size_t)len + 1, 1);
  if (!name) return NULL;
  snprintf(name, (size_t)len + 1, format, sequence);
  return name;
}

static node_t *lower_file_scope_compound_literal(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    node_compound_literal_t *compound,
    const token_t *fallback_diag_tok) {
  const psx_type_t *type = compound->type_name.resolved_type;
  node_t *initializer = compound->base.rhs;
  int is_array = type && type->kind == PSX_TYPE_ARRAY;
  node_init_list_t *list = initializer && initializer->kind == ND_INIT_LIST
                               ? (node_init_list_t *)initializer
                               : NULL;
  if (!is_array && !compound->requires_addressable_object &&
      !ps_type_is_tag_aggregate(type) && list &&
      list->entry_count == 1 &&
      list->entries[0].designator_count == 0 &&
      list->entries[0].value &&
      list->entries[0].value->kind == ND_NUM) {
    return list->entries[0].value;
  }

  psx_parsed_initializer_t parsed = {
      .has_initializer = 1,
      .kind = PSX_DECL_INIT_LIST,
      .value = initializer,
      .value_tok = compound->base.tok,
  };
  psx_global_declaration_pipeline_result_t object;
  char *storage_name = new_compound_object_name(lowering_context, 1);
  token_t *diag_tok = compound->base.tok
                          ? compound->base.tok
                          : (token_t *)fallback_diag_tok;
  if (!storage_name ||
      !psx_apply_global_declaration_pipeline(
          &(psx_global_declaration_pipeline_request_t){
              .semantic_context = semantic_context,
              .global_registry = global_registry,
              .local_registry = local_registry,
              .lowering_context = lowering_context,
              .options = options,
              .name = storage_name,
              .name_len = storage_name ? (int)strlen(storage_name) : 0,
              .type = type,
              .is_static = 1,
              .initializer = &parsed,
              .diag_tok = diag_tok,
          },
          &object)) {
    ps_diag_ctx_in(
        ps_lowering_diagnostics(lowering_context), diag_tok,
        "compound-literal", "%s",
        diag_message_for_in(
            ps_lowering_diagnostics(lowering_context),
                    DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
  }
  node_t *reference = is_array
                          ? ps_node_new_gvar_array_addr_for_in(
                                ps_lowering_arena(lowering_context),
                                object.global)
                          : ps_node_new_gvar_for_in(
                                ps_lowering_arena(lowering_context),
                                object.global);
  if (!compound->requires_addressable_object) return reference;
  return is_array
             ? ps_node_new_explicit_addr_value_for_in(
                   ps_lowering_arena(lowering_context), reference)
             : ps_node_new_unary_addr_for_in(
                   ps_lowering_arena(lowering_context), reference);
}

static node_t *lower_local_compound_literal(
    psx_semantic_context_t *semantic_context,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    node_compound_literal_t *compound,
    const token_t *fallback_diag_tok) {
  const psx_type_t *type = compound->type_name.resolved_type;
  psx_parsed_initializer_t parsed = {
      .has_initializer = 1,
      .kind = PSX_DECL_INIT_LIST,
      .value = compound->base.rhs,
      .value_tok = compound->base.tok,
  };
  psx_runtime_declarator_application_t application = {0};
  psx_automatic_local_declaration_pipeline_result_t object;
  char *storage_name = new_compound_object_name(lowering_context, 0);
  token_t *diag_tok = compound->base.tok
                          ? compound->base.tok
                          : (token_t *)fallback_diag_tok;
  if (!storage_name ||
      !psx_apply_automatic_local_declaration_pipeline(
          &(psx_automatic_local_declaration_pipeline_request_t){
              .semantic_context = semantic_context,
              .local_registry = local_registry,
              .lowering_context = lowering_context,
              .name = storage_name,
              .name_len = storage_name ? (int)strlen(storage_name) : 0,
              .type = type,
              .application = &application,
              .initializer = &parsed,
              .diag_tok = diag_tok,
          },
          &object)) {
    ps_diag_ctx_in(
        ps_lowering_diagnostics(lowering_context), diag_tok,
        "compound-literal",
        "compound literal local storage lowering failed");
  }
  int is_array = type && type->kind == PSX_TYPE_ARRAY;
  node_t *reference = is_array
                          ? ps_node_new_lvar_array_addr_for_in(
                                ps_lowering_arena(lowering_context),
                                object.var)
                          : ps_node_new_lvar_expr_ref_for_in(
                                ps_lowering_arena(lowering_context),
                                object.var);
  if (compound->requires_addressable_object) {
    reference = is_array
                    ? ps_node_new_explicit_addr_value_for_in(
                          ps_lowering_arena(lowering_context), reference)
                    : ps_node_new_unary_addr_for_in(
                          ps_lowering_arena(lowering_context), reference);
  }
  return ps_node_new_binary_for_target_in(
      ps_lowering_arena(lowering_context),
      ps_lowering_target(lowering_context), ND_COMMA,
      object.initialization, reference);
}

node_t *lower_compound_literal_expression_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    node_t *node, const token_t *fallback_diag_tok) {
  if (!semantic_context || !global_registry || !local_registry ||
      !lowering_context || !options)
    return node;
  if (!node || node->kind != ND_COMPOUND_LITERAL) return node;
  node_compound_literal_t *compound = (node_compound_literal_t *)node;
  node_t *lowered = compound->has_file_scope_storage
                        ? lower_file_scope_compound_literal(
                              semantic_context, global_registry,
                              local_registry, lowering_context, options,
                              compound, fallback_diag_tok)
                        : lower_local_compound_literal(
                              semantic_context, local_registry,
                              lowering_context,
                              compound, fallback_diag_tok);
  if (!lowered) return node;
  if (!lowered->tok) lowered->tok = node->tok;
  return lowered;
}
