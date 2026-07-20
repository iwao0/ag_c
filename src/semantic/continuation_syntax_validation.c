#include "continuation_syntax_validation.h"

#include <stddef.h>
#include <string.h>

#include "../continuation_options.h"
#include "../diag/diag.h"
#include "../parser/ast.h"
#include "../parser/function_definition_syntax.h"
#include "../parser/semantic_ctx.h"
#include "identifier_resolution.h"

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_global_registry_t *global_registry;
  psx_local_registry_t *local_registry;
  const ag_continuation_options_t *continuation;
  const token_t *invalid_token;
} continuation_condition_validation_t;

static int name_matches(
    const char *name, int name_length, const char *expected) {
  return name && expected && name_length >= 0 &&
         (size_t)name_length == strlen(expected) &&
         memcmp(name, expected, (size_t)name_length) == 0;
}

static void validate_node(
    continuation_condition_validation_t *validation,
    const node_t *node);

static void validate_call(
    continuation_condition_validation_t *validation,
    const node_function_call_t *call) {
  if (!call || validation->invalid_token) return;
  const node_identifier_t *identifier =
      call->callee && call->callee->kind == ND_IDENTIFIER
          ? (const node_identifier_t *)call->callee : NULL;
  if (identifier && name_matches(
          identifier->name, identifier->name_len,
          validation->continuation->frame_condition)) {
    psx_identifier_expression_resolution_t resolution;
    psx_resolve_identifier_expression(
        &(psx_identifier_resolution_request_t){
            .semantic_context = validation->semantic_context,
            .global_registry = validation->global_registry,
            .local_registry = validation->local_registry,
            .name = identifier->name,
            .name_len = identifier->name_len,
            .is_call = 1,
            .has_local_lookup_point = 1,
            .local_lookup_point = {
                .scope_seq = identifier->scope_seq,
                .declaration_seq = identifier->declaration_seq,
            },
        },
        &resolution);
    if (resolution.expression_qual_type.type_id !=
            PSX_TYPE_ID_INVALID &&
        !psx_semantic_type_is_exact_int_void_function(
            ps_ctx_semantic_type_table_in(
                validation->semantic_context),
            resolution.expression_qual_type)) {
      validation->invalid_token = call->base.tok;
      return;
    }
  }
  validate_node(validation, call->callee);
  for (int i = 0; i < call->argument_count; i++)
    validate_node(validation, call->arguments[i]);
}

static void validate_node(
    continuation_condition_validation_t *validation,
    const node_t *node) {
  if (!node || validation->invalid_token) return;
  if (node->kind == ND_FUNCALL) {
    validate_call(validation, (const node_function_call_t *)node);
    return;
  }
  if (node->kind == ND_BLOCK) {
    node_t *const *body = ((const node_block_t *)node)->body;
    for (size_t i = 0; body && body[i]; i++)
      validate_node(validation, body[i]);
    return;
  }
  if (node->kind == ND_IF || node->kind == ND_FOR ||
      node->kind == ND_TERNARY) {
    const node_ctrl_t *control = (const node_ctrl_t *)node;
    validate_node(validation, control->init);
    validate_node(validation, control->inc);
    validate_node(validation, control->els);
  }
  validate_node(validation, node->lhs);
  validate_node(validation, node->rhs);
}

int psx_validate_continuation_condition_types_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const ag_continuation_options_t *continuation,
    const psx_parsed_function_definition_t *function) {
  if (!continuation || !function || function->is_static ||
      !function->declarator.identifier ||
      !name_matches(
          function->declarator.identifier->str,
          function->declarator.identifier->len,
          continuation->entry))
    return 1;
  continuation_condition_validation_t validation = {
      .semantic_context = semantic_context,
      .global_registry = global_registry,
      .local_registry = local_registry,
      .continuation = continuation,
  };
  validate_node(&validation, function->body);
  if (!validation.invalid_token) return 1;
  ag_diagnostic_context_t *diagnostics =
      ps_ctx_diagnostics(semantic_context);
  diag_error_id_t id =
      DIAG_ERR_PARSER_CONTINUATION_FRAME_CONDITION_TYPE;
  diag_emit_tokf_in(
      diagnostics, id, validation.invalid_token, "%s",
      diag_message_for_in(diagnostics, id));
  return 0;
}
