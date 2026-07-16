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
#include "../semantic/vla_runtime_plan.h"

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

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_global_registry_t *global_registry;
  psx_local_registry_t *local_registry;
  psx_lowering_context_t *lowering_context;
  const ag_compilation_options_t *options;
} psx_local_declaration_resolver_t;

static int resolve_local_declarations_in_slot(
    psx_local_declaration_resolver_t *resolver, node_t **slot);
static int resolve_initializer_declarations(
    psx_local_declaration_resolver_t *resolver, node_t **slot);

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

static node_t *apply_local_declaration_syntax(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    psx_parsed_local_declaration_t *declaration,
    psx_local_declaration_resolver_t *resolver) {
  if (!semantic_context || !global_registry || !local_registry ||
      !lowering_context || !options || !declaration)
    return NULL;
  ps_prepare_decl_specifier_alignments_in_context(
      &declaration->specifier, semantic_context,
      &(psx_name_classifier_t){
          .context = semantic_context,
          .is_typedef_name =
              ps_ctx_name_classifier(semantic_context).is_typedef_name,
      });
  psx_local_declaration_callbacks_t callbacks = {
      .semantic_context = semantic_context,
      .global_registry = global_registry,
      .local_registry = local_registry,
      .lowering_context = lowering_context,
      .options = options,
  };
  void *application = begin_declaration(
      &callbacks, &declaration->specifier,
      declaration->is_typedef, declaration->is_standalone_tag);
  if (declaration->is_standalone_tag)
    return finish_declaration(application);
  for (int i = 0; i < declaration->declarator_count; i++) {
    begin_declarator(
        application, &declaration->declarators[i],
        &declaration->initializers[i]);
    if (resolver && declaration->initializers[i].has_initializer &&
        !resolve_initializer_declarations(
            resolver, &declaration->initializers[i].value)) {
      abort_declaration(application);
      return NULL;
    }
    finish_declarator(
        application, &declaration->initializers[i]);
  }
  return finish_declaration(application);
}

node_t *psx_apply_local_declaration_syntax_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    psx_parsed_local_declaration_t *declaration) {
  return apply_local_declaration_syntax(
      semantic_context, global_registry, local_registry,
      lowering_context, options, declaration, NULL);
}

static int resolve_type_name_declarations(
    psx_local_declaration_resolver_t *resolver,
    psx_type_name_ref_t *type_name) {
  if (!type_name || !type_name->syntax) return 1;
  psx_parsed_declarator_t *declarator =
      &type_name->syntax->declarator;
  for (int i = 0; i < declarator->array_bound_count; i++) {
    if (!resolve_local_declarations_in_slot(
            resolver,
            &declarator->array_bounds[i].expression.node))
      return 0;
  }
  if (type_name->syntax->atomic_inner) {
    psx_type_name_ref_t inner = {
        .syntax = type_name->syntax->atomic_inner,
    };
    if (!resolve_type_name_declarations(resolver, &inner))
      return 0;
  }
  return 1;
}

static int resolve_initializer_declarations(
    psx_local_declaration_resolver_t *resolver, node_t **slot) {
  if (!slot || !*slot) return 1;
  if ((*slot)->kind != ND_INIT_LIST)
    return resolve_local_declarations_in_slot(resolver, slot);
  node_init_list_t *list = (node_init_list_t *)*slot;
  for (int i = 0; i < list->entry_count; i++) {
    psx_initializer_entry_t *entry = &list->entries[i];
    for (int d = 0; d < entry->designator_count; d++) {
      if (!resolve_local_declarations_in_slot(
              resolver, &entry->designators[d].index_expr) ||
          !resolve_local_declarations_in_slot(
              resolver, &entry->designators[d].range_end_expr))
        return 0;
    }
    for (int d = 0; d < entry->index_expr_count; d++) {
      if (!resolve_local_declarations_in_slot(
              resolver, &entry->index_exprs[d]))
        return 0;
    }
    if (!resolve_initializer_declarations(
            resolver, &entry->value))
      return 0;
  }
  return 1;
}

static int resolve_block_declarations(
    psx_local_declaration_resolver_t *resolver,
    node_block_t *block) {
  ps_ctx_enter_block_scope_in(resolver->semantic_context);
  ps_decl_enter_scope_in(resolver->local_registry);
  int ok = 1;
  for (int i = 0; block && block->body && block->body[i]; i++) {
    if (!resolve_local_declarations_in_slot(
            resolver, &block->body[i])) {
      ok = 0;
      break;
    }
  }
  ps_decl_leave_scope_in(resolver->local_registry);
  ps_ctx_leave_block_scope_in(resolver->semantic_context);
  return ok;
}

