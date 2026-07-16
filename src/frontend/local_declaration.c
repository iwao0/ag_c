#include "local_declaration.h"

#include "../semantic/declaration_application.h"
#include "../semantic/declaration_registration.h"
#include "../declaration_pipeline.h"
#include "../diag/diag.h"
#include "../parser/decl.h"
#include "../parser/diag.h"
#include "../parser/node_utils.h"
#include "../parser/local_registry.h"
#include "../parser/global_registry.h"
#include "../parser/semantic_ctx.h"
#include "../lowering/runtime_context.h"

#include <stdlib.h>

typedef enum {
  PSX_LOCAL_APPLY_NONE = 0,
  PSX_LOCAL_APPLY_TYPEDEF,
  PSX_LOCAL_APPLY_EXTERN,
  PSX_LOCAL_APPLY_STATIC,
  PSX_LOCAL_APPLY_AUTOMATIC,
} psx_local_apply_kind_t;

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_global_registry_t *global_registry;
  psx_local_registry_t *local_registry;
  psx_lowering_context_t *lowering_context;
  const ag_compilation_options_t *options;
  const psx_type_t *base_type;
  int requested_alignment;
  int is_typedef;
  int is_extern;
  int is_static;
  node_t *initialization;

  psx_local_apply_kind_t current_kind;
  const psx_type_t *current_type;
  psx_runtime_declarator_application_t current_application;
  psx_parsed_initializer_t current_initializer;
  psx_static_local_declaration_pipeline_request_t static_request;
  psx_static_local_declaration_pipeline_result_t static_result;
  psx_automatic_local_declaration_pipeline_request_t automatic_request;
  psx_automatic_local_declaration_pipeline_result_t automatic_result;
} psx_local_declaration_application_t;

static ag_diagnostic_context_t *application_diagnostics(
    const psx_local_declaration_application_t *application) {
  return ps_ctx_diagnostics(application->semantic_context);
}

static void *begin_declaration(
    void *context, const psx_parsed_decl_specifier_t *specifier,
    int is_typedef, int is_standalone_tag) {
  const psx_local_declaration_callbacks_t *callbacks = context;
  psx_local_declaration_application_t *application =
      calloc(1, sizeof(*application));
  if (!application) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(callbacks->semantic_context),
        specifier ? specifier->diagnostic_token : NULL,
        "local-declaration", "local declaration allocation failed");
  }
  application->semantic_context = callbacks->semantic_context;
  application->global_registry = callbacks->global_registry;
  application->local_registry = callbacks->local_registry;
  application->lowering_context = callbacks->lowering_context;
  application->options = callbacks->options;
  application->is_typedef = is_typedef;
  if (is_standalone_tag) {
    psx_apply_parsed_standalone_tag_in_contexts(
        application->semantic_context, application->global_registry,
        application->local_registry,
        specifier);
    return application;
  }
  application->base_type = psx_apply_parsed_decl_specifier_in_contexts(
      application->semantic_context, application->global_registry,
      application->local_registry,
      specifier);
  if (!application->base_type) {
    ps_diag_ctx_in(
        application_diagnostics(application),
        specifier->diagnostic_token, "local-declaration",
        "canonical local declaration type resolution failed");
  }
  application->requested_alignment =
      psx_apply_parsed_decl_alignment(
          application->semantic_context, specifier);
  application->is_extern = specifier->type_spec.is_extern ? 1 : 0;
  application->is_static = specifier->type_spec.is_static ? 1 : 0;
  return application;
}

