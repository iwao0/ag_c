#include "declaration_pipeline.h"

#include "diag/diag.h"
#include "lowering/global_object_lowering.h"
#include "lowering/local_object_lowering.h"
#include "lowering/static_local_lowering.h"
#include "lowering/vla_lowering.h"
#include "parser/decl.h"
#include "parser/declarator_shape_builder.h"
#include "parser/arena.h"
#include "parser/diag.h"
#include "parser/global_registry.h"
#include "parser/local_registry.h"
#include "semantic/declaration_application.h"
#include "frontend/semantic_pipeline_internal.h"
#include "parser/dynarray.h"
#include "parser/node_utils.h"
#include "parser/semantic_ctx.h"
#include "parser/type_builder.h"
#include "lowering/parameter_lowering.h"
#include "lowering/runtime_context.h"
#include "semantic/function_declaration_resolution.h"
#include "semantic/function_parameter_resolution.h"
#include "semantic/global_declaration_resolution.h"
#include "semantic/initializer_resolution.h"
#include "semantic/identifier_binding.h"
#include "semantic/local_declaration_resolution.h"
#include "semantic/static_initializer_resolution.h"
#include "semantic/parameter_declaration_resolution.h"


#include <stdlib.h>
#include <string.h>

void psx_declaration_pipeline_reset_translation_unit_state_in(
    psx_lowering_context_t *ctx) {
  ps_lowering_context_reset_translation_unit(ctx);
}

static void diagnose_global_declaration(
    const psx_global_declaration_pipeline_request_t *request,
    psx_global_declaration_status_t status) {
  ag_diagnostic_context_t *diagnostics =
      ps_ctx_diagnostics(request->semantic_context);
  switch (status) {
    case PSX_GLOBAL_DECLARATION_INCOMPLETE_OBJECT:
      ps_diag_ctx_in(
          diagnostics, request->diag_tok, "decl", "%s",
          diag_message_for_in(
              diagnostics,
                       DIAG_ERR_PARSER_INCOMPLETE_OBJECT_FORBIDDEN));
      return;
    case PSX_GLOBAL_DECLARATION_FUNCTION_NAME_CONFLICT:
      ps_diag_ctx_in(
          diagnostics, request->diag_tok, "decl",
          "'%.*s' は関数として既に宣言されています (C11 6.7p4)",
          request->name_len, request->name);
      return;
    case PSX_GLOBAL_DECLARATION_TYPEDEF_NAME_CONFLICT:
      ps_diag_ctx_in(
          diagnostics, request->diag_tok, "decl",
          "'%.*s' は typedef 名として既に宣言されています (C11 6.7p4)",
          request->name_len, request->name);
      return;
    case PSX_GLOBAL_DECLARATION_ENUM_NAME_CONFLICT:
      ps_diag_ctx_in(
          diagnostics, request->diag_tok, "decl",
          "'%.*s' は enum 定数として既に宣言されています (C11 6.7p4)",
          request->name_len, request->name);
      return;
    case PSX_GLOBAL_DECLARATION_TYPE_CONFLICT:
      ps_diag_ctx_in(
          diagnostics, request->diag_tok, "decl",
          "グローバル変数 '%.*s' の型が以前の宣言と異なります (C11 6.7p4)",
          request->name_len, request->name);
      return;
    default:
      ps_diag_ctx_in(
          diagnostics, request->diag_tok, "decl",
          "canonical global declaration resolution failed for '%.*s'",
          request->name_len, request->name);
  }
}