static int resolve_function_body_declarations(
    psx_local_declaration_resolver_t *resolver,
    node_t **body_slot) {
  if (!body_slot || !*body_slot) return 1;
  if ((*body_slot)->kind != ND_BLOCK)
    return resolve_local_declarations_in_slot(
        resolver, body_slot);
  ps_ctx_enter_block_scope_in(resolver->semantic_context);
  node_block_t *body = (node_block_t *)*body_slot;
  int ok = 1;
  for (int i = 0; body->body && body->body[i]; i++) {
    if (!resolve_local_declarations_in_slot(
            resolver, &body->body[i])) {
      ok = 0;
      break;
    }
  }
  ps_ctx_leave_block_scope_in(resolver->semantic_context);
  return ok;
}

static int resolve_statement_expression_declarations(
    psx_local_declaration_resolver_t *resolver, node_t *node) {
  node_block_t *block =
      node->lhs && node->lhs->kind == ND_BLOCK
          ? (node_block_t *)node->lhs : NULL;
  node_t *value = node->rhs;
  int value_index = -1;
  for (int i = 0; block && block->body && block->body[i]; i++) {
    if (block->body[i] == value) {
      value_index = i;
      break;
    }
  }
  if (!resolve_local_declarations_in_slot(
          resolver, &node->lhs))
    return 0;
  block = node->lhs && node->lhs->kind == ND_BLOCK
              ? (node_block_t *)node->lhs : NULL;
  if (value_index >= 0 && block && block->body)
    node->rhs = block->body[value_index];
  else if (!resolve_local_declarations_in_slot(
               resolver, &node->rhs))
    return 0;
  return 1;
}