static void begin_declarator(
    void *declaration_context,
    const psx_parsed_declarator_t *declarator,
    const psx_parsed_initializer_t *initializer) {
  psx_local_declaration_application_t *application = declaration_context;
  token_ident_t *name = declarator->identifier;
  application->current_kind = PSX_LOCAL_APPLY_NONE;
  application->current_initializer = *initializer;
  psx_apply_runtime_parsed_declarator_in_contexts(
      application->semantic_context, application->global_registry,
      application->local_registry,
      declarator,
      &application->current_application);
  application->current_type = psx_apply_runtime_declarator_type_in_context(
      application->semantic_context, application->base_type,
      &application->current_application);
  if (!application->current_type) {
    ps_diag_ctx_in(
        application_diagnostics(application), (token_t *)name,
        "local-declaration",
        "canonical declarator type resolution failed for '%.*s'",
        name->len, name->str);
  }

  if (application->is_typedef) {
    if (initializer->has_initializer) {
      ps_diag_ctx_in(
          application_diagnostics(application), (token_t *)name,
          "typedef",
          "typedef declaration '%.*s' cannot have an initializer",
          name->len, name->str);
    }
    psx_apply_parsed_typedef_declaration_in_contexts(
        application->semantic_context, application->global_registry,
        application->local_registry,
        name->str, name->len, application->current_type, (token_t *)name);
    application->current_kind = PSX_LOCAL_APPLY_TYPEDEF;
    return;
  }

  if (application->is_extern ||
      application->current_type->kind == PSX_TYPE_FUNCTION) {
    if (!psx_apply_block_extern_declaration_pipeline(
            &(psx_block_extern_declaration_pipeline_request_t){
                .semantic_context = application->semantic_context,
                .global_registry = application->global_registry,
                .local_registry = application->local_registry,
                .lowering_context = application->lowering_context,
                .options = application->options,
                .name = name->str,
                .name_len = name->len,
                .type = application->current_type,
                .has_initializer = initializer->has_initializer,
                .diag_tok = (token_t *)name,
            })) {
      ps_diag_ctx_in(
          application_diagnostics(application), (token_t *)name,
          "local-declaration",
          "block declaration pipeline failed for '%.*s'",
          name->len, name->str);
    }
    application->current_kind = PSX_LOCAL_APPLY_EXTERN;
    return;
  }

  if (application->is_static) {
    char *function_name = NULL;
    int function_name_len = 0;
    ps_decl_get_current_funcname_in(
        application->local_registry,
        &function_name, &function_name_len);
    application->static_request =
        (psx_static_local_declaration_pipeline_request_t){
            .semantic_context = application->semantic_context,
            .global_registry = application->global_registry,
            .local_registry = application->local_registry,
            .lowering_context = application->lowering_context,
            .options = application->options,
            .function_name = function_name,
            .function_name_len = function_name_len,
            .name = name->str,
            .name_len = name->len,
            .type = application->current_type,
            .initializer = &application->current_initializer,
            .diag_tok = (token_t *)name,
        };
    if (!psx_begin_static_local_declaration_pipeline(
            &application->static_request, &application->static_result)) {
      ps_diag_ctx_in(
          application_diagnostics(application), (token_t *)name,
          "local-declaration",
          "static local declaration pipeline failed for '%.*s'",
          name->len, name->str);
    }
    application->current_kind = PSX_LOCAL_APPLY_STATIC;
    return;
  }

  application->automatic_request =
      (psx_automatic_local_declaration_pipeline_request_t){
          .semantic_context = application->semantic_context,
          .local_registry = application->local_registry,
          .lowering_context = application->lowering_context,
          .name = name->str,
          .name_len = name->len,
          .type = application->current_type,
          .application = &application->current_application,
          .requested_alignment = application->requested_alignment,
          .initializer = &application->current_initializer,
          .diag_tok = (token_t *)name,
      };
  if (!psx_begin_automatic_local_declaration_pipeline(
          &application->automatic_request,
          &application->automatic_result)) {
    ps_diag_ctx_in(
        application_diagnostics(application), (token_t *)name,
        "local-declaration",
        "automatic local declaration pipeline failed for '%.*s'",
        name->len, name->str);
  }
  application->current_kind = PSX_LOCAL_APPLY_AUTOMATIC;
}

static void finish_declarator(
    void *declaration_context,
    const psx_parsed_initializer_t *initializer) {
  psx_local_declaration_application_t *application = declaration_context;
  application->current_initializer = *initializer;
  switch (application->current_kind) {
    case PSX_LOCAL_APPLY_STATIC:
      if (!psx_finish_static_local_declaration_pipeline(
              &application->static_request, &application->static_result)) {
        ps_diag_ctx_in(
            application_diagnostics(application),
            application->static_request.diag_tok,
            "local-declaration",
            "static local declaration finalization failed");
      }
      break;
    case PSX_LOCAL_APPLY_AUTOMATIC:
      if (!psx_finish_automatic_local_declaration_pipeline(
              &application->automatic_request,
              &application->automatic_result)) {
        ps_diag_ctx_in(
            application_diagnostics(application),
            application->automatic_request.diag_tok,
            "local-declaration",
            "automatic local declaration finalization failed");
      }
      if (application->automatic_result.initialization) {
        application->initialization = application->initialization
            ? ps_node_new_binary_for_target_in(
                  ps_lowering_arena(application->lowering_context),
                  ps_lowering_target(application->lowering_context), ND_COMMA,
                  application->initialization,
                  application->automatic_result.initialization)
            : application->automatic_result.initialization;
      }
      break;
    case PSX_LOCAL_APPLY_NONE:
    case PSX_LOCAL_APPLY_TYPEDEF:
    case PSX_LOCAL_APPLY_EXTERN:
      break;
  }
}

static node_t *finish_declaration(void *declaration_context) {
  psx_local_declaration_application_t *application = declaration_context;
  node_t *result = application && application->initialization
                       ? application->initialization
                       : ps_node_new_num_in(
                             ps_lowering_arena(application->lowering_context),
                             0);
  free(application);
  return result;
}

static void abort_declaration(void *declaration_context) {
  free(declaration_context);
}

void psx_frontend_init_local_declaration_callbacks_in_contexts(
    psx_local_declaration_callbacks_t *callbacks,
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options) {
  if (!callbacks) return;
  *callbacks = (psx_local_declaration_callbacks_t){0};
  if (!semantic_context || !global_registry || !local_registry ||
      !runtime_context || !lowering_context || !options)
    return;
  *callbacks = (psx_local_declaration_callbacks_t){
      .name_classifier = ps_ctx_name_classifier(semantic_context),
      .semantic_context = semantic_context,
      .global_registry = global_registry,
      .local_registry = local_registry,
      .runtime_context = runtime_context,
      .lowering_context = lowering_context,
      .options = options,
      .begin_declaration = begin_declaration,
      .begin_declarator = begin_declarator,
      .finish_declarator = finish_declarator,
      .finish_declaration = finish_declaration,
      .abort_declaration = abort_declaration,
  };
  callbacks->context = callbacks;
}
