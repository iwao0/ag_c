#include "global_declaration_resolution.h"

#include "scope_graph.h"

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
  if (qual_types_equal(existing, incoming)) return 1;
  psx_type_shape_t existing_shape = {0};
  psx_type_shape_t incoming_shape = {0};
  if (!psx_semantic_type_table_describe(
          types, existing.type_id, &existing_shape) ||
      !psx_semantic_type_table_describe(
          types, incoming.type_id, &incoming_shape) ||
      existing.qualifiers != incoming.qualifiers ||
      existing_shape.kind != PSX_TYPE_ARRAY ||
      incoming_shape.kind != PSX_TYPE_ARRAY ||
      (existing_shape.array_len > 0 && incoming_shape.array_len > 0)) {
    return 0;
  }
  return qual_types_equal(
      psx_semantic_type_table_base(types, existing.type_id),
      psx_semantic_type_table_base(types, incoming.type_id));
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
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_type_shape_t type = {0};
  if (!psx_semantic_type_table_describe(types, type_id, &type) ||
      type.kind == PSX_TYPE_INVALID || type.kind == PSX_TYPE_VOID ||
      type.kind == PSX_TYPE_FUNCTION)
    return 0;
  if (type.kind == PSX_TYPE_POINTER) return 1;
  if (psx_type_kind_is_aggregate(type.kind))
    return record_type_is_complete(semantic_context, &type);
  if (type.kind == PSX_TYPE_ARRAY) {
    psx_qual_type_t base = psx_semantic_type_table_base(types, type_id);
    return type.array_len > 0 && base.type_id != PSX_TYPE_ID_INVALID &&
           type_is_complete_object(semantic_context, base.type_id);
  }
  return 1;
}

void psx_resolve_global_declaration(
    const psx_global_declaration_resolution_request_t *request,
    psx_global_declaration_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_GLOBAL_DECLARATION_INVALID;
  if (!request || !request->semantic_context || !request->name ||
      request->name_len <= 0 ||
      request->type.type_id == PSX_TYPE_ID_INVALID) {
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

  if (type_contains_incomplete_record(
          semantic_context, request->type.type_id) ||
      (incoming_shape.kind == PSX_TYPE_ARRAY &&
       incoming_shape.array_len <= 0 && !request->is_extern_decl &&
       !request->has_initializer)) {
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
      !type_is_complete_object(semantic_context, object_type.type_id)) {
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
    psx_qual_type_t existing_type =
        ps_gvar_decl_qual_type(resolution->existing);
    if (!global_types_compatible(types, existing_type, request->type)) {
      resolution->status = PSX_GLOBAL_DECLARATION_TYPE_CONFLICT;
      return;
    }
    psx_type_shape_t existing_shape = {0};
    if (!psx_semantic_type_table_describe(
            types, existing_type.type_id, &existing_shape))
      return;
    int existing_is_incomplete = existing_shape.kind == PSX_TYPE_ARRAY &&
                                 existing_shape.array_len <= 0;
    resolution->complete_existing_array =
        existing_is_incomplete && !incoming_is_incomplete_array;
    resolution->clear_existing_extern =
        resolution->existing->is_extern_decl && !request->is_extern_decl;
  }
  resolution->status = PSX_GLOBAL_DECLARATION_OK;
}