static void diagnose_static_initializer(
    ag_diagnostic_context_t *diagnostics,
    token_t *tok, const char *name, int name_len,
    psx_static_initializer_status_t status) {
  switch (status) {
    case PSX_STATIC_INITIALIZER_DUPLICATE_DEFINITION:
      ps_diag_ctx_in(
          diagnostics, tok, "decl",
          "グローバル変数 '%.*s' は重複定義されています (C11 6.9.2)",
          name_len, name);
      return;
    case PSX_STATIC_INITIALIZER_ARRAY_COMPLETION_FAILED:
      ps_diag_ctx_in(
          diagnostics, tok, "decl", "%s",
          diag_message_for_in(
              diagnostics,
                       DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
      return;
    case PSX_STATIC_INITIALIZER_INVALID_SCALAR_LIST:
      ps_diag_ctx_in(
          diagnostics, tok, "static-init", "%s",
          diag_message_for_in(
              diagnostics,
                       DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
      return;
    default:
      ps_diag_ctx_in(
          diagnostics, tok, "static-init", "%s",
          diag_message_for_in(
              diagnostics,
                       DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
  }
}

int psx_begin_global_declaration_pipeline(
    const psx_global_declaration_pipeline_request_t *request,
    psx_global_declaration_pipeline_result_t *result) {
  if (result) *result = (psx_global_declaration_pipeline_result_t){0};
  if (!request || !result || !request->semantic_context ||
      !request->global_registry || !request->local_registry ||
      !request->lowering_context || !request->options ||
      !request->name || request->name_len <= 0 || !request->type ||
      !request->initializer) return 0;

  if (ps_ctx_intern_qual_type_in(
          request->semantic_context, request->type).type_id ==
      PSX_TYPE_ID_INVALID) {
    return 0;
  }

  psx_global_declaration_resolution_t resolution;
  psx_resolve_global_declaration(
      &(psx_global_declaration_resolution_request_t){
          .semantic_context = request->semantic_context,
          .global_registry = request->global_registry,
          .name = request->name,
          .name_len = request->name_len,
          .type = request->type,
          .is_extern_decl = request->is_extern_decl,
          .has_initializer = request->initializer->has_initializer,
      },
      &resolution);
  if (resolution.status != PSX_GLOBAL_DECLARATION_OK)
    diagnose_global_declaration(request, resolution.status);

  psx_global_object_result_t lowered = {0};
  if (!lower_resolved_global_object_declaration(
          &(psx_resolved_global_object_request_t){
              .global_registry = request->global_registry,
              .name = request->name,
              .name_len = request->name_len,
              .type = request->type,
              .is_extern_decl = request->is_extern_decl,
              .is_static = request->is_static,
              .resolution = &resolution,
          },
          &lowered)) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(request->semantic_context),
        request->diag_tok, "decl",
        "canonical global storage planning failed for '%.*s'",
        request->name_len, request->name);
  }
  result->global = lowered.global;
  result->created = lowered.created;
  return 1;
}

int psx_finish_global_declaration_pipeline(
    const psx_global_declaration_pipeline_request_t *request,
    psx_global_declaration_pipeline_result_t *result) {
  if (!request || !result || !request->semantic_context ||
      !request->global_registry || !request->local_registry ||
      !request->lowering_context || !request->options ||
      !result->global || !request->initializer) return 0;
  global_var_t *global = result->global;
  if (!request->is_extern_decl) {
    global->is_thread_local = request->is_thread_local;
    if (request->initializer->has_initializer) {
      psx_static_initializer_resolution_t initializer_resolution;
      psx_resolve_static_initializer(
          &(psx_static_initializer_resolution_request_t){
              .semantic_context = request->semantic_context,
              .type = request->type,
              .kind = request->initializer->kind,
              .initializer = request->initializer->value,
              .diag_tok = request->initializer->value_tok,
              .already_initialized = global->has_init,
          },
          &initializer_resolution);
      if (initializer_resolution.status != PSX_STATIC_INITIALIZER_OK)
        diagnose_static_initializer(
            ps_ctx_diagnostics(request->semantic_context),
            request->initializer->value_tok
                ? request->initializer->value_tok : request->diag_tok,
            global->name, global->name_len,
            initializer_resolution.status);
      psx_frontend_expression_hir_t expression_hir = {
          .root = PSX_HIR_NODE_ID_INVALID,
      };
      psx_static_aggregate_initializer_plan_t aggregate_plan = {0};
      if (initializer_resolution.is_aggregate_initializer) {
        if (!psx_frontend_resolve_static_aggregate_initializer_plan_in_contexts(
                request->semantic_context,
                request->global_registry,
                request->local_registry,
                request->lowering_context,
                request->options,
                initializer_resolution.type,
                initializer_resolution.initializer,
                request->initializer->value_tok,
                &aggregate_plan)) {
          return 0;
        }
        initializer_resolution.aggregate_plan = &aggregate_plan;
      } else {
        if (!psx_frontend_resolve_expression_to_hir_in_contexts(
                request->semantic_context,
                request->global_registry,
                request->local_registry,
                request->lowering_context,
                request->options,
                initializer_resolution.initializer,
                initializer_resolution.initializer->tok
                    ? initializer_resolution.initializer->tok
                    : request->initializer->value_tok,
                &expression_hir)) {
          return 0;
        }
        initializer_resolution.initializer_hir =
            expression_hir.module;
        initializer_resolution.initializer_hir_root =
            expression_hir.root;
      }
      int lowered = lower_resolved_global_declaration_initializer(
          request->global_registry, request->lowering_context, global,
          &initializer_resolution,
          request->initializer->value_tok);
      psx_frontend_expression_hir_dispose(&expression_hir);
      if (!lowered) {
        ps_diag_ctx_in(
            ps_ctx_diagnostics(request->semantic_context),
            request->initializer->value_tok, "static-init", "%s",
            diag_message_for_in(
                ps_ctx_diagnostics(request->semantic_context),
                DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
      }
      result->initialized = 1;
    }
  }
  return 1;
}

int psx_apply_global_declaration_pipeline(
    const psx_global_declaration_pipeline_request_t *request,
    psx_global_declaration_pipeline_result_t *result) {
  if (!psx_begin_global_declaration_pipeline(request, result)) return 0;
  return psx_finish_global_declaration_pipeline(request, result);
}

static void diagnose_function_declaration(
    const psx_function_declaration_pipeline_request_t *request,
    psx_function_declaration_status_t status) {
  ag_diagnostic_context_t *diagnostics =
      ps_ctx_diagnostics(request->semantic_context);
  const char *context = request->diag_context ? request->diag_context : "decl";
  switch (status) {
    case PSX_FUNCTION_DECLARATION_OBJECT_NAME_CONFLICT:
      ps_diag_ctx_in(
          diagnostics, request->diag_tok, context,
          "'%.*s' はグローバル変数として既に宣言されています (C11 6.7p4)",
          request->name_len, request->name);
      return;
    case PSX_FUNCTION_DECLARATION_TYPE_CONFLICT:
      ps_diag_ctx_in(
          diagnostics, request->diag_tok, context,
          "関数 '%.*s' の型が以前の宣言と異なります (C11 6.7p3-4)",
          request->name_len, request->name);
      return;
    case PSX_FUNCTION_DECLARATION_DUPLICATE_DEFINITION:
      ps_diag_ctx_in(
          diagnostics, request->diag_tok, context,
          "関数 '%.*s' の重複定義 (C11 6.9p3)",
          request->name_len, request->name);
      return;
    default:
      ps_diag_ctx_in(
          diagnostics, request->diag_tok, context,
          "canonical function declaration resolution failed for '%.*s'",
          request->name_len, request->name);
  }
}

int psx_apply_function_declaration_pipeline(
    const psx_function_declaration_pipeline_request_t *request) {
  if (!request || !request->semantic_context || !request->global_registry ||
      !request->name || request->name_len <= 0 ||
      !request->function_type ||
      request->function_type->kind != PSX_TYPE_FUNCTION) return 0;
  psx_function_declaration_resolution_t resolution;
  psx_resolve_function_declaration(
      &(psx_function_declaration_resolution_request_t){
          .semantic_context = request->semantic_context,
          .global_registry = request->global_registry,
          .name = request->name,
          .name_len = request->name_len,
          .function_type = request->function_type,
          .is_definition = request->is_definition,
      },
      &resolution);
  if (resolution.status != PSX_FUNCTION_DECLARATION_OK)
    diagnose_function_declaration(request, resolution.status);
  if (request->function_node) {
    const psx_type_t *function_type =
        ps_function_symbol_type(resolution.function);
    request->function_node->signature = ps_type_clone_in(
        ps_ctx_arena(request->semantic_context), function_type);
  }
  return 1;
}

static const psx_runtime_array_bound_t *parameter_bound_for_op(
    const psx_runtime_declarator_application_t *application, int op_index) {
  for (int i = 0; application && i < application->array_bound_count; i++) {
    if (application->array_bounds[i].declarator_op_index == op_index)
      return &application->array_bounds[i];
  }
  return NULL;
}

static int resolve_definition_parameter(
    psx_semantic_context_t *semantic_context,
    const psx_type_t *base_type,
    const psx_runtime_declarator_application_t *application,
    psx_parameter_declaration_resolution_t *resolution) {
  psx_parameter_declaration_resolution_request_t semantic_request = {
      .type = {
          .semantic_context = semantic_context,
          .base_type = base_type,
          .declarator_shape = &application->shape,
      },
  };
  if (application->shape.count > 0) {
    semantic_request.inner_dimensions = arena_alloc_in(
        ps_ctx_arena(semantic_context),
        (size_t)application->shape.count *
        sizeof(*semantic_request.inner_dimensions));
  }
  int skip_outer_array =
      application->shape.count > 0 &&
      application->shape.ops[0].kind == PSX_DECL_OP_ARRAY;
  int saw_array = 0;
  for (int op_index = 0; op_index < application->shape.count; op_index++) {
    if (application->shape.ops[op_index].kind != PSX_DECL_OP_ARRAY) continue;
    if (skip_outer_array && saw_array++ == 0) continue;
    const psx_runtime_array_bound_t *bound =
        parameter_bound_for_op(application, op_index);
    psx_parameter_dimension_t *dimension =
        &semantic_request.inner_dimensions[
            semantic_request.inner_dimension_count++];
    dimension->expression_id =
        bound ? bound->expression_id : PSX_SEMANTIC_EXPR_ID_INVALID;
    dimension->constant_value =
        bound && bound->is_constant ? bound->constant_value : 0;
    dimension->is_constant = bound && bound->is_constant;
  }
  return psx_resolve_parameter_declaration(
      &semantic_request, resolution);
}

static int append_definition_parameter(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    psx_function_definition_pipeline_result_t *result, int *capacity,
    const psx_parsed_function_parameter_t *parameter) {
  const psx_type_t *base_type = psx_apply_parsed_decl_specifier_in_contexts(
      semantic_context, global_registry, local_registry,
      &parameter->specifier);
  if (!base_type) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(semantic_context),
        parameter->specifier.diagnostic_token, "param",
        "canonical parameter base type resolution failed");
  }
  psx_local_lookup_point_t parameter_lookup_point =
      ps_local_registry_capture_lookup_point_in(local_registry);
  psx_runtime_declarator_application_t applied;
  psx_apply_runtime_parsed_declarator_at_lookup_point_in_contexts(
      semantic_context, global_registry, local_registry,
      &parameter->declarator, &applied, -1,
      parameter_lookup_point);
  token_ident_t *name = parameter->declarator.identifier;
  int has_pointer = ps_declarator_shape_count_ops(
      &applied.shape, PSX_DECL_OP_POINTER) > 0;
  int has_array = ps_declarator_shape_count_ops(
      &applied.shape, PSX_DECL_OP_ARRAY) > 0;
  if (!name && base_type->kind == PSX_TYPE_VOID &&
      !has_pointer && !has_array && applied.shape.count == 0) {
    return -1;
  }

  psx_parameter_declaration_resolution_t resolution;
  if (!resolve_definition_parameter(
          semantic_context, base_type, &applied, &resolution)) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(semantic_context),
        parameter->declarator.diagnostic_token, "param",
        "canonical parameter declaration resolution failed");
  }
  if (result->nargs + 1 >= *capacity) {
    *capacity = pda_next_cap_in(
        ps_ctx_diagnostics(semantic_context),
        *capacity, result->nargs + 2);
    result->args = pda_xreallocarray_in(
        ps_ctx_diagnostics(semantic_context), result->args,
        (size_t)*capacity, sizeof(node_t *));
  }
  if (!name) {
    result->args[result->nargs++] =
        ps_node_new_param_placeholder_in(
            ps_lowering_arena(lowering_context), resolution.type);
    result->args[result->nargs] = NULL;
    return 1;
  }

  node_t **inner_dimension_expressions = NULL;
  if (resolution.inner_dimension_count > 0) {
    inner_dimension_expressions = arena_alloc_in(
        ps_ctx_arena(semantic_context),
        (size_t)resolution.inner_dimension_count *
        sizeof(*inner_dimension_expressions));
  }
  for (int i = 0; i < resolution.inner_dimension_count; i++) {
    inner_dimension_expressions[i] = ps_ctx_semantic_expression_in(
        semantic_context, resolution.inner_dimensions[i].expression_id);
    if (!resolution.inner_dimensions[i].is_constant &&
        !inner_dimension_expressions[i]) {
      ps_diag_ctx_in(
          ps_ctx_diagnostics(semantic_context),
          parameter->declarator.diagnostic_token, "param",
          "VLA parameter bound expression lookup failed");
    }
  }
  lvar_t *lowered = lower_resolved_parameter_declaration(
          &(psx_resolved_parameter_lowering_request_t){
              .local_registry = local_registry,
              .lowering_context = lowering_context,
              .name = name->str,
              .name_len = name->len,
              .resolution = &resolution,
              .inner_dimension_expressions =
                  inner_dimension_expressions,
              .diag_tok = parameter->declarator.diagnostic_token,
          });
  if (!lowered) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(semantic_context),
        parameter->declarator.diagnostic_token, "param",
        "canonical parameter lowering failed for '%.*s'",
        name->len, name->str);
  }
  result->args[result->nargs++] =
      ps_node_new_param_lvar_for_in(
          ps_lowering_arena(lowering_context), lowered);
  result->args[result->nargs] = NULL;
  return 0;
}

