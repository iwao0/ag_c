#include "declaration_pipeline.h"

#include "diag/diag.h"
#include "lowering/global_object_lowering.h"
#include "lowering/local_object_lowering.h"
#include "lowering/static_data_initializer.h"
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
#include "lowering/parameter_lowering.h"
#include "lowering/runtime_context.h"
#include "semantic/function_declaration_resolution.h"
#include "semantic/function_parameter_resolution.h"
#include "semantic/global_declaration_resolution.h"
#include "semantic/initializer_resolution.h"
#include "semantic/assignment_resolution.h"
#include "semantic/scope_graph.h"
#include "source_manager.h"
#include "semantic/local_declaration_resolution.h"
#include "semantic/static_initializer_resolution.h"
#include "semantic/static_initializer_materialization.h"
#include "semantic/syntax_typed_hir_resolution.h"
#include "semantic/typed_hir_materialization.h"
#include "semantic/parameter_declaration_resolution.h"


#include <stdlib.h>
#include <string.h>

static void note_pipeline_declaration_source(
    psx_semantic_context_t *semantic_context, psx_scope_id_t scope_id,
    psx_c_namespace_t name_space, const char *name, int name_len,
    token_t *token) {
  if (!semantic_context || !name || name_len <= 0 || !token) return;
  psx_scope_graph_t *graph = ps_ctx_scope_graph(semantic_context);
  psx_decl_id_t declaration_id = psx_scope_graph_lookup_in_scope(
      graph, scope_id, name_space, name, name_len);
  ag_source_manager_t *sources = diag_context_source_manager(
      ps_ctx_diagnostics(semantic_context));
  (void)psx_scope_graph_note_declaration_source(
      graph, declaration_id,
      ag_source_manager_name(sources, token->file_name_id),
      token->source_input, token->byte_offset, token->byte_length);
}

