#include "toplevel_declaration.h"

#include "../semantic/declaration_application.h"
#include "../semantic/declaration_registration.h"
#include "../declaration_pipeline.h"
#include "../parser/diag.h"
#include "../parser/global_registry.h"
#include "../parser/local_registry.h"
#include "../parser/semantic_ctx.h"

#include <stdlib.h>

typedef enum {
  PSX_TOPLEVEL_APPLY_NONE = 0,
  PSX_TOPLEVEL_APPLY_TYPEDEF,
  PSX_TOPLEVEL_APPLY_FUNCTION,
  PSX_TOPLEVEL_APPLY_GLOBAL,
} psx_toplevel_apply_kind_t;

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_global_registry_t *global_registry;
  psx_local_registry_t *local_registry;
  psx_parsed_toplevel_declaration_t *declaration;
  const psx_type_t *base_type;
  psx_toplevel_apply_kind_t current_kind;
  const psx_type_t *current_type;
  psx_parsed_initializer_t current_initializer;
  psx_global_declaration_pipeline_request_t global_request;
  psx_global_declaration_pipeline_result_t global_result;
} psx_toplevel_declaration_application_t;

static void apply_function_prototype(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    token_ident_t *name, const psx_type_t *type) {
  if (!name || !type || type->kind != PSX_TYPE_FUNCTION) return;
  if (!psx_apply_function_declaration_pipeline(
      &(psx_function_declaration_pipeline_request_t){
              .semantic_context = semantic_context,
              .global_registry = global_registry,
              .name = name->str,
              .name_len = name->len,
              .function_type = type,
              .diag_context = "decl",
              .diag_tok = (token_t *)name,
          })) {
    ps_diag_ctx((token_t *)name, "decl",
                "function declaration pipeline failed");
  }
}

static void *begin_declaration(
    void *context, psx_parsed_toplevel_declaration_t *declaration) {
  const psx_toplevel_declaration_callbacks_t *callbacks = context;
  if (!callbacks || !callbacks->semantic_context ||
      !callbacks->global_registry || !callbacks->local_registry) {
    ps_diag_ctx(declaration ? declaration->diagnostic_token : NULL,
                "decl", "complete top-level declaration context is required");
  }
  psx_toplevel_declaration_application_t *application =
      calloc(1, sizeof(*application));
  if (!application) {
    ps_diag_ctx(declaration ? declaration->diagnostic_token : NULL,
                "decl", "top-level declaration allocation failed");
  }
  application->semantic_context = callbacks->semantic_context;
  application->global_registry = callbacks->global_registry;
  application->local_registry = callbacks->local_registry;
  application->declaration = declaration;
  if (declaration->is_standalone_tag) {
    psx_apply_parsed_standalone_tag_in_contexts(
        application->semantic_context, application->global_registry,
        application->local_registry,
        &declaration->specifier);
    return application;
  }

  application->base_type = psx_apply_parsed_decl_specifier_in_contexts(
      application->semantic_context, application->global_registry,
      application->local_registry,
      &declaration->specifier);
  if (!application->base_type) {
    ps_diag_ctx(declaration->diagnostic_token, "decl",
                "canonical top-level base type resolution failed");
  }
  return application;
}