int psx_begin_function_definition_pipeline(
    const psx_function_definition_pipeline_request_t *request,
    psx_function_definition_pipeline_result_t *result,
    psx_function_definition_pipeline_state_t *state) {
  if (result) *result = (psx_function_definition_pipeline_result_t){0};
  if (state) *state = (psx_function_definition_pipeline_state_t){0};
  if (!request || !result || !request->base_type || !request->declarator ||
      !request->semantic_context || !request->global_registry ||
      !request->local_registry || !request->lowering_context ||
      !request->declarator->identifier ||
      request->declarator->function_suffix_count <= 0 || !state) return 0;

  const psx_parsed_function_suffix_t *primary_suffix =
      psx_declarator_outermost_function_suffix(
          request->declarator);
  if (!primary_suffix) return 0;
  psx_apply_runtime_parsed_declarator_ex_in_contexts(
      request->semantic_context, request->global_registry,
      request->local_registry,
      request->declarator, &state->application,
      primary_suffix->declarator_op_index);
  if (primary_suffix->declarator_op_index < 0 ||
      primary_suffix->declarator_op_index >= state->application.shape.count ||
      state->application.shape.ops[primary_suffix->declarator_op_index].kind !=
          PSX_DECL_OP_FUNCTION) {
    return 0;
  }

  state->semantic_context = request->semantic_context;
  state->global_registry = request->global_registry;
  state->local_registry = request->local_registry;
  state->lowering_context = request->lowering_context;
  state->base_type = request->base_type;
  state->result = result;
  state->primary_function_op_index =
      primary_suffix->declarator_op_index;
  state->parameter_count = primary_suffix->parameters
                               ? primary_suffix->parameters->count : 0;
  state->args_capacity = 16;
  result->args = calloc((size_t)state->args_capacity, sizeof(node_t *));
  return result->args != NULL;
}

