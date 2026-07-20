#include "function_declaration_resolution.h"

#include "scope_graph.h"

#include "../parser/global_registry.h"
#include "../parser/semantic_ctx.h"

#include <string.h>

void psx_resolve_function_declaration(
    const psx_function_declaration_resolution_request_t *request,
    psx_function_declaration_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_FUNCTION_DECLARATION_INVALID;
  if (!request || !request->semantic_context || !request->global_registry ||
      !request->name || request->name_len <= 0 ||
      request->function_qual_type.type_id == PSX_TYPE_ID_INVALID) {
    return;
  }
  psx_global_registry_t *global_registry = request->global_registry;
  psx_semantic_context_t *semantic_context = request->semantic_context;
  psx_scope_graph_t *scope_graph = ps_ctx_scope_graph(semantic_context);
  if (!scope_graph ||
      scope_graph != ps_global_registry_scope_graph(global_registry))
    return;
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_type_shape_t function_shape = {0};
  psx_qual_type_t return_type = psx_semantic_type_table_base(
      types, request->function_qual_type.type_id);
  if (!psx_semantic_type_table_describe(
          types, request->function_qual_type.type_id, &function_shape) ||
      function_shape.kind != PSX_TYPE_FUNCTION ||
      return_type.type_id == PSX_TYPE_ID_INVALID) {
    return;
  }
  const psx_scope_declaration_t *existing =
      psx_scope_graph_lookup_declaration_in_scope(
          scope_graph, PSX_SCOPE_ID_TRANSLATION_UNIT,
          PSX_NAMESPACE_ORDINARY, request->name, request->name_len);
  if (existing && (existing->kind == PSX_DECL_GLOBAL_OBJECT ||
                   existing->kind == PSX_DECL_LOCAL_OBJECT)) {
    resolution->status = PSX_FUNCTION_DECLARATION_OBJECT_NAME_CONFLICT;
    return;
  }
  if (existing && existing->kind != PSX_DECL_FUNCTION) {
    resolution->status = PSX_FUNCTION_DECLARATION_TYPE_CONFLICT;
    return;
  }
  resolution->function = ps_ctx_register_function_qual_type_in(
      semantic_context, request->name, request->name_len,
      request->function_qual_type);
  if (!resolution->function) {
    resolution->status = PSX_FUNCTION_DECLARATION_TYPE_CONFLICT;
    return;
  }
  if (request->is_definition &&
      !ps_ctx_track_function_defined_in(
          semantic_context, request->name, request->name_len)) {
    resolution->function = NULL;
    resolution->status = PSX_FUNCTION_DECLARATION_DUPLICATE_DEFINITION;
    return;
  }
  resolution->status = PSX_FUNCTION_DECLARATION_OK;
}
