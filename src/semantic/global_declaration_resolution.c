#include "global_declaration_resolution.h"

#include "scope_graph.h"
#include "type_completeness.h"

#include "../diag/diag.h"
#include "../parser/global_registry.h"
#include "../parser/node_utils.h"
#include "../parser/semantic_ctx.h"

#include <string.h>

static int qual_types_equal(
    psx_qual_type_t existing, psx_qual_type_t incoming) {
  return existing.type_id == incoming.type_id &&
         existing.qualifiers == incoming.qualifiers;
}

static int global_types_compatible(
    const psx_semantic_type_table_t *types,
    psx_qual_type_t existing, psx_qual_type_t incoming) {
  return psx_semantic_type_table_types_compatible(
      types, existing, incoming);
}

static int record_type_is_complete(
    psx_semantic_context_t *semantic_context,
    const psx_type_shape_t *type) {
  if (!semantic_context || !type ||
      !psx_type_kind_is_aggregate(type->kind))
    return 0;
  const psx_record_decl_t *record = ps_ctx_get_record_decl_in(
      semantic_context, type->record_id);
  return record && record->is_complete;
}

static int type_contains_incomplete_record(
    psx_semantic_context_t *semantic_context, psx_type_id_t type_id) {
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_type_shape_t type = {0};
  if (!psx_semantic_type_table_describe(types, type_id, &type) ||
      type.kind == PSX_TYPE_POINTER || type.kind == PSX_TYPE_FUNCTION)
    return 0;
  if (psx_type_kind_is_aggregate(type.kind))
    return !record_type_is_complete(semantic_context, &type);
  if (type.kind == PSX_TYPE_ARRAY) {
    psx_qual_type_t base = psx_semantic_type_table_base(types, type_id);
    return base.type_id == PSX_TYPE_ID_INVALID ||
           type_contains_incomplete_record(
               semantic_context, base.type_id);
  }
  return 0;
}

static int type_is_complete_object(
    psx_semantic_context_t *semantic_context, psx_type_id_t type_id) {
  return psx_semantic_type_is_complete_object_in(
      semantic_context, type_id);
}

void psx_resolve_global_declaration(
    const psx_global_declaration_resolution_request_t *request,
    psx_global_declaration_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_GLOBAL_DECLARATION_INVALID;
  if (!request || !request->semantic_context || !request->name ||
      request->name_len <= 0 ||
      request->type.type_id == PSX_TYPE_ID_INVALID ||
      request->requested_alignment < 0 ||
      (!request->has_alignment_specifier &&
       request->requested_alignment != 0)) {
    return;
  }
  psx_semantic_context_t *semantic_context = request->semantic_context;
  psx_scope_graph_t *scope_graph = ps_ctx_scope_graph(semantic_context);
  if (!scope_graph) return;

  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_type_shape_t incoming_shape = {0};
  if (!psx_semantic_type_table_describe(
          types, request->type.type_id, &incoming_shape))
    return;

  int contains_incomplete_record = type_contains_incomplete_record(
      semantic_context, request->type.type_id);
  /* A tentative definition with external linkage may name an incomplete
   * record that is completed later in the translation unit. Internal
   * linkage and initialized definitions still require a complete type at
   * the declaration (C11 6.9.2p3). */
  int defer_incomplete_record =
      contains_incomplete_record && !request->is_static &&
      !request->has_initializer;
  /* An external-linkage incomplete array without an initializer is also a
   * tentative definition. A static one is forbidden by C11 6.9.2p3. */
  if ((contains_incomplete_record && !defer_incomplete_record) ||
      (incoming_shape.kind == PSX_TYPE_ARRAY &&
       incoming_shape.array_len <= 0 && !request->is_extern_decl &&
       !request->has_initializer && request->is_static)) {
    resolution->status = PSX_GLOBAL_DECLARATION_INCOMPLETE_OBJECT;
    return;
  }
  int incoming_is_incomplete_array =
      incoming_shape.kind == PSX_TYPE_ARRAY &&
      incoming_shape.array_len <= 0;
  psx_qual_type_t object_type = incoming_is_incomplete_array
                                    ? psx_semantic_type_table_base(
                                          types, request->type.type_id)
                                    : request->type;
  if (object_type.type_id == PSX_TYPE_ID_INVALID ||
      (!defer_incomplete_record &&
       !type_is_complete_object(semantic_context, object_type.type_id))) {
    return;
  }
  resolution->declaration_qual_type = request->type;
  const psx_scope_declaration_t *existing =
      psx_scope_graph_lookup_declaration_in_scope(
          scope_graph, PSX_SCOPE_ID_TRANSLATION_UNIT,
          PSX_NAMESPACE_ORDINARY, request->name, request->name_len);
  if (existing) {
    switch (existing->kind) {
    case PSX_DECL_FUNCTION:
      resolution->status = PSX_GLOBAL_DECLARATION_FUNCTION_NAME_CONFLICT;
      return;
    case PSX_DECL_TYPEDEF:
      resolution->status = PSX_GLOBAL_DECLARATION_TYPEDEF_NAME_CONFLICT;
      return;
    case PSX_DECL_ENUM_CONSTANT:
      resolution->status = PSX_GLOBAL_DECLARATION_ENUM_NAME_CONFLICT;
      return;
    case PSX_DECL_GLOBAL_OBJECT:
      resolution->existing = existing->payload;
      if (!resolution->existing) return;
      break;
    default:
      return;
    }
  }
  if (resolution->existing) {
    if ((request->is_static && !resolution->existing->is_static) ||
        (!request->is_static && !request->is_extern_decl &&
         resolution->existing->is_static)) {
      resolution->status = PSX_GLOBAL_DECLARATION_LINKAGE_CONFLICT;
      return;
    }
    psx_qual_type_t existing_type =
        ps_gvar_decl_qual_type(resolution->existing);
    if (!global_types_compatible(types, existing_type, request->type)) {
      resolution->status = PSX_GLOBAL_DECLARATION_TYPE_CONFLICT;
      return;
    }
    int existing_has_alignment =
        resolution->existing->has_alignment_specifier != 0;
    int incoming_has_alignment =
        request->has_alignment_specifier != 0;
    if (existing_has_alignment && incoming_has_alignment &&
        resolution->existing->requested_alignment !=
            request->requested_alignment) {
      resolution->status =
          PSX_GLOBAL_DECLARATION_ALIGNMENT_CONFLICT;
      return;
    }
    int incoming_is_definition =
        !request->is_extern_decl || request->has_initializer;
    if ((incoming_is_definition && existing_has_alignment &&
         !incoming_has_alignment) ||
        (resolution->existing->has_init &&
         !existing_has_alignment && incoming_has_alignment)) {
      resolution->status =
          PSX_GLOBAL_DECLARATION_DEFINITION_ALIGNMENT_MISSING;
      return;
    }
    psx_qual_type_t composite = ps_ctx_composite_qual_type_in(
        semantic_context, existing_type, request->type);
    if (composite.type_id == PSX_TYPE_ID_INVALID) {
      resolution->status = PSX_GLOBAL_DECLARATION_TYPE_CONFLICT;
      return;
    }
    resolution->declaration_qual_type = composite;
    psx_type_shape_t existing_shape = {0};
    psx_type_shape_t composite_shape = {0};
    if (!psx_semantic_type_table_describe(
            types, existing_type.type_id, &existing_shape) ||
        !psx_semantic_type_table_describe(
            types, composite.type_id, &composite_shape))
      return;
    int existing_is_incomplete = existing_shape.kind == PSX_TYPE_ARRAY &&
                                 existing_shape.array_len <= 0;
    resolution->complete_existing_array =
        existing_is_incomplete &&
        composite_shape.kind == PSX_TYPE_ARRAY &&
        composite_shape.array_len > 0 && !composite_shape.is_vla;
    resolution->adopt_composite_type =
        !resolution->complete_existing_array &&
        !qual_types_equal(existing_type, composite);
    resolution->clear_existing_extern =
        resolution->existing->is_extern_decl && !request->is_extern_decl;
  }
  resolution->status = PSX_GLOBAL_DECLARATION_OK;
}

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_global_registry_t *global_registry;
  int succeeded;
} tentative_global_finalization_t;

