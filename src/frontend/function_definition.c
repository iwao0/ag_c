#include "function_definition.h"

#include "../semantic/declaration_application.h"
#include "../declaration_pipeline.h"
#include "../diag/diag.h"
#include "../parser/arena.h"
#include "../parser/decl.h"
#include "../parser/diag.h"
#include "../parser/global_registry.h"
#include "../parser/node_utils.h"
#include "../parser/local_registry.h"
#include "../parser/semantic_ctx.h"

node_function_definition_t *psx_apply_function_definition_header_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parsed_function_definition_t *definition) {
  if (!definition || !semantic_context || !global_registry || !local_registry)
    return NULL;
  ps_decl_reset_locals_in(local_registry);
  ps_ctx_reset_function_scope_in(semantic_context);

  const psx_type_t *base_type = psx_apply_parsed_decl_specifier_in_contexts(
      semantic_context, global_registry, local_registry,
      &definition->return_specifier);
  if (!base_type) {
    ps_diag_ctx(definition->diagnostic_token, "funcdef",
                "canonical function return base type resolution failed");
  }
  ps_parse_runtime_declarator_expressions_in_contexts(
      &definition->declarator, semantic_context, global_registry,
      local_registry, NULL);
  psx_function_definition_pipeline_result_t applied;
  psx_function_definition_pipeline_state_t pipeline;
  if (!psx_begin_function_definition_pipeline(
          &(psx_function_definition_pipeline_request_t){
              .semantic_context = semantic_context,
              .global_registry = global_registry,
              .local_registry = local_registry,
              .base_type = base_type,
              .declarator = &definition->declarator,
          },
          &applied, &pipeline)) {
    ps_diag_ctx(definition->declarator.diagnostic_token, "funcdef",
                "function definition pipeline setup failed");
  }
  psx_parsed_function_suffix_t *primary_suffix =
      &definition->declarator.function_suffixes[0];
  psx_parsed_function_parameters_t *parameters =
      primary_suffix->parameters;
  for (int i = 0; parameters && i < parameters->count; i++) {
    psx_parsed_function_parameter_t *parameter =
        &parameters->items[i];
    ps_parse_runtime_declarator_expressions_in_contexts(
        &parameter->declarator, semantic_context, global_registry,
        local_registry, NULL);
    if (!psx_apply_function_definition_parameter_pipeline(
            &pipeline, parameter)) {
      ps_diag_ctx(parameter->declarator.diagnostic_token, "funcdef",
                  "function parameter pipeline failed");
    }
  }
  if (!psx_finish_function_definition_pipeline(&pipeline)) {
    ps_diag_ctx(definition->declarator.diagnostic_token, "funcdef",
                "function definition pipeline finalization failed");
  }
  if (applied.has_unnamed_parameter) {
    ps_diag_missing(
        definition->declarator.diagnostic_token,
        diag_text_for(DIAG_TEXT_PARAMETER));
  }
  if (!applied.function_type->base) {
    ps_diag_ctx(definition->diagnostic_token, "funcdef",
                "canonical function return type construction failed");
  }

  token_ident_t *name = definition->declarator.identifier;
  node_function_definition_t *node =
      arena_alloc(sizeof(node_function_definition_t));
  node->base.kind = ND_FUNCDEF;
  node->base.tok = (token_t *)name;
  node->base.is_implicit_int_return =
      definition->has_implicit_int_return ? 1 : 0;
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
    ps_diag_ctx((token_t *)name, "funcdef",
                "function declaration pipeline failed");
  }
  ps_decl_set_current_funcname_in(
      local_registry, name->str, name->len);
  if (node->signature->is_variadic_function)
    ps_decl_reserve_variadic_regs();
  return node;
}