static void begin_declarator(
    void *declaration_context,
    psx_parsed_declarator_t *declarator,
    psx_parsed_initializer_t *initializer) {
  psx_toplevel_declaration_application_t *application = declaration_context;
  psx_parsed_toplevel_declaration_t *declaration = application->declaration;
  token_ident_t *name = declarator->identifier;
  application->current_kind = PSX_TOPLEVEL_APPLY_NONE;
  application->current_initializer = *initializer;
  application->current_type = psx_apply_parsed_declarator_type_in_contexts(
      application->semantic_context, application->global_registry,
      application->local_registry,
      application->base_type, declarator);
  if (!application->current_type) {
    ps_diag_ctx(declarator->diagnostic_token, "decl",
                "canonical top-level declarator type resolution failed");
  }

  if (declaration->is_typedef) {
    if (initializer->has_initializer) {
      ps_diag_ctx((token_t *)name, "typedef",
                  "typedef declaration '%.*s' cannot have an initializer",
                  name->len, name->str);
    }
    psx_apply_parsed_typedef_declaration_in_contexts(
        application->semantic_context, application->global_registry,
        application->local_registry,
        name->str, name->len, application->current_type,
        declarator->diagnostic_token);
    application->current_kind = PSX_TOPLEVEL_APPLY_TYPEDEF;
    return;
  }
  if (application->current_type->kind == PSX_TYPE_FUNCTION) {
    if (initializer->has_initializer) {
      ps_diag_ctx((token_t *)name, "decl",
                  "function declaration '%.*s' cannot have an initializer",
                  name->len, name->str);
    }
    apply_function_prototype(
        application->semantic_context, application->global_registry,
        name, application->current_type);
    application->current_kind = PSX_TOPLEVEL_APPLY_FUNCTION;
    return;
  }

  application->global_request =
      (psx_global_declaration_pipeline_request_t){
          .semantic_context = application->semantic_context,
          .global_registry = application->global_registry,
          .local_registry = application->local_registry,
          .name = name->str,
          .name_len = name->len,
          .type = application->current_type,
          .is_extern_decl = declaration->is_extern,
          .is_static = declaration->is_static,
          .is_thread_local = declaration->is_thread_local,
          .initializer = &application->current_initializer,
          .diag_tok = declarator->diagnostic_token,
      };
  if (!psx_begin_global_declaration_pipeline(
          &application->global_request, &application->global_result)) {
    ps_diag_ctx(declarator->diagnostic_token, "decl",
                "global declaration storage pipeline failed");
  }
  application->current_kind = PSX_TOPLEVEL_APPLY_GLOBAL;
}

static void finish_declarator(
    void *declaration_context,
    psx_parsed_initializer_t *initializer) {
  psx_toplevel_declaration_application_t *application = declaration_context;
  if (application->current_kind != PSX_TOPLEVEL_APPLY_GLOBAL) return;
  application->current_initializer = *initializer;
  if (initializer->has_initializer &&
      initializer->kind == PSX_DECL_INIT_EXPR && initializer->value &&
      initializer->value->kind == ND_COMPOUND_LITERAL &&
      (application->current_type->kind == PSX_TYPE_ARRAY ||
       ps_type_is_tag_aggregate(application->current_type))) {
    application->current_initializer.kind = PSX_DECL_INIT_LIST;
    application->current_initializer.value = initializer->value->rhs;
  }
  if (!psx_finish_global_declaration_pipeline(
          &application->global_request, &application->global_result)) {
    ps_diag_ctx(application->global_request.diag_tok, "decl",
                "global declaration initializer pipeline failed");
  }
}

static void finish_declaration(void *declaration_context) {
  free(declaration_context);
}

void psx_frontend_init_toplevel_declaration_callbacks_in_contexts(
    psx_toplevel_declaration_callbacks_t *callbacks,
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context) {
  if (!callbacks) return;
  *callbacks = (psx_toplevel_declaration_callbacks_t){0};
  if (!semantic_context || !global_registry || !local_registry ||
      !runtime_context)
    return;
  *callbacks = (psx_toplevel_declaration_callbacks_t){
      .context = callbacks,
      .semantic_context = semantic_context,
      .global_registry = global_registry,
      .local_registry = local_registry,
      .runtime_context = runtime_context,
      .begin_declaration = begin_declaration,
      .begin_declarator = begin_declarator,
      .finish_declarator = finish_declarator,
      .finish_declaration = finish_declaration,
      .abort_declaration = finish_declaration,
  };
}

void psx_apply_toplevel_declaration_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    psx_parsed_toplevel_declaration_t *declaration) {
  if (!semantic_context || !global_registry || !local_registry ||
      !runtime_context ||
      !declaration || declaration->applied_during_parse) return;
  psx_toplevel_declaration_callbacks_t callbacks;
  psx_frontend_init_toplevel_declaration_callbacks_in_contexts(
      &callbacks, semantic_context, global_registry, local_registry,
      runtime_context);
  void *application = callbacks.begin_declaration(
      callbacks.context, declaration);
  for (int i = 0; i < declaration->declarator_count; i++) {
    callbacks.begin_declarator(
        application, &declaration->declarators[i],
        &declaration->initializers[i]);
    callbacks.finish_declarator(
        application, &declaration->initializers[i]);
  }
  callbacks.finish_declaration(application);
}