static void report_unresolved_incomplete_global(
    tentative_global_finalization_t *finalization,
    global_var_t *global) {
  psx_scope_graph_t *scope_graph = ps_ctx_scope_graph(
      finalization->semantic_context);
  const psx_scope_declaration_t *declaration =
      psx_scope_graph_lookup_declaration_in_scope(
          scope_graph, PSX_SCOPE_ID_TRANSLATION_UNIT,
          PSX_NAMESPACE_ORDINARY, ps_gvar_name(global),
          ps_gvar_name_len(global));
  const char *input = declaration ? declaration->source_input : NULL;
  const char *loc = input && declaration->source_byte_offset >= 0
                        ? input + declaration->source_byte_offset
                        : input;
  ag_diagnostic_context_t *diagnostics = ps_ctx_diagnostics(
      finalization->semantic_context);
  (void)diag_report_atf_in(
      diagnostics, DIAG_ERR_PARSER_INCOMPLETE_OBJECT_FORBIDDEN,
      input, loc, "%s: '%.*s'",
      diag_message_for_in(
          diagnostics, DIAG_ERR_PARSER_INCOMPLETE_OBJECT_FORBIDDEN),
      ps_gvar_name_len(global), ps_gvar_name(global));
}

static void finalize_tentative_global(global_var_t *global, void *user) {
  tentative_global_finalization_t *finalization = user;
  if (!global || !finalization || !finalization->succeeded ||
      ps_gvar_is_extern_decl(global))
    return;
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(finalization->semantic_context);
  psx_qual_type_t current = ps_gvar_decl_qual_type(global);
  psx_type_shape_t shape = {0};
  if (!psx_semantic_type_table_describe(
          types, current.type_id, &shape)) {
    finalization->succeeded = 0;
    return;
  }
  if (shape.kind == PSX_TYPE_ARRAY && shape.array_len <= 0 &&
      !shape.is_vla) {
    /* C11 6.9.2p5 completes an otherwise-incomplete tentative array as if
     * it had one element at the end of the translation unit. */
    psx_qual_type_t element =
        psx_semantic_type_table_base(types, current.type_id);
    psx_qual_type_t completed = ps_ctx_intern_array_of_qual_type_in(
        finalization->semantic_context, element, 1, 0);
    completed.qualifiers = current.qualifiers;
    if (completed.type_id == PSX_TYPE_ID_INVALID ||
        !ps_global_registry_complete_array_qual_type(
            finalization->global_registry, global, completed)) {
      finalization->succeeded = 0;
      return;
    }
    current = completed;
  }
  if (type_contains_incomplete_record(
          finalization->semantic_context, current.type_id)) {
    report_unresolved_incomplete_global(finalization, global);
    finalization->succeeded = 0;
  }
}

int psx_finalize_tentative_globals_in(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry) {
  if (!semantic_context || !global_registry) return 0;
  tentative_global_finalization_t finalization = {
      .semantic_context = semantic_context,
      .global_registry = global_registry,
      .succeeded = 1,
  };
  ps_iter_globals_in(
      global_registry, finalize_tentative_global, &finalization);
  return finalization.succeeded;
}