int psx_apply_function_definition_parameter_pipeline(
    psx_function_definition_pipeline_state_t *state,
    const psx_parsed_function_parameter_t *parameter) {
  if (!state || !state->result || !parameter) return 0;
  int applied = append_definition_parameter(
      state->semantic_context, state->global_registry,
      state->local_registry, state->lowering_context, state->result,
      &state->args_capacity, parameter);
  if (applied < 0) {
    if (state->parameter_count != 1) {
      ps_diag_ctx_in(
          ps_ctx_diagnostics(state->semantic_context),
          parameter->specifier.diagnostic_token, "param",
          "void parameter must be the only parameter");
    }
    state->result->nargs = 0;
    state->result->args[0] = NULL;
    return 1;
  }
  if (applied > 0) state->result->has_unnamed_parameter = 1;
  return 1;
}

int psx_finish_function_definition_pipeline(
    psx_function_definition_pipeline_state_t *state) {
  if (!state || !state->result || !state->base_type) return 0;
  psx_function_definition_pipeline_result_t *result = state->result;
  psx_declarator_op_t *primary =
      &state->application.shape.ops[state->primary_function_op_index];
  const psx_type_t **parameter_types = result->nargs > 0
      ? calloc((size_t)result->nargs, sizeof(*parameter_types)) : NULL;
  for (int i = 0; i < result->nargs; i++)
    parameter_types[i] = ps_node_get_type(result->args[i]);
  psx_set_resolved_function_parameter_types(
      ps_ctx_arena(state->semantic_context), primary,
      parameter_types, result->nargs,
      primary->function_is_variadic);
  free(parameter_types);

  result->function_type = psx_resolve_decl_type(
      &(psx_decl_type_request_t){
          .semantic_context = state->semantic_context,
          .base_type = state->base_type,
          .declarator_shape = &state->application.shape,
      });
  return result->function_type &&
         result->function_type->kind == PSX_TYPE_FUNCTION &&
         result->function_type->base;
}