static int resolve_local_declarations_in_slot(
    psx_local_declaration_resolver_t *resolver, node_t **slot) {
  if (!slot || !*slot) return 1;
  node_t *node = *slot;
  if (node->kind == ND_LOCAL_DECLARATION) {
    psx_lvar_usage_region_t *previous_usage_region =
        ps_local_registry_set_current_usage_region_in(
            resolver->local_registry, node->usage_region);
    node_t *replacement =
        apply_local_declaration_syntax(
            resolver->semantic_context,
            resolver->global_registry,
            resolver->local_registry,
            resolver->lowering_context,
            resolver->options,
            ((node_local_declaration_t *)node)->declaration,
            resolver);
    ps_local_registry_set_current_usage_region_in(
        resolver->local_registry, previous_usage_region);
    if (!replacement) return 0;
    if (!replacement->tok) replacement->tok = node->tok;
    if (!replacement->usage_region)
      replacement->usage_region = node->usage_region;
    *slot = replacement;
    return 1;
  }

  switch (node->kind) {
    case ND_BLOCK:
      return resolve_block_declarations(
          resolver, (node_block_t *)node);
    case ND_FUNCDEF: {
      node_function_definition_t *function =
          (node_function_definition_t *)node;
      char *previous_name = NULL;
      int previous_name_len = 0;
      ps_decl_get_current_funcname_in(
          resolver->local_registry,
          &previous_name, &previous_name_len);
      ps_decl_set_current_funcname_in(
          resolver->local_registry,
          function->name, function->name_len);
      int ok = resolve_function_body_declarations(
          resolver, &node->rhs);
      ps_decl_set_current_funcname_in(
          resolver->local_registry,
          previous_name, previous_name_len);
      return ok;
    }
    case ND_STATIC_ASSERT:
      return resolve_local_declarations_in_slot(
          resolver,
          &((node_static_assert_t *)node)->condition);
    case ND_STMT_EXPR:
      return resolve_statement_expression_declarations(
          resolver, node);
    case ND_COMPOUND_LITERAL:
      return resolve_type_name_declarations(
                 resolver,
                 &((node_compound_literal_t *)node)->type_name) &&
             resolve_initializer_declarations(
                 resolver, &node->rhs);
    case ND_CAST:
      if (node->is_source_cast &&
          !resolve_type_name_declarations(
              resolver,
              &((node_source_cast_t *)node)->type_name))
        return 0;
      return resolve_local_declarations_in_slot(
          resolver, &node->lhs);
    case ND_GENERIC_SELECTION: {
      node_generic_selection_t *selection =
          (node_generic_selection_t *)node;
      if (!resolve_local_declarations_in_slot(
              resolver, &selection->control))
        return 0;
      for (int i = 0; i < selection->association_count; i++) {
        if (!selection->associations[i].is_default &&
            !resolve_type_name_declarations(
                resolver,
                &selection->associations[i].type_name))
          return 0;
        if (!resolve_local_declarations_in_slot(
                resolver,
                &selection->associations[i].expression))
          return 0;
      }
      return 1;
    }
    case ND_SIZEOF_QUERY: {
      node_sizeof_query_t *query =
          (node_sizeof_query_t *)node;
      return resolve_type_name_declarations(
                 resolver, &query->type_name) &&
             resolve_local_declarations_in_slot(
                 resolver, &query->operand);
    }
    case ND_ALIGNOF_QUERY:
      return resolve_type_name_declarations(
          resolver,
          &((node_alignof_query_t *)node)->type_name);
    case ND_INIT_LIST:
      return resolve_initializer_declarations(
          resolver, slot);
    case ND_DECL_INIT: {
      node_decl_init_t *initializer = (node_decl_init_t *)node;
      return resolve_local_declarations_in_slot(
                 resolver, &node->lhs) &&
             (initializer->init_kind == PSX_DECL_INIT_LIST
                  ? resolve_initializer_declarations(
                        resolver, &node->rhs)
                  : resolve_local_declarations_in_slot(
                        resolver, &node->rhs));
    }
    case ND_FUNCALL: {
      node_function_call_t *call =
          (node_function_call_t *)node;
      if (!resolve_local_declarations_in_slot(
              resolver, &call->callee))
        return 0;
      for (int i = 0; i < call->argument_count; i++) {
        if (!resolve_local_declarations_in_slot(
                resolver, &call->arguments[i]))
          return 0;
      }
      return 1;
    }
    case ND_FOR: {
      node_ctrl_t *control = (node_ctrl_t *)node;
      int has_declaration_scope =
          control->init &&
          (control->init->kind == ND_LOCAL_DECLARATION ||
           control->init->kind == ND_STATIC_ASSERT);
      if (has_declaration_scope)
        ps_decl_enter_scope_in(resolver->local_registry);
      int ok =
          resolve_local_declarations_in_slot(
              resolver, &control->init) &&
          resolve_local_declarations_in_slot(
              resolver, &node->lhs) &&
          resolve_local_declarations_in_slot(
              resolver, &control->inc) &&
          resolve_local_declarations_in_slot(
              resolver, &node->rhs) &&
          resolve_local_declarations_in_slot(
              resolver, &control->els);
      if (has_declaration_scope)
        ps_decl_leave_scope_in(resolver->local_registry);
      return ok;
    }
    case ND_IF:
    case ND_TERNARY: {
      node_ctrl_t *control = (node_ctrl_t *)node;
      return resolve_local_declarations_in_slot(
                 resolver, &control->init) &&
             resolve_local_declarations_in_slot(
                 resolver, &node->lhs) &&
             resolve_local_declarations_in_slot(
                 resolver, &node->rhs) &&
             resolve_local_declarations_in_slot(
                 resolver, &control->inc) &&
             resolve_local_declarations_in_slot(
                 resolver, &control->els);
    }
    case ND_VLA_ALLOC: {
      psx_vla_runtime_plan_t *plan =
          ((node_vla_alloc_t *)node)->runtime_plan;
      for (int i = 0; plan && i < plan->dimension_count; i++) {
        if (!resolve_local_declarations_in_slot(
                resolver, &plan->dimensions[i]))
          return 0;
      }
      return 1;
    }
    default:
      return resolve_local_declarations_in_slot(
                 resolver, &node->lhs) &&
             resolve_local_declarations_in_slot(
                 resolver, &node->rhs);
  }
}

int psx_resolve_local_declaration_syntax_tree_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    node_t **root) {
  if (!semantic_context || !global_registry || !local_registry ||
      !lowering_context || !options || !root)
    return 0;
  psx_local_declaration_resolver_t resolver = {
      .semantic_context = semantic_context,
      .global_registry = global_registry,
      .local_registry = local_registry,
      .lowering_context = lowering_context,
      .options = options,
  };
  return resolve_local_declarations_in_slot(
      &resolver, root);
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
