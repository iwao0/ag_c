#include "compound_literal_lowering.h"
#include "runtime_context.h"

#include "../declaration_pipeline.h"
#include "../diag/diag.h"
#include "../diag/error_catalog.h"
#include "../parser/diag.h"
#include "../parser/decl.h"
#include "../parser/node_utils.h"
#include "../parser/semantic_ctx.h"
#include "../semantic/compound_literal_semantics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *new_compound_object_name(
    psx_lowering_context_t *lowering_context, int file_scope) {
  int sequence = file_scope
                     ? lowering_context->file_scope_compound_sequence++
                     : lowering_context->local_compound_sequence++;
  const char *format = file_scope ? "__compound_lit_%d"
                                  : "__compound_object_%d";
  int len = snprintf(NULL, 0, format, sequence);
  char *name = calloc((size_t)len + 1, 1);
  if (!name) return NULL;
  snprintf(name, (size_t)len + 1, format, sequence);
  return name;
}

static int qual_types_equal(psx_qual_type_t left, psx_qual_type_t right) {
  return left.type_id == right.type_id &&
         left.qualifiers == right.qualifiers;
}

static int plan_file_scope_compound_literal(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    node_compound_literal_t *compound,
    const token_t *fallback_diag_tok,
    psx_compound_literal_storage_plan_t *plan) {
  psx_resolution_store_t *store =
      ps_lowering_resolution_store(lowering_context);
  psx_qual_type_t object_qual_type =
      ps_node_qual_type(store, &compound->base);
  const psx_type_t *type = psx_semantic_type_table_lookup_qual_type(
      ps_lowering_semantic_types(lowering_context), object_qual_type);
  psx_type_shape_t object_shape = {0};
  int has_object_shape = psx_semantic_type_table_describe(
      ps_lowering_semantic_types(lowering_context),
      object_qual_type.type_id, &object_shape);
  node_t *initializer = compound->base.rhs;
  int is_array = has_object_shape && object_shape.kind == PSX_TYPE_ARRAY;
  node_init_list_t *list = initializer && initializer->kind == ND_INIT_LIST
                               ? (node_init_list_t *)initializer
                               : NULL;
  if (has_object_shape &&
      object_qual_type.type_id != PSX_TYPE_ID_INVALID && !is_array &&
      !psx_compound_literal_requires_addressable_storage(
          ps_lowering_resolution_store(lowering_context),
          &compound->base) &&
      !psx_type_kind_is_aggregate(object_shape.kind) && list &&
      list->entry_count == 1 &&
      list->entries[0].designator_count == 0 &&
      list->entries[0].value &&
      list->entries[0].value->kind == ND_NUM &&
      qual_types_equal(
          object_qual_type,
          ps_node_qual_type(store, list->entries[0].value))) {
    plan->direct_initializer_index = 0;
    plan->object_qual_type = object_qual_type;
    plan->kind = PSX_COMPOUND_LITERAL_DIRECT_INITIALIZER;
    return 1;
  }

  psx_parsed_initializer_t parsed = {
      .has_initializer = 1,
      .kind = PSX_DECL_INIT_LIST,
      .value = initializer,
      .value_tok = compound->base.tok,
  };
  psx_global_declaration_pipeline_result_t object;
  char *storage_name = new_compound_object_name(lowering_context, 1);
  token_t *diag_tok = compound->base.tok
                          ? compound->base.tok
                          : (token_t *)fallback_diag_tok;
  if (!storage_name ||
      !psx_apply_resolved_global_declaration_pipeline(
          &(psx_global_declaration_pipeline_request_t){
              .semantic_context = semantic_context,
              .global_registry = global_registry,
              .local_registry = local_registry,
              .lowering_context = lowering_context,
              .options = options,
              .name = storage_name,
              .name_len = storage_name ? (int)strlen(storage_name) : 0,
              .type = type,
              .is_static = 1,
              .is_compiler_generated = 1,
              .initializer = &parsed,
              .diag_tok = diag_tok,
          },
          &object)) {
    ps_diag_ctx_in(
        ps_lowering_diagnostics(lowering_context), diag_tok,
        "compound-literal", "%s",
        diag_message_for_in(
            ps_lowering_diagnostics(lowering_context),
                    DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
  }
  if (!object.global) return 0;
  plan->global_object = object.global;
  plan->object_qual_type = ps_gvar_decl_qual_type(object.global);
  plan->kind = PSX_COMPOUND_LITERAL_GLOBAL_OBJECT;
  return plan->object_qual_type.type_id != PSX_TYPE_ID_INVALID;
}

static int plan_local_compound_literal(
    psx_semantic_context_t *semantic_context,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    node_compound_literal_t *compound,
    const token_t *fallback_diag_tok,
    psx_compound_literal_storage_plan_t *plan) {
  psx_qual_type_t object_qual_type = ps_node_qual_type(
      ps_lowering_resolution_store(lowering_context), &compound->base);
  const psx_type_t *type = psx_semantic_type_table_lookup_qual_type(
      ps_lowering_semantic_types(lowering_context), object_qual_type);
  psx_parsed_initializer_t parsed = {
      .has_initializer = 1,
      .kind = PSX_DECL_INIT_LIST,
      .value = compound->base.rhs,
      .value_tok = compound->base.tok,
  };
  psx_runtime_declarator_application_t application = {0};
  psx_automatic_local_declaration_pipeline_result_t object;
  char *storage_name = new_compound_object_name(lowering_context, 0);
  token_t *diag_tok = compound->base.tok
                          ? compound->base.tok
                          : (token_t *)fallback_diag_tok;
  if (!storage_name ||
      !psx_apply_automatic_local_declaration_pipeline(
          &(psx_automatic_local_declaration_pipeline_request_t){
              .semantic_context = semantic_context,
              .local_registry = local_registry,
              .lowering_context = lowering_context,
              .name = storage_name,
              .name_len = storage_name ? (int)strlen(storage_name) : 0,
              .type = type,
              .application = &application,
              .initializer = &parsed,
              .diag_tok = diag_tok,
          },
          &object)) {
    ps_diag_ctx_in(
        ps_lowering_diagnostics(lowering_context), diag_tok,
        "compound-literal",
        "compound literal local storage lowering failed");
  }
  if (!object.var) return 0;
  plan->local_object = object.var;
  plan->initialization_tree = object.initialization;
  plan->object_qual_type = ps_lvar_decl_qual_type(object.var);
  plan->kind = PSX_COMPOUND_LITERAL_LOCAL_OBJECT;
  return plan->object_qual_type.type_id != PSX_TYPE_ID_INVALID;
}

int psx_plan_compound_literal_storage_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    node_compound_literal_t *compound,
    const token_t *fallback_diag_tok,
    psx_compound_literal_storage_plan_t *plan) {
  if (plan) {
    *plan = (psx_compound_literal_storage_plan_t){
        .direct_initializer_index = -1,
    };
  }
  if (!semantic_context || !global_registry || !local_registry ||
      !lowering_context || !options || !compound || !plan)
    return 0;
  char *function_name = NULL;
  int function_name_len = 0;
  ps_decl_get_current_funcname_in(
      local_registry, &function_name, &function_name_len);
  (void)function_name_len;
  return psx_compound_literal_storage_duration_in_scope_graph(
             ps_ctx_scope_graph(semantic_context),
             compound->type_name.scope_seq,
             function_name != NULL) ==
             PSX_COMPOUND_LITERAL_STORAGE_STATIC
             ? plan_file_scope_compound_literal(
                   semantic_context, global_registry, local_registry,
                   lowering_context, options, compound,
                   fallback_diag_tok, plan)
             : plan_local_compound_literal(
                   semantic_context, local_registry, lowering_context,
                   compound, fallback_diag_tok, plan);
}