int psx_begin_static_local_declaration_pipeline(
    const psx_static_local_declaration_pipeline_request_t *request,
    psx_static_local_declaration_pipeline_result_t *result) {
  if (result) *result = (psx_static_local_declaration_pipeline_result_t){0};
  if (!request || !result || !request->semantic_context ||
      !request->global_registry || !request->local_registry ||
      !request->lowering_context || !request->options ||
      !request->name || request->name_len <= 0 || !request->type ||
      !request->initializer) return 0;
  if (ps_ctx_intern_qual_type_in(
          request->semantic_context, request->type).type_id ==
      PSX_TYPE_ID_INVALID) {
    return 0;
  }
  if (request->type->kind == PSX_TYPE_FUNCTION) return 0;
  if (request->type->kind == PSX_TYPE_VOID) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(request->semantic_context),
        request->diag_tok, "decl",
        diag_message_for_in(
            ps_ctx_diagnostics(request->semantic_context),
            DIAG_ERR_PARSER_VOID_OBJECT_FORBIDDEN),
        request->name_len, request->name);
  }
  if (ps_type_contains_vla_array(request->type)) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(request->semantic_context),
        request->diag_tok, "decl",
        "static local object '%.*s' cannot have variably modified type",
        request->name_len, request->name);
  }

  const psx_type_t *leaf = ps_type_array_leaf_type(request->type);
  int object_size = ps_lowering_type_size(
      request->lowering_context, request->type);
  int leaf_size = ps_lowering_type_size(
      request->lowering_context, leaf);
  if (leaf && ps_type_is_tag_aggregate(leaf) && leaf_size <= 0) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(request->semantic_context),
        request->diag_tok, "decl", "%s",
        diag_message_for_in(
            ps_ctx_diagnostics(request->semantic_context),
            DIAG_ERR_PARSER_INCOMPLETE_OBJECT_FORBIDDEN));
  }
  if (request->type->kind == PSX_TYPE_ARRAY) {
    if (leaf_size <= 0 ||
        (object_size <= 0 && !request->initializer->has_initializer)) {
      ps_diag_ctx_in(
          ps_ctx_diagnostics(request->semantic_context),
          request->diag_tok, "decl", "%s",
          diag_message_for_in(
              ps_ctx_diagnostics(request->semantic_context),
              DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
    }
  } else if (object_size <= 0) {
    return 0;
  }

  if (leaf && ps_type_is_tag_aggregate(leaf) && leaf->tag_name &&
      leaf->tag_len >= 11 &&
      memcmp(leaf->tag_name, "__anon_tag_", 11) == 0) {
    ps_ctx_promote_tag_to_file_scope_in(
        request->semantic_context,
        ps_type_tag_token_kind(leaf), leaf->tag_name, leaf->tag_len);
  }

  psx_static_local_kind_t kind = PSX_STATIC_LOCAL_SCALAR;
  if (request->type->kind == PSX_TYPE_ARRAY) {
    kind = leaf && ps_type_is_tag_aggregate(leaf)
               ? PSX_STATIC_LOCAL_AGGREGATE_ARRAY
               : PSX_STATIC_LOCAL_CONSUMED_ARRAY;
  } else if (ps_type_is_tag_aggregate(request->type)) {
    kind = PSX_STATIC_LOCAL_AGGREGATE;
  }

  psx_static_local_declaration_result_t storage = {0};
  if (!lower_static_local_declaration_storage(
          &(psx_static_local_declaration_request_t){
              .global_registry = request->global_registry,
              .local_registry = request->local_registry,
              .lowering_context = request->lowering_context,
              .kind = kind,
              .function_name = request->function_name,
              .function_name_len = request->function_name_len,
              .name = request->name,
              .name_len = request->name_len,
              .type = request->type,
          },
          &storage)) {
    return 0;
  }
  result->global = storage.global;
  result->alias = storage.alias;

  return 1;
}

int psx_finish_static_local_declaration_pipeline(
    const psx_static_local_declaration_pipeline_request_t *request,
    psx_static_local_declaration_pipeline_result_t *result) {
  if (!request || !result || !request->semantic_context ||
      !request->global_registry || !request->local_registry ||
      !request->lowering_context || !request->options ||
      !result->global || !result->alias || !request->initializer) return 0;
  if (!request->initializer->has_initializer) return 1;

  psx_static_initializer_resolution_t resolution;
  psx_resolve_static_initializer(
      &(psx_static_initializer_resolution_request_t){
          .semantic_context = request->semantic_context,
          .type = request->type,
          .kind = request->initializer->kind,
          .initializer = request->initializer->value,
          .diag_tok = request->initializer->value_tok,
      },
      &resolution);
  if (resolution.status != PSX_STATIC_INITIALIZER_OK) {
    diagnose_static_initializer(
        ps_ctx_diagnostics(request->semantic_context),
        request->initializer->value_tok
            ? request->initializer->value_tok : request->diag_tok,
        request->name, request->name_len, resolution.status);
  }
  psx_frontend_expression_hir_t expression_hir = {
      .root = PSX_HIR_NODE_ID_INVALID,
  };
  psx_static_aggregate_initializer_plan_t aggregate_plan = {0};
  if (resolution.is_aggregate_initializer) {
    if (!psx_frontend_resolve_static_aggregate_initializer_plan_in_contexts(
            request->semantic_context, request->global_registry,
            request->local_registry, request->lowering_context,
            request->options, resolution.type,
            resolution.initializer, request->initializer->value_tok,
            &aggregate_plan)) {
      return 0;
    }
    resolution.aggregate_plan = &aggregate_plan;
  } else {
    if (!psx_frontend_resolve_expression_to_hir_in_contexts(
            request->semantic_context, request->global_registry,
            request->local_registry, request->lowering_context,
            request->options, resolution.initializer,
            resolution.initializer->tok
                ? resolution.initializer->tok
                : request->initializer->value_tok,
            &expression_hir)) {
      return 0;
    }
    resolution.initializer_hir = expression_hir.module;
    resolution.initializer_hir_root = expression_hir.root;
  }
  int lowered = lower_static_local_declaration_initializer(
      request->global_registry, request->lowering_context,
      result->global, &resolution, request->initializer->value_tok,
      &result->type_completed);
  psx_frontend_expression_hir_dispose(&expression_hir);
  if (!lowered) {
    return 0;
  }
  if (result->type_completed &&
      !ps_local_registry_complete_array_type(
          request->local_registry, result->alias, resolution.type))
    return 0;
  result->initialized = 1;
  return 1;
}

int psx_apply_static_local_declaration_pipeline(
    const psx_static_local_declaration_pipeline_request_t *request,
    psx_static_local_declaration_pipeline_result_t *result) {
  if (!psx_begin_static_local_declaration_pipeline(request, result))
    return 0;
  return psx_finish_static_local_declaration_pipeline(request, result);
}

static void diagnose_local_declaration(
    const psx_automatic_local_declaration_pipeline_request_t *request,
    psx_local_declaration_status_t status) {
  ag_diagnostic_context_t *diagnostics =
      ps_lowering_diagnostics(request->lowering_context);
  switch (status) {
    case PSX_LOCAL_DECLARATION_VOID_OBJECT:
      ps_diag_ctx_in(
          diagnostics, request->diag_tok, "decl",
          diag_message_for_in(
              diagnostics, DIAG_ERR_PARSER_VOID_OBJECT_FORBIDDEN),
          request->name_len, request->name);
      return;
    case PSX_LOCAL_DECLARATION_INCOMPLETE_OBJECT:
      ps_diag_ctx_in(
          diagnostics, request->diag_tok, "decl", "%s",
          diag_message_for_in(
              diagnostics,
              DIAG_ERR_PARSER_INCOMPLETE_OBJECT_FORBIDDEN));
      return;
    case PSX_LOCAL_DECLARATION_INCOMPLETE_ARRAY_NEEDS_INITIALIZER:
      ps_diag_ctx_in(
          diagnostics, request->diag_tok, "decl", "%s",
          diag_message_for_in(
              diagnostics,
              DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
      return;
    case PSX_LOCAL_DECLARATION_VLA_INITIALIZER_FORBIDDEN:
      ps_diag_ctx_in(
          diagnostics, request->diag_tok, "decl",
          "variable length array '%.*s' cannot be initialized",
          request->name_len, request->name);
      return;
    default:
      ps_diag_ctx_in(
          diagnostics, request->diag_tok, "decl",
          "canonical local declaration resolution failed for '%.*s'",
          request->name_len, request->name);
  }
}

static node_t *append_local_initialization(
    psx_lowering_context_t *lowering_context, node_t *chain, node_t *node) {
  if (!node) return chain;
  return chain ? ps_node_new_binary_for_target_in(
                     ps_lowering_arena(lowering_context),
                     ps_lowering_target(lowering_context),
                     ND_COMMA, chain, node)
               : node;
}

int psx_begin_automatic_local_declaration_pipeline(
    const psx_automatic_local_declaration_pipeline_request_t *request,
    psx_automatic_local_declaration_pipeline_result_t *result) {
  if (result)
    *result = (psx_automatic_local_declaration_pipeline_result_t){0};
  if (!request || !result || !request->name || request->name_len <= 0 ||
      !request->type || !request->application || !request->initializer ||
      !request->semantic_context ||
      !request->local_registry || !request->lowering_context)
    return 0;

  psx_qual_type_t declaration_identity = ps_ctx_intern_qual_type_in(
      request->semantic_context, request->type);
  if (declaration_identity.type_id == PSX_TYPE_ID_INVALID) {
    return 0;
  }

  psx_local_declaration_resolution_t resolution;
  psx_resolve_local_declaration(
      &(psx_local_declaration_resolution_request_t){
          .arena_context = ps_lowering_arena(request->lowering_context),
          .semantic_types = ps_ctx_semantic_type_table_in(
              request->semantic_context),
          .record_layouts = ps_ctx_record_layout_table_in(
              request->semantic_context),
          .type_id = declaration_identity.type_id,
          .target = ps_lowering_target(request->lowering_context),
          .application = request->application,
          .has_initializer = request->initializer->has_initializer,
      },
      &resolution);
  if (resolution.status != PSX_LOCAL_DECLARATION_OK)
    diagnose_local_declaration(request, resolution.status);

  psx_vla_lowering_result_t vla = {0};
  switch (resolution.storage_kind) {
    case PSX_LOCAL_STORAGE_COMPLETE:
      result->var = lower_complete_local_object(
              &(psx_local_object_request_t){
                  .local_registry = request->local_registry,
                  .lowering_context = request->lowering_context,
                  .name = request->name,
                  .name_len = request->name_len,
                  .type = request->type,
                  .requested_alignment = request->requested_alignment,
                  .diag_tok = request->diag_tok,
              });
      if (!result->var) return 0;
      break;
    case PSX_LOCAL_STORAGE_INCOMPLETE_ARRAY:
      result->var = declare_incomplete_local_object(
              &(psx_local_object_request_t){
                  .local_registry = request->local_registry,
                  .lowering_context = request->lowering_context,
                  .name = request->name,
                  .name_len = request->name_len,
                  .type = request->type,
                  .requested_alignment = request->requested_alignment,
                  .diag_tok = request->diag_tok,
              });
      if (!result->var) return 0;
      break;
    case PSX_LOCAL_STORAGE_VLA_OBJECT: {
      psx_vla_lowering_request_t lowering = {
          .local_registry = request->local_registry,
          .lowering_context = request->lowering_context,
          .name = request->name,
          .name_len = request->name_len,
          .dimension_count = resolution.dimension_count,
          .type = request->type,
          .requested_alignment = request->requested_alignment,
          .diag_tok = request->diag_tok,
      };
      lowering.dimensions = arena_alloc_in(
          ps_lowering_arena(request->lowering_context),
          (size_t)resolution.dimension_count * sizeof(*lowering.dimensions));
      lowering.is_const = arena_alloc_in(
          ps_lowering_arena(request->lowering_context),
          (size_t)resolution.dimension_count * sizeof(*lowering.is_const));
      for (int i = 0; i < resolution.dimension_count; i++) {
        lowering.dimensions[i] = ps_ctx_semantic_expression_in(
            request->semantic_context,
            resolution.dimensions[i].expression_id);
        if (!lowering.dimensions[i]) return 0;
        lowering.is_const[i] = resolution.dimensions[i].is_constant;
      }
      vla = lower_vla_declaration(&lowering);
      result->var = vla.var;
      result->initialization = vla.init;
      break;
    }
    case PSX_LOCAL_STORAGE_POINTER_TO_VLA: {
      node_t *row_dimension = ps_ctx_semantic_expression_in(
          request->semantic_context,
          resolution.pointer_row_dimension_id);
      if (!row_dimension) return 0;
      vla = lower_pointer_to_vla_declaration(
          &(psx_pointer_vla_lowering_request_t){
              .local_registry = request->local_registry,
              .lowering_context = request->lowering_context,
              .name = request->name,
              .name_len = request->name_len,
              .row_dimension = row_dimension,
              .type = request->type,
              .requested_alignment = request->requested_alignment,
              .diag_tok = request->diag_tok,
          });
      result->var = vla.var;
      result->initialization = vla.init;
      break;
    }
  }
  if (!result->var) return 0;

  return 1;
}

int psx_finish_automatic_local_declaration_pipeline(
    const psx_automatic_local_declaration_pipeline_request_t *request,
    psx_automatic_local_declaration_pipeline_result_t *result) {
  if (!request || !result || !result->var || !request->initializer)
    return 0;
  if (ps_type_is_incomplete_array(request->type)) {
    psx_type_t *completed_type = ps_type_clone_in(
        ps_lowering_arena(request->lowering_context), request->type);
    if (!completed_type) return 0;
    if (!request->initializer->has_initializer ||
        !psx_resolve_incomplete_array_initializer(
            request->semantic_context, completed_type,
            request->initializer->kind,
            request->initializer->value)) {
      ps_diag_ctx_in(
          ps_lowering_diagnostics(request->lowering_context),
          request->initializer->value_tok, "decl", "%s",
          diag_message_for_in(
              ps_lowering_diagnostics(request->lowering_context),
              DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
    }
    if (ps_ctx_intern_qual_type_in(
            request->semantic_context, completed_type).type_id ==
        PSX_TYPE_ID_INVALID) {
      return 0;
    }
    if (!complete_declared_local_object(
            result->var,
            &(psx_local_object_request_t){
                .local_registry = request->local_registry,
                .lowering_context = request->lowering_context,
                .name = request->name,
                .name_len = request->name_len,
                .type = completed_type,
                .requested_alignment = request->requested_alignment,
                .diag_tok = request->diag_tok,
            })) {
      return 0;
    }
  }

  if (request->initializer->has_initializer) {
    node_t *initializer = ps_decl_bind_initializer_for_var_in(
        ps_lowering_arena(request->lowering_context),
        result->var, request->initializer->value,
        request->initializer->kind,
        request->initializer->value_tok);
    result->initialization = append_local_initialization(
        request->lowering_context, result->initialization, initializer);
  }
  return 1;
}

int psx_apply_automatic_local_declaration_pipeline(
    const psx_automatic_local_declaration_pipeline_request_t *request,
    psx_automatic_local_declaration_pipeline_result_t *result) {
  if (!psx_begin_automatic_local_declaration_pipeline(
          request, result)) {
    return 0;
  }
  return psx_finish_automatic_local_declaration_pipeline(
      request, result);
}

int psx_apply_block_extern_declaration_pipeline(
    const psx_block_extern_declaration_pipeline_request_t *request) {
  if (!request || !request->semantic_context || !request->global_registry ||
      !request->local_registry || !request->lowering_context ||
      !request->options ||
      !request->name || request->name_len <= 0 ||
      !request->type) return 0;
  if (request->has_initializer) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(request->semantic_context),
        request->diag_tok, "decl",
        "block scope extern declaration '%.*s' cannot have an initializer",
        request->name_len, request->name);
  }

  if (request->type->kind == PSX_TYPE_FUNCTION) {
    if (!psx_apply_function_declaration_pipeline(
            &(psx_function_declaration_pipeline_request_t){
                .semantic_context = request->semantic_context,
                .global_registry = request->global_registry,
                .name = request->name,
                .name_len = request->name_len,
                .function_type = request->type,
                .diag_context = "block-extern",
                .diag_tok = request->diag_tok,
            })) {
      return 0;
    }
    return 1;
  }

  psx_parsed_initializer_t initializer = {0};
  psx_global_declaration_pipeline_result_t global;
  if (!psx_apply_global_declaration_pipeline(
          &(psx_global_declaration_pipeline_request_t){
              .semantic_context = request->semantic_context,
              .global_registry = request->global_registry,
              .local_registry = request->local_registry,
              .lowering_context = request->lowering_context,
              .options = request->options,
              .name = request->name,
              .name_len = request->name_len,
              .type = request->type,
              .is_extern_decl = 1,
              .initializer = &initializer,
              .diag_tok = request->diag_tok,
          },
          &global)) {
    return 0;
  }
  return 1;
}

lvar_t *psx_apply_temporary_local_declaration_pipeline(
    const psx_temporary_local_declaration_pipeline_request_t *request) {
  if (!request || !request->name || request->name_len <= 0 ||
      !request->type || !request->local_registry ||
      !request->lowering_context) {
    return NULL;
  }
  return lower_complete_local_object(
      &(psx_local_object_request_t){
          .local_registry = request->local_registry,
          .lowering_context = request->lowering_context,
          .name = request->name,
          .name_len = request->name_len,
          .type = request->type,
          .requested_alignment = request->requested_alignment,
      });
}