static int prepare_local_vla_dimensions(
    psx_semantic_context_t *semantic_context,
    psx_lowering_context_t *lowering_context,
    const psx_local_declaration_resolution_t *resolution,
    psx_vla_runtime_dimension_t **out_dimensions,
    psx_qual_type_t *out_constant_qual_type) {
  if (out_dimensions) *out_dimensions = NULL;
  if (out_constant_qual_type)
    *out_constant_qual_type = (psx_qual_type_t){
        PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  if (!semantic_context || !lowering_context || !resolution ||
      !out_dimensions || !out_constant_qual_type ||
      resolution->dimension_count <= 0 || !resolution->dimensions)
    return 0;

  psx_vla_runtime_dimension_t *dimensions = arena_alloc_in(
      ps_lowering_arena(lowering_context),
      (size_t)resolution->dimension_count * sizeof(*dimensions));
  psx_qual_type_t constant_qual_type =
      ps_ctx_intern_integer_qual_type_in(
          semantic_context, PSX_INTEGER_KIND_INT, 0, 0);
  if (!dimensions ||
      constant_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return 0;
  for (int i = 0; i < resolution->dimension_count; i++) {
    const psx_local_vla_dimension_t *dimension =
        &resolution->dimensions[i];
    dimensions[i] = (psx_vla_runtime_dimension_t){
        .expression_id = dimension->expression_id,
        .constant_value = dimension->constant_value,
        .is_constant = dimension->is_constant ? 1 : 0,
    };
    if ((dimension->is_constant && dimension->constant_value <= 0) ||
        (!dimension->is_constant &&
         !ps_ctx_semantic_expression_in(
             semantic_context, dimension->expression_id)))
      return 0;
  }
  *out_dimensions = dimensions;
  *out_constant_qual_type = constant_qual_type;
  return 1;
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
    case PSX_GLOBAL_DECLARATION_LINKAGE_CONFLICT:
      ps_diag_ctx_in(
          diagnostics, request->diag_tok, "decl",
          "declaration of '%.*s' changes its language linkage",
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
      !request->name || request->name_len <= 0 ||
      request->type.type_id == PSX_TYPE_ID_INVALID ||
      !request->initializer) return 0;

  psx_global_declaration_resolution_t resolution;
  psx_resolve_global_declaration(
      &(psx_global_declaration_resolution_request_t){
          .semantic_context = request->semantic_context,
          .name = request->name,
          .name_len = request->name_len,
          .type = request->type,
          .is_extern_decl = request->is_extern_decl,
          .is_static = request->is_static,
          .has_initializer = request->initializer->has_initializer,
      },
      &resolution);
  if (resolution.status != PSX_GLOBAL_DECLARATION_OK)
    diagnose_global_declaration(request, resolution.status);
  if (resolution.existing &&
      resolution.existing->is_thread_local !=
          (unsigned int)(request->is_thread_local != 0)) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(request->semantic_context),
        request->diag_tok, "decl",
        "every declaration of '%.*s' must consistently use '_Thread_local'",
        request->name_len, request->name);
  }

  psx_global_object_result_t lowered = {0};
  if (!lower_resolved_global_object_declaration(
          &(psx_resolved_global_object_request_t){
              .global_registry = request->global_registry,
              .name = request->name,
              .name_len = request->name_len,
              .is_extern_decl = request->is_extern_decl,
              .is_static = request->is_static,
              .is_compiler_generated = request->is_compiler_generated,
              .is_compound_literal = request->is_compound_literal,
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
  if (result->created && result->global)
    result->global->is_thread_local = request->is_thread_local ? 1 : 0;
  if (result->global &&
      request->requested_alignment > result->global->requested_alignment)
    result->global->requested_alignment = request->requested_alignment;
  note_pipeline_declaration_source(
      request->semantic_context, PSX_SCOPE_ID_TRANSLATION_UNIT,
      PSX_NAMESPACE_ORDINARY, request->name, request->name_len,
      request->diag_tok);
  return 1;
}

static const node_t *selected_static_initializer_syntax(
    const psx_parsed_initializer_t *initializer,
    const psx_static_initializer_resolution_t *resolution) {
  if (!initializer || !initializer->value || !resolution)
    return NULL;
  if (!resolution->scalar_list_value_selected)
    return initializer->value;
  if (initializer->value->kind != ND_INIT_LIST)
    return NULL;
  const node_init_list_t *list =
      (const node_init_list_t *)initializer->value;
  return list->entry_count == 1 && list->entries
             ? list->entries[0].value : NULL;
}

static int static_initializer_is_null_pointer_constant(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const node_t *initializer) {
  const psx_typed_hir_tree_t *typed_hir = NULL;
  psx_syntax_integer_constant_result_t constant = {0};
  psx_resolved_hir_build_failure_t failure = {0};
  return initializer &&
         psx_resolve_syntax_integer_constant_expression_direct_to_typed_hir_in_contexts(
             semantic_context, global_registry, local_registry,
             NULL, initializer, &typed_hir,
             &constant, &failure) == PSX_SYNTAX_TYPED_HIR_RESOLVED &&
         typed_hir && constant.is_constant && constant.value == 0;
}

static int validate_static_scalar_initializer_assignment(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_qual_type_t object_qual_type, const node_t *initializer,
    const psx_frontend_expression_hir_t *expression_hir,
    token_t *fallback_diag_tok) {
  if (!semantic_context || !global_registry || !local_registry ||
      !initializer || !expression_hir ||
      !expression_hir->module ||
      expression_hir->root == PSX_HIR_NODE_ID_INVALID)
    return 0;
  const psx_hir_node_t *root = psx_hir_module_lookup(
      expression_hir->module, expression_hir->root);
  if (!root) return 0;
  psx_type_shape_t object_shape = {0};
  psx_type_shape_t value_shape = {0};
  const psx_semantic_type_table_t *semantic_types =
      ps_ctx_semantic_type_table_in(semantic_context);
  if (!psx_semantic_type_table_describe(
          semantic_types, object_qual_type.type_id, &object_shape) ||
      !psx_semantic_type_table_describe(
          semantic_types, psx_hir_node_qual_type(root).type_id,
          &value_shape))
    return 0;
  if (psx_hir_node_kind(root) == PSX_HIR_STRING &&
      object_shape.kind == PSX_TYPE_ARRAY)
    return 1;
  int is_null_pointer_constant =
      object_shape.kind == PSX_TYPE_POINTER &&
      (value_shape.kind == PSX_TYPE_BOOL ||
       value_shape.kind == PSX_TYPE_INTEGER) &&
      static_initializer_is_null_pointer_constant(
          semantic_context, global_registry, local_registry,
          initializer);
  object_qual_type.qualifiers &= PSX_TYPE_QUALIFIER_ATOMIC;
  psx_assignment_types_resolution_t assignment;
  psx_resolve_assignment_qual_types_in(
      semantic_context, object_qual_type,
      psx_hir_node_qual_type(root),
      is_null_pointer_constant,
      &assignment);
  if (assignment.status == PSX_ASSIGNMENT_TYPES_OK) return 1;

  ag_diagnostic_context_t *diagnostics =
      ps_ctx_diagnostics(semantic_context);
  token_t *token = initializer->tok
      ? initializer->tok : fallback_diag_tok;
  diag_error_id_t diagnostic =
      assignment.status == PSX_ASSIGNMENT_DISCARDS_QUALIFIERS
          ? DIAG_ERR_PARSER_CONST_QUAL_DISCARD
          : DIAG_ERR_PARSER_ASSIGN_TYPES_INCOMPATIBLE;
  diag_emit_tokf_in(
      diagnostics, diagnostic, token, "%s",
      diag_message_for_in(diagnostics, diagnostic));
  return 0;
}

static void diagnose_nonconstant_static_initializer(
    psx_semantic_context_t *semantic_context, token_t *token) {
  ag_diagnostic_context_t *diagnostics =
      ps_ctx_diagnostics(semantic_context);
  diag_emit_tokf_in(
      diagnostics, DIAG_ERR_PARSER_STATIC_INITIALIZER_NOT_CONSTANT,
      token, "%s",
      diag_message_for_in(
          diagnostics,
          DIAG_ERR_PARSER_STATIC_INITIALIZER_NOT_CONSTANT));
}

static int finish_global_declaration_pipeline(
    const psx_global_declaration_pipeline_request_t *request,
    psx_global_declaration_pipeline_result_t *result) {
  if (!request || !result || !request->semantic_context ||
      !request->global_registry || !request->local_registry ||
      !request->lowering_context || !request->options ||
      !result->global || !request->initializer) return 0;
  global_var_t *global = result->global;
  if (!request->is_extern_decl) {
    if (request->initializer->has_initializer) {
      psx_static_initializer_resolution_t initializer_resolution;
      psx_resolve_static_initializer(
          &(psx_static_initializer_resolution_request_t){
              .semantic_context = request->semantic_context,
              .global_registry = request->global_registry,
              .local_registry = request->local_registry,
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
      const node_t *initializer = selected_static_initializer_syntax(
          request->initializer, &initializer_resolution);
      if (!initializer) return 0;
      psx_frontend_expression_hir_t expression_hir = {
          .root = PSX_HIR_NODE_ID_INVALID,
      };
      psx_static_aggregate_initializer_plan_t aggregate_plan = {0};
      psx_static_initializer_lowering_input_t lowering_input = {
          .resolution = &initializer_resolution,
          .initializer_hir_root = PSX_HIR_NODE_ID_INVALID,
      };
      if (initializer_resolution.is_aggregate_initializer) {
        if (!psx_frontend_resolve_static_aggregate_initializer_plan_in_contexts(
                request->semantic_context,
                request->global_registry,
                request->local_registry,
                request->lowering_context,
                request->options,
                initializer_resolution.object_qual_type,
                initializer,
                request->initializer->value_tok,
                &aggregate_plan)) {
          if (aggregate_plan.failure ==
              PSX_STATIC_AGGREGATE_INITIALIZER_FAILURE_NON_CONSTANT) {
            diagnose_nonconstant_static_initializer(
                request->semantic_context,
                request->initializer->value_tok);
          }
          return 0;
        }
        lowering_input.aggregate_plan = &aggregate_plan;
      } else {
        if (!psx_frontend_resolve_expression_to_hir_in_contexts(
                request->semantic_context,
                request->global_registry,
                request->local_registry,
                request->lowering_context,
                request->options,
                initializer,
                initializer->tok
                    ? initializer->tok
                    : request->initializer->value_tok,
                &expression_hir)) {
          return 0;
        }
        if (!validate_static_scalar_initializer_assignment(
                request->semantic_context, request->global_registry,
                request->local_registry,
                initializer_resolution.object_qual_type,
                initializer, &expression_hir, request->diag_tok)) {
          psx_frontend_expression_hir_dispose(&expression_hir);
          return 0;
        }
        lowering_input.initializer_hir = expression_hir.module;
        lowering_input.initializer_hir_root = expression_hir.root;
      }
      int lowered = lower_resolved_global_declaration_initializer(
          request->global_registry, request->lowering_context, global,
          &lowering_input);
      psx_frontend_expression_hir_dispose(&expression_hir);
      if (!lowered) {
        diagnose_nonconstant_static_initializer(
            request->semantic_context,
            request->initializer->value_tok);
        return 0;
      }
      result->initialized = 1;
    }
  }
  return 1;
}

int psx_finish_global_declaration_pipeline(
    const psx_global_declaration_pipeline_request_t *request,
    psx_global_declaration_pipeline_result_t *result) {
  return finish_global_declaration_pipeline(request, result);
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
    case PSX_FUNCTION_DECLARATION_LINKAGE_CONFLICT:
      ps_diag_ctx_in(
          diagnostics, request->diag_tok, context,
          "declaration of function '%.*s' changes its language linkage",
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
  if (!request || !request->semantic_context || !request->name ||
      request->name_len <= 0 ||
      request->function_qual_type.type_id == PSX_TYPE_ID_INVALID) return 0;
  psx_function_declaration_resolution_t resolution;
  psx_resolve_function_declaration(
      &(psx_function_declaration_resolution_request_t){
          .semantic_context = request->semantic_context,
          .name = request->name,
          .name_len = request->name_len,
          .function_qual_type = request->function_qual_type,
          .is_definition = request->is_definition,
          .is_static = request->is_static,
      },
      &resolution);
  if (resolution.status != PSX_FUNCTION_DECLARATION_OK)
    diagnose_function_declaration(request, resolution.status);
  note_pipeline_declaration_source(
      request->semantic_context, PSX_SCOPE_ID_TRANSLATION_UNIT,
      PSX_NAMESPACE_ORDINARY, request->name, request->name_len,
      request->diag_tok);
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
    psx_qual_type_t base_qual_type,
    const psx_runtime_declarator_application_t *application,
    psx_parameter_declaration_resolution_t *resolution) {
  psx_parameter_declaration_resolution_request_t semantic_request = {
      .type = {
          .semantic_context = semantic_context,
          .base_qual_type = base_qual_type,
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
  int first_inner_array_op_index = -1;
  for (int op_index = 0; op_index < application->shape.count; op_index++) {
    if (application->shape.ops[op_index].kind != PSX_DECL_OP_ARRAY) continue;
    if (skip_outer_array && saw_array++ == 0) continue;
    if (first_inner_array_op_index < 0)
      first_inner_array_op_index = op_index;
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
  int explicit_pointer_count = 0;
  for (int op_index = 0;
       op_index < first_inner_array_op_index; op_index++) {
    if (application->shape.ops[op_index].kind == PSX_DECL_OP_POINTER)
      explicit_pointer_count++;
  }
  semantic_request.pointer_indirections =
      explicit_pointer_count - (skip_outer_array ? 0 : 1);
  if (semantic_request.pointer_indirections < 0)
    semantic_request.pointer_indirections = 0;
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
  psx_qual_type_t base_qual_type =
      psx_apply_parsed_decl_specifier_qual_type_in_contexts(
          semantic_context, global_registry, local_registry,
          &parameter->specifier);
  if (base_qual_type.type_id == PSX_TYPE_ID_INVALID) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(semantic_context),
        parameter->specifier.diagnostic_token, "param",
        "canonical parameter base type resolution failed");
  }
  psx_type_shape_t base_shape = {0};
  if (!psx_semantic_type_table_describe(
          ps_ctx_semantic_type_table_in(semantic_context),
          base_qual_type.type_id, &base_shape)) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(semantic_context),
        parameter->specifier.diagnostic_token, "param",
        "canonical parameter base type description failed");
  }
  psx_scope_lookup_point_t parameter_lookup_point =
      psx_scope_graph_capture_lookup_point(
          ps_ctx_scope_graph(semantic_context));
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
  if (!name && base_shape.kind == PSX_TYPE_VOID &&
      !has_pointer && !has_array && applied.shape.count == 0) {
    return -1;
  }

  psx_parameter_declaration_resolution_t resolution;
  if (!resolve_definition_parameter(
          semantic_context,
          base_qual_type,
          &applied, &resolution)) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(semantic_context),
        parameter->declarator.diagnostic_token, "param",
        "canonical parameter declaration resolution failed");
  }
  if (result->nargs + 1 >= *capacity) {
    *capacity = pda_next_cap_in(
        ps_ctx_diagnostics(semantic_context),
        *capacity, result->nargs + 2);
    result->parameter_vars = pda_xreallocarray_in(
        ps_ctx_diagnostics(semantic_context), result->parameter_vars,
        (size_t)*capacity, sizeof(*result->parameter_vars));
    result->parameter_qual_types = pda_xreallocarray_in(
        ps_ctx_diagnostics(semantic_context), result->parameter_qual_types,
        (size_t)*capacity, sizeof(*result->parameter_qual_types));
  }
  if (!name) {
    result->parameter_vars[result->nargs] = NULL;
    result->parameter_qual_types[result->nargs] =
        resolution.declaration_qual_type;
    result->nargs++;
    return 1;
  }

  for (int i = 0; i < resolution.inner_dimension_count; i++) {
    if (!resolution.inner_dimensions[i].is_constant &&
        !ps_ctx_semantic_expression_in(
            semantic_context,
            resolution.inner_dimensions[i].expression_id)) {
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
              .semantic_expressions =
                  ps_ctx_semantic_expression_table_in(semantic_context),
              .name = name->str,
              .name_len = name->len,
              .resolution = &resolution,
              .diag_tok = parameter->declarator.diagnostic_token,
          });
  if (!lowered) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(semantic_context),
        parameter->declarator.diagnostic_token, "param",
        "canonical parameter lowering failed for '%.*s'",
        name->len, name->str);
  }
  if (parameter->specifier.type_spec.is_register)
    ps_local_registry_mark_register(lowered);
  result->parameter_vars[result->nargs] = lowered;
  result->parameter_qual_types[result->nargs] =
      resolution.declaration_qual_type;
  result->nargs++;
  return 0;
}

int psx_begin_function_definition_pipeline(
    const psx_function_definition_pipeline_request_t *request,
    psx_function_definition_pipeline_result_t *result,
    psx_function_definition_pipeline_state_t *state) {
  if (result) *result = (psx_function_definition_pipeline_result_t){0};
  if (state) *state = (psx_function_definition_pipeline_state_t){0};
  if (!request || !result ||
      request->base_qual_type.type_id == PSX_TYPE_ID_INVALID ||
      !request->declarator ||
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
  state->base_qual_type = request->base_qual_type;
  state->result = result;
  state->primary_function_op_index =
      primary_suffix->declarator_op_index;
  state->parameter_count = primary_suffix->parameters
                               ? primary_suffix->parameters->count : 0;
  state->args_capacity = 16;
  result->parameter_vars = calloc(
      (size_t)state->args_capacity, sizeof(*result->parameter_vars));
  result->parameter_qual_types = calloc(
      (size_t)state->args_capacity,
      sizeof(*result->parameter_qual_types));
  return result->parameter_vars && result->parameter_qual_types;
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
    state->result->parameter_vars[0] = NULL;
    state->result->parameter_qual_types[0] =
        (psx_qual_type_t){
            PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
    return 1;
  }
  if (applied > 0) state->result->has_unnamed_parameter = 1;
  return 1;
}

int psx_finish_function_definition_pipeline(
    psx_function_definition_pipeline_state_t *state) {
  if (!state || !state->result ||
      state->base_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return 0;
  psx_function_definition_pipeline_result_t *result = state->result;
  psx_declarator_op_t *primary =
      &state->application.shape.ops[state->primary_function_op_index];
  psx_qual_type_t *function_parameter_qual_types =
      result->nargs > 0
          ? arena_alloc_in(
                ps_ctx_arena(state->semantic_context),
                (size_t)result->nargs *
                    sizeof(*function_parameter_qual_types))
          : NULL;
  if (result->nargs > 0 && !function_parameter_qual_types) return 0;
  for (int i = 0; i < result->nargs; i++) {
    function_parameter_qual_types[i] = result->parameter_qual_types[i];
    function_parameter_qual_types[i].qualifiers =
        PSX_TYPE_QUALIFIER_NONE;
  }
  psx_set_resolved_function_parameter_qual_types(
      ps_ctx_arena(state->semantic_context), primary,
      function_parameter_qual_types, result->nargs,
      primary->function_is_variadic,
      state->parameter_count > 0 || primary->function_is_variadic);

  result->function_qual_type = psx_resolve_decl_qual_type(
      &(psx_decl_type_request_t){
          .semantic_context = state->semantic_context,
          .base_qual_type = state->base_qual_type,
          .declarator_shape = &state->application.shape,
      });
  psx_type_shape_t function_shape = {0};
  return result->function_qual_type.type_id != PSX_TYPE_ID_INVALID &&
         psx_semantic_type_table_describe(
             ps_ctx_semantic_type_table_in(state->semantic_context),
             result->function_qual_type.type_id, &function_shape) &&
         function_shape.kind == PSX_TYPE_FUNCTION &&
         psx_semantic_type_table_base(
             ps_ctx_semantic_type_table_in(state->semantic_context),
             result->function_qual_type.type_id).type_id !=
             PSX_TYPE_ID_INVALID;
}

int psx_begin_static_local_declaration_pipeline(
    const psx_static_local_declaration_pipeline_request_t *request,
    psx_static_local_declaration_pipeline_result_t *result) {
  if (result) *result = (psx_static_local_declaration_pipeline_result_t){0};
  if (!request || !result || !request->semantic_context ||
      !request->global_registry || !request->local_registry ||
      !request->lowering_context || !request->options ||
      !request->name || request->name_len <= 0 ||
      request->type.type_id == PSX_TYPE_ID_INVALID ||
      !request->initializer) return 0;
  psx_qual_type_t declaration_identity = request->type;
  const psx_semantic_type_table_t *semantic_types =
      ps_ctx_semantic_type_table_in(request->semantic_context);
  psx_type_shape_t object_shape = {0};
  if (!psx_semantic_type_table_describe(
          semantic_types, declaration_identity.type_id, &object_shape))
    return 0;
  if (object_shape.kind == PSX_TYPE_FUNCTION) return 0;
  if (object_shape.kind == PSX_TYPE_VOID) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(request->semantic_context),
        request->diag_tok, "decl",
        diag_message_for_in(
            ps_ctx_diagnostics(request->semantic_context),
            DIAG_ERR_PARSER_VOID_OBJECT_FORBIDDEN),
        request->name_len, request->name);
  }
  int has_variably_modified_type =
      psx_semantic_type_table_contains_vla_array(
          semantic_types, declaration_identity.type_id);
  if (has_variably_modified_type &&
      object_shape.kind == PSX_TYPE_ARRAY) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(request->semantic_context),
        request->diag_tok, "decl",
        "static local variable length array '%.*s' is not permitted",
        request->name_len, request->name);
    return 0;
  }

  psx_qual_type_t leaf = psx_semantic_type_table_array_leaf(
      semantic_types, declaration_identity.type_id);
  psx_type_shape_t leaf_shape = {0};
  if (leaf.type_id == PSX_TYPE_ID_INVALID ||
      !psx_semantic_type_table_describe(
          semantic_types, leaf.type_id, &leaf_shape))
    return 0;
  int object_size = ps_lowering_type_id_size(
      request->lowering_context, declaration_identity.type_id);
  int leaf_size = ps_lowering_type_id_size(
      request->lowering_context, leaf.type_id);
  if (psx_type_kind_is_aggregate(leaf_shape.kind) && leaf_size <= 0) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(request->semantic_context),
        request->diag_tok, "decl", "%s",
        diag_message_for_in(
            ps_ctx_diagnostics(request->semantic_context),
            DIAG_ERR_PARSER_INCOMPLETE_OBJECT_FORBIDDEN));
  }
  if (object_shape.kind == PSX_TYPE_ARRAY) {
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

  const psx_record_decl_t *leaf_record =
      psx_type_kind_is_aggregate(leaf_shape.kind)
          ? ps_ctx_get_record_decl_in(
                request->semantic_context, leaf_shape.record_id)
          : NULL;
  if (leaf_record && leaf_record->is_anonymous) {
    ps_ctx_promote_tag_to_file_scope_in(
        request->semantic_context,
        leaf_shape.kind == PSX_TYPE_STRUCT ? TK_STRUCT : TK_UNION,
        leaf_record->tag_name, leaf_record->tag_len);
  }

  psx_static_local_kind_t kind = PSX_STATIC_LOCAL_SCALAR;
  if (object_shape.kind == PSX_TYPE_ARRAY) {
    kind = psx_type_kind_is_aggregate(leaf_shape.kind)
               ? PSX_STATIC_LOCAL_AGGREGATE_ARRAY
               : PSX_STATIC_LOCAL_CONSUMED_ARRAY;
  } else if (psx_type_kind_is_aggregate(object_shape.kind)) {
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
              .type = declaration_identity,
          },
          &storage)) {
    return 0;
  }
  result->global = storage.global;
  result->alias = storage.alias;
  if (has_variably_modified_type) {
    if (!request->application ||
        object_shape.kind != PSX_TYPE_POINTER)
      return 0;
    psx_local_declaration_resolution_t local_resolution;
    psx_resolve_local_declaration(
        &(psx_local_declaration_resolution_request_t){
            .arena_context =
                ps_lowering_arena(request->lowering_context),
            .semantic_types = semantic_types,
            .record_layouts =
                ps_ctx_record_layout_table_in(
                    request->semantic_context),
            .type_id = declaration_identity.type_id,
            .data_layout =
                ps_lowering_data_layout(request->lowering_context),
            .application = request->application,
            .has_initializer =
                request->initializer->has_initializer,
        },
        &local_resolution);
    if (local_resolution.status != PSX_LOCAL_DECLARATION_OK ||
        local_resolution.storage_kind !=
            PSX_LOCAL_STORAGE_POINTER_TO_VLA)
      return 0;
    psx_vla_runtime_dimension_t *dimensions = NULL;
    psx_qual_type_t constant_qual_type;
    if (!prepare_local_vla_dimensions(
            request->semantic_context, request->lowering_context,
            &local_resolution, &dimensions, &constant_qual_type))
      return 0;
    psx_vla_lowering_result_t vla =
        lower_static_pointer_to_vla_declaration_plan(
            &(psx_pointer_vla_lowering_request_t){
                .local_registry = request->local_registry,
                .lowering_context = request->lowering_context,
                .name = request->name,
                .name_len = request->name_len,
                .dimensions = dimensions,
                .constant_qual_type = constant_qual_type,
                .dimension_count =
                    local_resolution.dimension_count,
                .pointer_indirections =
                    local_resolution.pointer_indirections,
                .type = declaration_identity,
                .requested_alignment =
                    request->requested_alignment,
                .diag_tok = request->diag_tok,
            },
            result->alias);
    if (vla.var != result->alias || !vla.runtime_plan)
      return 0;
    result->vla_runtime_plan = vla.runtime_plan;
  }
  if (result->global)
    result->global->is_thread_local = request->is_thread_local ? 1 : 0;
  if (result->global &&
      request->requested_alignment > result->global->requested_alignment)
    result->global->requested_alignment = request->requested_alignment;

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
          .global_registry = request->global_registry,
          .local_registry = request->local_registry,
          .type = ps_lvar_decl_qual_type(result->alias),
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
  const node_t *initializer = selected_static_initializer_syntax(
      request->initializer, &resolution);
  if (!initializer) return 0;
  psx_frontend_expression_hir_t expression_hir = {
      .root = PSX_HIR_NODE_ID_INVALID,
  };
  psx_static_aggregate_initializer_plan_t aggregate_plan = {0};
  psx_static_initializer_lowering_input_t lowering_input = {
      .resolution = &resolution,
      .initializer_hir_root = PSX_HIR_NODE_ID_INVALID,
  };
  if (resolution.is_aggregate_initializer) {
    if (!psx_frontend_resolve_static_aggregate_initializer_plan_in_contexts(
            request->semantic_context, request->global_registry,
            request->local_registry, request->lowering_context,
            request->options, resolution.object_qual_type,
            initializer, request->initializer->value_tok,
            &aggregate_plan)) {
      if (aggregate_plan.failure ==
          PSX_STATIC_AGGREGATE_INITIALIZER_FAILURE_NON_CONSTANT)
        diagnose_nonconstant_static_initializer(
            request->semantic_context,
            request->initializer->value_tok);
      return 0;
    }
    lowering_input.aggregate_plan = &aggregate_plan;
  } else {
    if (!psx_frontend_resolve_expression_to_hir_in_contexts(
            request->semantic_context, request->global_registry,
            request->local_registry, request->lowering_context,
            request->options, initializer,
            initializer->tok
                ? initializer->tok
                : request->initializer->value_tok,
            &expression_hir)) {
      return 0;
    }
    if (!validate_static_scalar_initializer_assignment(
            request->semantic_context, request->global_registry,
            request->local_registry, resolution.object_qual_type,
            initializer, &expression_hir, request->diag_tok)) {
      psx_frontend_expression_hir_dispose(&expression_hir);
      return 0;
    }
    lowering_input.initializer_hir = expression_hir.module;
    lowering_input.initializer_hir_root = expression_hir.root;
  }
  int lowered = lower_static_local_declaration_initializer(
      request->global_registry, request->lowering_context,
      result->global, &lowering_input, &result->type_completed);
  psx_frontend_expression_hir_dispose(&expression_hir);
  if (!lowered) {
    diagnose_nonconstant_static_initializer(
        request->semantic_context,
        request->initializer->value_tok);
    return 0;
  }
  if (result->type_completed &&
      !ps_local_registry_complete_array_qual_type(
          request->local_registry, result->alias,
          resolution.object_qual_type))
    return 0;
  result->initialized = 1;
  return 1;
}

