#include "function_definition.h"

#include "../semantic/declaration_application.h"
#include "../semantic/resolved_node_kind.h"
#include "../declaration_pipeline.h"
#include "../diag/diag.h"
#include "../lowering/local_storage.h"
#include "../parser/arena.h"
#include "../parser/decl.h"
#include "../parser/diag.h"
#include "../parser/global_registry.h"
#include "../parser/node_utils.h"
#include "../parser/local_registry.h"
#include "../parser/semantic_ctx.h"
#include "../parser/runtime_context.h"

node_function_definition_t *psx_apply_function_definition_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    psx_lowering_context_t *lowering_context,
    const psx_parsed_function_definition_t *definition) {
  if (!definition || !definition->body ||
      !semantic_context || !global_registry ||
      !local_registry || !runtime_context || !lowering_context)
    return NULL;
  ag_diagnostic_context_t *diagnostics =
      ps_parser_runtime_diagnostics(runtime_context);
  ps_decl_reset_locals_in(local_registry);
  local_storage_reset(lowering_context);
  ps_ctx_reset_function_scope_in(semantic_context);

  const psx_type_t *base_type = psx_apply_parsed_decl_specifier_in_contexts(
      semantic_context, global_registry, local_registry,
      &definition->return_specifier);
  if (!base_type) {
    ps_diag_ctx_in(
        diagnostics, definition->diagnostic_token, "funcdef",
        "canonical function return base type resolution failed");
  }
  psx_function_definition_pipeline_result_t applied;
  psx_function_definition_pipeline_state_t pipeline;
  if (!psx_begin_function_definition_pipeline(
          &(psx_function_definition_pipeline_request_t){
              .semantic_context = semantic_context,
              .global_registry = global_registry,
              .local_registry = local_registry,
              .lowering_context = lowering_context,
              .base_type = base_type,
              .declarator = &definition->declarator,
          },
          &applied, &pipeline)) {
    ps_diag_ctx_in(
        diagnostics, definition->declarator.diagnostic_token,
        "funcdef", "function definition pipeline setup failed");
  }
  const psx_parsed_function_suffix_t *primary_suffix =
      psx_declarator_outermost_function_suffix(
          &definition->declarator);
  const psx_parsed_function_parameters_t *parameters =
      primary_suffix ? primary_suffix->parameters : NULL;
  for (int i = 0; parameters && i < parameters->count; i++) {
    const psx_parsed_function_parameter_t *parameter =
        &parameters->items[i];
    if (!psx_apply_function_definition_parameter_pipeline(
            &pipeline, parameter)) {
      ps_diag_ctx_in(
          diagnostics, parameter->declarator.diagnostic_token,
          "funcdef", "function parameter pipeline failed");
    }
  }
  if (!psx_finish_function_definition_pipeline(&pipeline)) {
    ps_diag_ctx_in(
        diagnostics, definition->declarator.diagnostic_token,
        "funcdef", "function definition pipeline finalization failed");
  }
  if (applied.has_unnamed_parameter) {
    ps_diag_missing_in(
        diagnostics, definition->declarator.diagnostic_token,
        diag_text_for_in(diagnostics, DIAG_TEXT_PARAMETER));
  }
  if (!applied.function_type->base) {
    ps_diag_ctx_in(
        diagnostics, definition->diagnostic_token, "funcdef",
        "canonical function return type construction failed");
  }

  token_ident_t *name = definition->declarator.identifier;
  node_function_definition_t *node =
      psx_resolution_node_alloc_in(
          ps_parser_runtime_arena(runtime_context),
          sizeof(node_function_definition_t));
  node->base.kind = ND_FUNCDEF;
  node->base.tok = (token_t *)name;
  ps_node_set_implicit_int_return(
      &node->base, definition->has_implicit_int_return);
  node->base.rhs = definition->body;
  node->name = name->str;
  node->name_len = name->len;
  node->is_static = definition->is_static;
  node->parameters = applied.args;
  node->parameter_count = applied.nargs;

  int registered = psx_apply_function_declaration_pipeline(
          &(psx_function_declaration_pipeline_request_t){
              .semantic_context = semantic_context,
              .global_registry = global_registry,
              .name = name->str,
              .name_len = name->len,
              .function_type = applied.function_type,
              .is_definition = 1,
              .function_node = node,
              .diag_context = "funcdef",
              .diag_tok = (token_t *)name,
          });
  if (!registered) {
    ps_diag_ctx_in(
        diagnostics, (token_t *)name, "funcdef",
        "function declaration pipeline failed");
  }
  ps_decl_set_current_funcname_in(
      local_registry, name->str, name->len);
  if (node->signature->is_variadic_function)
    local_storage_reserve_prefix(lowering_context, 64);
  node->lvars = ps_decl_get_locals_in(local_registry);
  ps_decl_set_current_funcname_in(local_registry, NULL, 0);
  return node;
}
