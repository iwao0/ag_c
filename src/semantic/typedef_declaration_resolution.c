#include "typedef_declaration_resolution.h"

#include "scope_graph.h"

#include "../parser/semantic_ctx.h"

#include <string.h>

void psx_resolve_typedef_declaration(
    const psx_typedef_declaration_resolution_request_t *request,
    psx_typedef_declaration_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_TYPEDEF_DECLARATION_INVALID;
  if (!request || !request->semantic_context || !request->name ||
      request->name_len <= 0 ||
      request->decl_qual_type.type_id == PSX_TYPE_ID_INVALID) {
    return;
  }
  psx_semantic_context_t *semantic_context = request->semantic_context;
  psx_scope_graph_t *scope_graph = ps_ctx_scope_graph(semantic_context);
  if (!scope_graph) return;

  int scope_depth = ps_ctx_current_tag_scope_depth_in(semantic_context);
  const psx_scope_declaration_t *existing =
      psx_scope_graph_lookup_declaration_in_scope(
          scope_graph, psx_scope_graph_current_scope(scope_graph),
          PSX_NAMESPACE_ORDINARY, request->name, request->name_len);
  if (existing) {
    switch (existing->kind) {
    case PSX_DECL_ENUM_CONSTANT:
      resolution->status = PSX_TYPEDEF_DECLARATION_ENUM_NAME_CONFLICT;
      return;
    case PSX_DECL_LOCAL_OBJECT:
    case PSX_DECL_GLOBAL_OBJECT:
      resolution->status = PSX_TYPEDEF_DECLARATION_OBJECT_NAME_CONFLICT;
      return;
    case PSX_DECL_FUNCTION:
      resolution->status = PSX_TYPEDEF_DECLARATION_FUNCTION_NAME_CONFLICT;
      return;
    case PSX_DECL_TYPEDEF:
      break;
    default:
      resolution->status = PSX_TYPEDEF_DECLARATION_INVALID;
      return;
    }
  }

  if (!psx_semantic_type_table_lookup_qual_type(
          ps_ctx_semantic_type_table_in(semantic_context),
          request->decl_qual_type)) {
    resolution->status = PSX_TYPEDEF_DECLARATION_TYPE_CONFLICT;
    return;
  }
  const psx_typedef_info_t info = {
      .decl_type_table = ps_ctx_semantic_type_table_in(semantic_context),
      .decl_qual_type = request->decl_qual_type,
      .runtime_application = request->runtime_application,
  };
  if (!ps_ctx_register_typedef_name_in(
          semantic_context,
          request->name, request->name_len, &info,
          &resolution->created, &resolution->redeclared)) {
    resolution->status = PSX_TYPEDEF_DECLARATION_TYPE_CONFLICT;
    return;
  }
  resolution->scope_depth = scope_depth;
  resolution->status = PSX_TYPEDEF_DECLARATION_OK;
}
