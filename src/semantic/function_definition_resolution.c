#include "function_definition_resolution.h"

#include "declaration_application.h"
#include "resolved_node_kind.h"
#include "../declaration_pipeline.h"
#include "../diag/diag.h"
#include "../lowering/local_storage.h"
#include "../parser/arena.h"
#include "../parser/decl.h"
#include "../parser/diag.h"
#include "../parser/function_definition_syntax.h"
#include "../parser/global_registry.h"
#include "../parser/local_registry.h"
#include "../parser/node_utils.h"
#include "../parser/runtime_context.h"
#include "../parser/semantic_ctx.h"

#include <stdlib.h>
#include <string.h>

static int resolve_function_definition_header(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const psx_parsed_function_definition_t *definition,
    psx_function_definition_header_resolution_t *resolution,
    node_t ***compatibility_parameters) {
  if (resolution)
    *resolution = (psx_function_definition_header_resolution_t){0};
  if (compatibility_parameters) *compatibility_parameters = NULL;
  if (!definition || !semantic_context ||
      !global_registry || !local_registry || !lowering_context ||
      !resolution)
    return 0;
  ag_diagnostic_context_t *diagnostics =
      ps_ctx_diagnostics(semantic_context);
  ps_local_registry_prepare_function_resolution_in(local_registry);
  local_storage_reset(lowering_context);
  ps_ctx_reset_function_scope_in(semantic_context);

  const psx_type_t *base_type =
      psx_apply_parsed_decl_specifier_in_contexts(
          semantic_context, global_registry, local_registry,
          &definition->return_specifier);
  if (!base_type) {
    ps_diag_ctx_in(
        diagnostics, definition->diagnostic_token, "funcdef",
        "canonical function return base type resolution failed");
    return 0;
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
    return 0;
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
      return 0;
    }
  }
  if (!psx_finish_function_definition_pipeline(&pipeline)) {
    ps_diag_ctx_in(
        diagnostics, definition->declarator.diagnostic_token,
        "funcdef", "function definition pipeline finalization failed");
    return 0;
  }
  if (applied.has_unnamed_parameter) {
    ps_diag_missing_in(
        diagnostics, definition->declarator.diagnostic_token,
        diag_text_for_in(diagnostics, DIAG_TEXT_PARAMETER));
    return 0;
  }
  if (!applied.function_type->base) {
    ps_diag_ctx_in(
        diagnostics, definition->diagnostic_token, "funcdef",
        "canonical function return type construction failed");
    return 0;
  }

  token_ident_t *name = definition->declarator.identifier;
  if (!name || !psx_apply_function_declaration_pipeline(
          &(psx_function_declaration_pipeline_request_t){
              .semantic_context = semantic_context,
              .global_registry = global_registry,
              .name = name ? name->str : NULL,
              .name_len = name ? name->len : 0,
              .function_type = applied.function_type,
              .is_definition = 1,
              .diag_context = "funcdef",
              .diag_tok = (token_t *)name,
          })) {
    ps_diag_ctx_in(
        diagnostics, (token_t *)name, "funcdef",
        "function declaration pipeline failed");
    return 0;
  }

  const psx_type_t *canonical_function_type =
      ps_ctx_get_function_type_in(
          semantic_context, name->str, name->len);
  psx_qual_type_t signature = ps_ctx_intern_qual_type_in(
      semantic_context, canonical_function_type);
  if (!canonical_function_type ||
      signature.type_id == PSX_TYPE_ID_INVALID) {
    ps_diag_ctx_in(
        diagnostics, (token_t *)name, "funcdef",
        "canonical function signature identity is unavailable");
    return 0;
  }

  lvar_t **resolved_parameters = NULL;
  if (applied.nargs > 0) {
    resolved_parameters = arena_alloc_in(
        ps_ctx_arena(semantic_context),
        (size_t)applied.nargs * sizeof(*resolved_parameters));
    memcpy(resolved_parameters, applied.parameter_vars,
           (size_t)applied.nargs * sizeof(*resolved_parameters));
  }
  resolution->name = name->str;
  resolution->name_len = name->len;
  resolution->function_type = canonical_function_type;
  resolution->signature_qual_type = signature;
  resolution->parameters = resolved_parameters;
  resolution->parameter_count = applied.nargs;
  resolution->locals = ps_decl_get_locals_in(local_registry);
  resolution->is_static = definition->is_static;
  resolution->is_variadic =
      canonical_function_type->is_variadic_function;
  resolution->has_implicit_int_return =
      definition->has_implicit_int_return;

  if (resolution->is_variadic)
    local_storage_reserve_prefix(lowering_context, 64);
  if (compatibility_parameters)
    *compatibility_parameters = applied.args;
  else
    free(applied.args);
  free(applied.parameter_vars);
  free(applied.parameter_types);
  return 1;
}

int psx_resolve_function_definition_header_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const psx_parsed_function_definition_t *definition,
    psx_function_definition_header_resolution_t *resolution) {
  return resolve_function_definition_header(
      semantic_context, global_registry, local_registry,
      lowering_context, definition, resolution, NULL);
}

node_function_definition_t *
psx_prepare_function_definition_resolution_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    psx_lowering_context_t *lowering_context,
    const psx_parsed_function_definition_t *definition) {
  if (!definition || !definition->body || !runtime_context)
    return NULL;
  psx_function_definition_header_resolution_t resolution;
  node_t **compatibility_parameters = NULL;
  if (!resolve_function_definition_header(
          semantic_context, global_registry, local_registry,
          lowering_context, definition, &resolution,
          &compatibility_parameters))
    return NULL;
  node_function_definition_t *node =
      psx_resolution_node_alloc_in(
          ps_parser_runtime_arena(runtime_context),
          sizeof(node_function_definition_t));
  if (!node ||
      !psx_resolution_node_set_kind(&node->base, ND_FUNCDEF))
    return NULL;
  node->base.tok =
      (token_t *)definition->declarator.identifier;
  ps_node_set_implicit_int_return(
      &node->base, resolution.has_implicit_int_return);
  node->base.rhs = definition->body;
  node->name = resolution.name;
  node->name_len = resolution.name_len;
  node->is_static = resolution.is_static;
  node->parameters = compatibility_parameters;
  node->parameter_count = resolution.parameter_count;
  node->signature = resolution.function_type;
  node->signature_qual_type = resolution.signature_qual_type;
  node->lvars = resolution.locals;
  return node;
}