int psx_finish_static_local_declaration_typed_hir_pipeline(
    const psx_static_local_declaration_pipeline_request_t *request,
    psx_static_local_declaration_pipeline_result_t *result,
    const psx_typed_hir_tree_t *initializer_typed_hir) {
  if (!request || !result || !request->semantic_context ||
      !request->global_registry || !request->local_registry ||
      !request->lowering_context || !result->global || !result->alias ||
      !request->initializer || !request->initializer->has_initializer ||
      !initializer_typed_hir)
    return 0;

  psx_static_initializer_resolution_t resolution;
  psx_resolve_static_initializer(
      &(psx_static_initializer_resolution_request_t){
          .semantic_context = request->semantic_context,
          .global_registry = request->global_registry,
          .local_registry = request->local_registry,
          .type = ps_lvar_decl_qual_type(result->alias),
          .kind = request->initializer->kind,
          .initializer = request->initializer->value,
          .diag_tok = request->initializer->value_tok,
      },
      &resolution);
  if (resolution.status != PSX_STATIC_INITIALIZER_OK)
    return 0;
  psx_hir_module_t *initializer_hir = NULL;
  psx_static_aggregate_initializer_plan_t aggregate_plan = {0};
  psx_static_initializer_lowering_input_t lowering_input = {
      .resolution = &resolution,
      .initializer_hir_root = PSX_HIR_NODE_ID_INVALID,
  };
  if (resolution.is_aggregate_initializer) {
    if (!psx_materialize_static_aggregate_initializer_plan(
            initializer_typed_hir, request->global_registry,
            request->lowering_context, resolution.object_qual_type,
            request->initializer->value_tok, &aggregate_plan)) {
      if (aggregate_plan.failure ==
          PSX_STATIC_AGGREGATE_INITIALIZER_FAILURE_NON_CONSTANT)
        diagnose_nonconstant_static_initializer(
            request->semantic_context,
            request->initializer->value_tok);
      return 0;
    }
    lowering_input.aggregate_plan = &aggregate_plan;
  } else {
    initializer_hir = psx_hir_module_create();
    if (!initializer_hir) return 0;
    psx_resolved_hir_build_failure_t failure;
    psx_hir_node_id_t initializer_root = psx_typed_hir_tree_emit(
        initializer_hir, initializer_typed_hir, &failure);
    if (initializer_root == PSX_HIR_NODE_ID_INVALID) {
      psx_hir_module_destroy(initializer_hir);
      return 0;
    }
    lowering_input.initializer_hir = initializer_hir;
    lowering_input.initializer_hir_root = initializer_root;
    psx_frontend_expression_hir_t expression_hir = {
        .module = initializer_hir,
        .root = initializer_root,
    };
    const node_t *initializer = selected_static_initializer_syntax(
        request->initializer, &resolution);
    if (!initializer ||
        !validate_static_scalar_initializer_assignment(
            request->semantic_context, request->global_registry,
            request->local_registry, resolution.object_qual_type,
            initializer, &expression_hir, request->diag_tok)) {
      psx_hir_module_destroy(initializer_hir);
      return 0;
    }
  }
  int lowered = lower_static_local_declaration_initializer(
      request->global_registry, request->lowering_context,
      result->global, &lowering_input, &result->type_completed);
  psx_hir_module_destroy(initializer_hir);
  if (!lowered) {
    diagnose_nonconstant_static_initializer(
        request->semantic_context,
        request->initializer->value_tok);
    return 0;
  }
  if (result->type_completed &&
      !ps_local_registry_complete_array_qual_type(
          request->local_registry, result->alias,
          resolution.object_qual_type))
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

int psx_begin_automatic_local_declaration_hir_pipeline(
    const psx_automatic_local_declaration_pipeline_request_t *request,
    psx_automatic_local_declaration_pipeline_result_t *result) {
  if (result)
    *result = (psx_automatic_local_declaration_pipeline_result_t){0};
  if (!request || !result || !request->name || request->name_len <= 0 ||
      request->type.type_id == PSX_TYPE_ID_INVALID ||
      !request->application || !request->initializer ||
      !request->semantic_context ||
      !request->local_registry || !request->lowering_context)
    return 0;

  psx_qual_type_t declaration_identity = request->type;

  psx_local_declaration_resolution_t resolution;
  psx_resolve_local_declaration(
      &(psx_local_declaration_resolution_request_t){
          .arena_context = ps_lowering_arena(request->lowering_context),
          .semantic_types =
              ps_ctx_semantic_type_table_in(request->semantic_context),
          .record_layouts =
              ps_ctx_record_layout_table_in(request->semantic_context),
          .type_id = declaration_identity.type_id,
          .data_layout = ps_lowering_data_layout(request->lowering_context),
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
                  .type = declaration_identity,
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
                  .type = declaration_identity,
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
          .type = declaration_identity,
          .requested_alignment = request->requested_alignment,
          .diag_tok = request->diag_tok,
      };
      if (!prepare_local_vla_dimensions(
              request->semantic_context, request->lowering_context,
              &resolution, &lowering.dimensions,
              &lowering.constant_qual_type))
        return 0;
      vla = lower_vla_declaration_plan(&lowering);
      result->var = vla.var;
      result->vla_runtime_plan = vla.runtime_plan;
      break;
    }
    case PSX_LOCAL_STORAGE_POINTER_TO_VLA: {
      psx_pointer_vla_lowering_request_t lowering = {
              .local_registry = request->local_registry,
              .lowering_context = request->lowering_context,
              .name = request->name,
              .name_len = request->name_len,
              .dimension_count = resolution.dimension_count,
              .pointer_indirections =
                  resolution.pointer_indirections,
              .type = declaration_identity,
              .requested_alignment = request->requested_alignment,
              .diag_tok = request->diag_tok,
      };
      if (!prepare_local_vla_dimensions(
              request->semantic_context, request->lowering_context,
              &resolution, &lowering.dimensions,
              &lowering.constant_qual_type))
        return 0;
      vla = lower_pointer_to_vla_declaration_plan(&lowering);
      result->var = vla.var;
      result->vla_runtime_plan = vla.runtime_plan;
      break;
    }
  }
  if (!result->var) return 0;

  return 1;
}

