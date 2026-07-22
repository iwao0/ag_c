#include "toplevel_declaration.h"

#include "../semantic/declaration_application.h"
#include "../semantic/declaration_registration.h"
#include "../declaration_pipeline.h"
#include "../parser/diag.h"
#include "../parser/global_registry.h"
#include "../parser/local_registry.h"
#include "../parser/semantic_ctx.h"

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
  psx_lowering_context_t *lowering_context;
  const ag_compilation_options_t *options;
  psx_parsed_toplevel_declaration_t *declaration;
  psx_qual_type_t base_qual_type;
  int requested_alignment;
  psx_toplevel_apply_kind_t current_kind;
  psx_qual_type_t current_qual_type;
  psx_parsed_initializer_t current_initializer;
  psx_global_declaration_pipeline_request_t global_request;
  psx_global_declaration_pipeline_result_t global_result;
} psx_toplevel_declaration_application_t;

static ag_diagnostic_context_t *application_diagnostics(
    const psx_toplevel_declaration_application_t *application) {
  return ps_ctx_diagnostics(application->semantic_context);
}

static void apply_function_prototype(
    psx_semantic_context_t *semantic_context,
    token_ident_t *name, psx_qual_type_t function_qual_type) {
  psx_type_shape_t function_shape = {0};
  if (!name ||
      !psx_semantic_type_table_describe(
          ps_ctx_semantic_type_table_in(semantic_context),
          function_qual_type.type_id, &function_shape) ||
      function_shape.kind != PSX_TYPE_FUNCTION)
    return;
  if (!psx_apply_function_declaration_pipeline(
      &(psx_function_declaration_pipeline_request_t){
              .semantic_context = semantic_context,
              .name = name->str,
              .name_len = name->len,
              .function_qual_type = function_qual_type,
              .diag_context = "decl",
              .diag_tok = (token_t *)name,
          })) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(semantic_context), (token_t *)name,
        "decl", "function declaration pipeline failed");
  }
}

static int begin_declaration(
    psx_toplevel_declaration_application_t *application,
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    psx_parsed_toplevel_declaration_t *declaration) {
  if (!application || !semantic_context || !global_registry ||
      !local_registry || !lowering_context || !options ||
      !declaration)
    return 0;
  *application = (psx_toplevel_declaration_application_t){
      .semantic_context = semantic_context,
      .global_registry = global_registry,
      .local_registry = local_registry,
      .lowering_context = lowering_context,
      .options = options,
  };
  application->declaration = declaration;
  if (declaration->is_standalone_tag) {
    if (declaration->specifier.alignas_specifier_count > 0 ||
        declaration->specifier.type_spec.is_inline ||
        declaration->specifier.type_spec.is_noreturn) {
      ps_diag_ctx_in(
          application_diagnostics(application),
          declaration->diagnostic_token, "declaration-specifier",
          "standalone tag declaration cannot use function or alignment specifiers");
      return 0;
    }
    psx_apply_parsed_standalone_tag_in_contexts(
        application->semantic_context, application->global_registry,
        application->local_registry,
        &declaration->specifier);
    return 1;
  }

  application->base_qual_type =
      psx_apply_parsed_decl_specifier_qual_type_in_contexts(
          application->semantic_context, application->global_registry,
          application->local_registry,
          &declaration->specifier);
  if (application->base_qual_type.type_id == PSX_TYPE_ID_INVALID) {
    ps_diag_ctx_in(
        application_diagnostics(application),
        declaration->diagnostic_token, "decl",
        "canonical top-level base type resolution failed");
    return 0;
  }
  application->requested_alignment =
      psx_resolve_parsed_decl_alignment_in_contexts(
          application->semantic_context, application->global_registry,
          application->local_registry, &declaration->specifier);
  return 1;
}

