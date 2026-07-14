#include "function_definition.h"

#include "../semantic/declaration_application.h"
#include "../declaration_pipeline.h"
#include "../diag/diag.h"
#include "../parser/arena.h"
#include "../parser/decl.h"
#include "../parser/diag.h"
#include "../parser/node_utils.h"
#include "../parser/semantic_ctx.h"

node_func_t *psx_apply_function_definition_header(
    psx_parsed_function_definition_t *definition) {
  if (!definition) return NULL;
  ps_decl_reset_locals();
  ps_ctx_reset_function_scope();

  psx_type_t *base_type =
      psx_apply_parsed_decl_specifier(&definition->return_specifier);
  if (!base_type) {
    ps_diag_ctx(definition->diagnostic_token, "funcdef",
                "canonical function return base type resolution failed");
  }
  ps_parse_runtime_declarator_expressions(&definition->declarator);
  psx_function_definition_pipeline_result_t applied;
  psx_function_definition_pipeline_state_t pipeline;
  if (!psx_begin_function_definition_pipeline(
          &(psx_function_definition_pipeline_request_t){
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
    ps_parse_runtime_declarator_expressions(&parameter->declarator);
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
  node_func_t *node = arena_alloc(sizeof(node_func_t));
  node->base.kind = ND_FUNCDEF;
  node->base.tok = (token_t *)name;
  node->base.is_implicit_int_return =
      definition->has_implicit_int_return ? 1 : 0;
  node->funcname = name->str;
  node->funcname_len = name->len;
  node->is_static = definition->is_static;
  node->args = applied.args;
  node->nargs = applied.nargs;

  int registered = psx_apply_function_declaration_pipeline(
          &(psx_function_declaration_pipeline_request_t){
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
  ps_decl_set_current_funcname(name->str, name->len);
  if (node->function_type->is_variadic_function)
    ps_decl_reserve_variadic_regs();
  return node;
}