int psx_apply_block_extern_declaration_pipeline(
    const psx_block_extern_declaration_pipeline_request_t *request) {
  if (!request || !request->semantic_context || !request->global_registry ||
      !request->local_registry || !request->lowering_context ||
      !request->options ||
      !request->name || request->name_len <= 0 ||
      request->type.type_id == PSX_TYPE_ID_INVALID) return 0;
  if (request->has_initializer) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(request->semantic_context),
        request->diag_tok, "decl",
        "block scope extern declaration '%.*s' cannot have an initializer",
        request->name_len, request->name);
  }

  psx_type_shape_t declaration_shape = {0};
  if (!psx_semantic_type_table_describe(
          ps_ctx_semantic_type_table_in(request->semantic_context),
          request->type.type_id, &declaration_shape))
    return 0;
  if (declaration_shape.kind == PSX_TYPE_FUNCTION) {
    if (!psx_apply_function_declaration_pipeline(
            &(psx_function_declaration_pipeline_request_t){
                .semantic_context = request->semantic_context,
                .name = request->name,
                .name_len = request->name_len,
                .function_qual_type = request->type,
                .diag_context = "block-extern",
                .diag_tok = request->diag_tok,
            })) {
      return 0;
    }
    return 1;
  }
  if (psx_semantic_type_table_contains_vla_array(
          ps_ctx_semantic_type_table_in(request->semantic_context),
          request->type.type_id)) {
    ps_diag_ctx_in(
        ps_ctx_diagnostics(request->semantic_context),
        request->diag_tok, "decl",
        "block scope extern object '%.*s' cannot have "
        "variably modified type",
        request->name_len, request->name);
    return 0;
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
              .is_thread_local = request->is_thread_local,
              .requested_alignment = request->requested_alignment,
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
      request->type.type_id == PSX_TYPE_ID_INVALID ||
      !request->local_registry ||
      !request->lowering_context) {
    return NULL;
  }
  return lower_complete_internal_local_object(
      &(psx_local_object_request_t){
          .local_registry = request->local_registry,
          .lowering_context = request->lowering_context,
          .name = request->name,
          .name_len = request->name_len,
          .type = request->type,
          .requested_alignment = request->requested_alignment,
      });
}
