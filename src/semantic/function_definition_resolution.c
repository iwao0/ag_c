#include "function_definition_resolution.h"

#include "declaration_application.h"
#include "type_completeness.h"
#include "../declaration_pipeline.h"
#include "../diag/diag.h"
#include "../lowering/local_storage.h"
#include "../parser/arena.h"
#include "../parser/decl.h"
#include "../parser/diag.h"
#include "../parser/function_definition_syntax.h"
#include "../parser/global_registry.h"
#include "../parser/local_registry.h"
#include "../parser/runtime_context.h"
#include "../parser/semantic_ctx.h"

#include <stdlib.h>
#include <string.h>

static int parsed_declarator_has_explicit_vla_star(
    const psx_parsed_declarator_t *declarator) {
  if (!declarator) return 0;
  for (int op_index = 0;
       op_index < declarator->declarator_shape.count; op_index++) {
    const psx_declarator_op_t *op =
        &declarator->declarator_shape.ops[op_index];
    if (op->kind != PSX_DECL_OP_ARRAY || !op->is_vla_array)
      continue;
    int has_bound_expression = 0;
    for (int bound_index = 0;
         bound_index < declarator->array_bound_count; bound_index++) {
      if (declarator->array_bounds[bound_index].declarator_op_index ==
          op_index) {
        has_bound_expression = 1;
        break;
      }
    }
    if (!has_bound_expression) return 1;
  }
  return 0;
}

static int validate_function_definition_parameter_vla_stars(
    ag_diagnostic_context_t *diagnostics,
    const psx_parsed_function_definition_t *definition) {
  const psx_parsed_function_suffix_t *primary_suffix =
      psx_declarator_outermost_function_suffix(
          &definition->declarator);
  const psx_parsed_function_parameters_t *parameters =
      primary_suffix ? primary_suffix->parameters : NULL;
  for (int i = 0; parameters && i < parameters->count; i++) {
    const psx_parsed_function_parameter_t *parameter =
        &parameters->items[i];
    if (!parsed_declarator_has_explicit_vla_star(
            &parameter->declarator))
      continue;
    ps_diag_ctx_in(
        diagnostics, parameter->declarator.diagnostic_token,
        "funcdef",
        "variable length array '*' is only permitted in a "
        "function prototype declaration");
    return 0;
  }
  return 1;
}

static int validate_function_definition_return_type(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t function_qual_type,
    token_t *diagnostic_token) {
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_type_shape_t function_shape = {0};
  if (!psx_semantic_type_table_describe(
          types, function_qual_type.type_id, &function_shape) ||
      function_shape.kind != PSX_TYPE_FUNCTION)
    return 0;
  psx_qual_type_t return_type =
      psx_semantic_type_table_base(
          types, function_qual_type.type_id);
  psx_type_shape_t return_shape = {0};
  if (!psx_semantic_type_table_describe(
          types, return_type.type_id, &return_shape))
    return 0;
  if (return_shape.kind == PSX_TYPE_VOID ||
      psx_semantic_type_is_complete_object_in(
          semantic_context, return_type.type_id))
    return 1;
  ps_diag_ctx_in(
      ps_ctx_diagnostics(semantic_context), diagnostic_token,
      "funcdef",
      "function definition return type must be void or a "
      "complete object type");
  return 0;
}

static int resolve_function_definition_header(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const psx_parsed_function_definition_t *definition,
    psx_function_definition_header_resolution_t *resolution) {
  if (resolution)
    *resolution = (psx_function_definition_header_resolution_t){0};
  if (!definition || !semantic_context ||
      !global_registry || !local_registry || !lowering_context ||
      !resolution)
    return 0;
  ag_diagnostic_context_t *diagnostics =
      ps_ctx_diagnostics(semantic_context);
  if (definition->return_specifier.alignas_specifier_count > 0) {
    ps_diag_ctx_in(
        diagnostics, definition->diagnostic_token, "funcdef",
        "function definition cannot use an alignment specifier");
    return 0;
  }
  if (!validate_function_definition_parameter_vla_stars(
          diagnostics, definition))
    return 0;
  ps_local_registry_prepare_function_resolution_in(local_registry);
  local_storage_reset(lowering_context);

  psx_qual_type_t base_qual_type =
      psx_apply_parsed_decl_specifier_qual_type_in_contexts(
          semantic_context, global_registry, local_registry,
          &definition->return_specifier);
  if (base_qual_type.type_id == PSX_TYPE_ID_INVALID) {
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
              .base_qual_type = base_qual_type,
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
  if (applied.function_qual_type.type_id == PSX_TYPE_ID_INVALID) {
    ps_diag_ctx_in(
        diagnostics, definition->diagnostic_token, "funcdef",
        "canonical function return type construction failed");
    return 0;
  }
  if (!validate_function_definition_return_type(
          semantic_context, applied.function_qual_type,
          definition->declarator.diagnostic_token))
    return 0;
  if (!psx_validate_parsed_decl_specifier_constraints_in_context(
          semantic_context, &definition->return_specifier,
          applied.function_qual_type, 0, 0,
          PSX_DECLARATION_CONTEXT_FILE, 0,
          definition->declarator.diagnostic_token)) {
    return 0;
  }

  token_ident_t *name = definition->declarator.identifier;
  if (!name || !psx_apply_function_declaration_pipeline(
          &(psx_function_declaration_pipeline_request_t){
              .semantic_context = semantic_context,
              .name = name ? name->str : NULL,
              .name_len = name ? name->len : 0,
              .function_qual_type = applied.function_qual_type,
              .is_definition = 1,
              .is_static = definition->is_static,
              .diag_context = "funcdef",
              .diag_tok = (token_t *)name,
          })) {
    ps_diag_ctx_in(
        diagnostics, (token_t *)name, "funcdef",
        "function declaration pipeline failed");
    return 0;
  }

  psx_qual_type_t signature = ps_ctx_get_function_qual_type_in(
      semantic_context, name->str, name->len);
  if (signature.type_id == PSX_TYPE_ID_INVALID) {
    ps_diag_ctx_in(
        diagnostics, (token_t *)name, "funcdef",
        "canonical function signature identity is unavailable");
    return 0;
  }
  psx_type_shape_t function_shape = {0};
  if (!psx_semantic_type_table_describe(
          ps_ctx_semantic_type_table_in(semantic_context),
          signature.type_id, &function_shape) ||
      function_shape.kind != PSX_TYPE_FUNCTION) {
    ps_diag_ctx_in(
        diagnostics, (token_t *)name, "funcdef",
        "canonical function signature shape is unavailable");
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
  resolution->signature_qual_type = signature;
  resolution->parameters = resolved_parameters;
  resolution->parameter_count = applied.nargs;
  resolution->locals = ps_decl_get_storage_objects_in(local_registry);
  resolution->is_static = ps_function_symbol_has_internal_linkage(
      ps_ctx_find_function_symbol_in(
          semantic_context, name->str, name->len));
  resolution->is_variadic = function_shape.is_variadic_function;
  resolution->has_implicit_int_return =
      definition->has_implicit_int_return;

  if (resolution->is_variadic)
    local_storage_reserve_prefix(lowering_context, 64);
  free(applied.parameter_vars);
  free(applied.parameter_qual_types);
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
      lowering_context, definition, resolution);
}