static void begin_declarator(
    psx_toplevel_declaration_application_t *application,
    psx_parsed_declarator_t *declarator,
    psx_parsed_initializer_t *initializer) {
  psx_parsed_toplevel_declaration_t *declaration = application->declaration;
  token_ident_t *name = declarator->identifier;
  application->current_kind = PSX_TOPLEVEL_APPLY_NONE;
  application->current_initializer = *initializer;
  application->current_qual_type =
      psx_apply_parsed_declarator_qual_type_in_contexts(
      application->semantic_context, application->global_registry,
      application->local_registry,
      application->base_qual_type, declarator);
  psx_type_shape_t current_shape = {0};
  if (!psx_semantic_type_table_describe(
          ps_ctx_semantic_type_table_in(application->semantic_context),
          application->current_qual_type.type_id, &current_shape)) {
    ps_diag_ctx_in(
        application_diagnostics(application),
        declarator->diagnostic_token, "decl",
        "canonical top-level declarator type resolution failed");
    return;
  }
  if (!psx_validate_parsed_decl_specifier_constraints_in_context(
          application->semantic_context, &declaration->specifier,
          application->current_qual_type,
          application->requested_alignment, declaration->is_typedef,
          0, declarator->has_bitfield,
          declarator->diagnostic_token))
    return;

  if (declaration->is_typedef) {
    if (initializer->has_initializer) {
      ps_diag_ctx_in(
          application_diagnostics(application), (token_t *)name,
          "typedef",
          "typedef declaration '%.*s' cannot have an initializer",
          name->len, name->str);
    }
    psx_apply_parsed_typedef_declaration_in(
        application->semantic_context,
        name->str, name->len,
        application->current_qual_type,
        declarator->diagnostic_token);
    application->current_kind = PSX_TOPLEVEL_APPLY_TYPEDEF;
    return;
  }
  if (current_shape.kind == PSX_TYPE_FUNCTION) {
    if (initializer->has_initializer) {
      ps_diag_ctx_in(
          application_diagnostics(application), (token_t *)name,
          "decl",
          "function declaration '%.*s' cannot have an initializer",
          name->len, name->str);
    }
    apply_function_prototype(
        application->semantic_context, name,
        application->current_qual_type);
    application->current_kind = PSX_TOPLEVEL_APPLY_FUNCTION;
    return;
  }

  application->global_request =
      (psx_global_declaration_pipeline_request_t){
          .semantic_context = application->semantic_context,
          .global_registry = application->global_registry,
          .local_registry = application->local_registry,
          .lowering_context = application->lowering_context,
          .options = application->options,
          .name = name->str,
          .name_len = name->len,
          .type = application->current_qual_type,
          .is_extern_decl = declaration->is_extern,
          .is_static = declaration->is_static,
          .is_thread_local = declaration->is_thread_local,
          .requested_alignment = application->requested_alignment,
          .initializer = &application->current_initializer,
          .diag_tok = declarator->diagnostic_token,
      };
  if (!psx_begin_global_declaration_pipeline(
          &application->global_request, &application->global_result)) {
    ps_diag_ctx_in(
        application_diagnostics(application),
        declarator->diagnostic_token, "decl",
        "global declaration storage pipeline failed");
  }
  application->current_kind = PSX_TOPLEVEL_APPLY_GLOBAL;
}

static void finish_declarator(
    psx_toplevel_declaration_application_t *application,
    psx_parsed_initializer_t *initializer) {
  if (application->current_kind != PSX_TOPLEVEL_APPLY_GLOBAL) return;
  application->current_initializer = *initializer;
  psx_type_shape_t current_shape = {0};
  if (!psx_semantic_type_table_describe(
          ps_ctx_semantic_type_table_in(application->semantic_context),
          application->current_qual_type.type_id, &current_shape))
    return;
  if (initializer->has_initializer &&
      initializer->kind == PSX_DECL_INIT_EXPR && initializer->value &&
      initializer->value->kind == ND_COMPOUND_LITERAL &&
      (current_shape.kind == PSX_TYPE_ARRAY ||
       current_shape.kind == PSX_TYPE_STRUCT ||
       current_shape.kind == PSX_TYPE_UNION)) {
    application->current_initializer.kind = PSX_DECL_INIT_LIST;
    application->current_initializer.value = initializer->value->rhs;
  }
  if (!psx_finish_global_declaration_pipeline(
          &application->global_request, &application->global_result)) {
    ps_diag_ctx_in(
        application_diagnostics(application),
        application->global_request.diag_tok, "decl",
        "global declaration initializer pipeline failed");
  }
}

void psx_apply_toplevel_declaration_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_parser_runtime_context_t *runtime_context,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    psx_parsed_toplevel_declaration_t *declaration) {
  if (!semantic_context || !global_registry || !local_registry ||
      !runtime_context || !lowering_context || !options ||
      !declaration) return;
  psx_toplevel_declaration_application_t application;
  if (!begin_declaration(
          &application, semantic_context, global_registry,
          local_registry, lowering_context, options, declaration))
    return;
  for (int i = 0; i < declaration->declarator_count; i++) {
    begin_declarator(
        &application, &declaration->declarators[i],
        &declaration->initializers[i]);
    finish_declarator(
        &application, &declaration->initializers[i]);
  }
}
