#include "function_declaration_resolution.h"

#include "scope_graph.h"

#include "../parser/semantic_ctx.h"

#include <string.h>

static const psx_scope_declaration_t *
translation_unit_nonfunction_declaration(
    const psx_scope_graph_t *scope_graph,
    const char *name, int name_len) {
  size_t declaration_count =
      psx_scope_graph_declaration_count(scope_graph);
  for (size_t index = declaration_count; index > 0; index--) {
    const psx_scope_declaration_t *declaration =
        psx_scope_graph_declaration_at(scope_graph, index - 1);
    if (!declaration ||
        declaration->scope_id != PSX_SCOPE_ID_TRANSLATION_UNIT ||
        declaration->name_space != PSX_NAMESPACE_ORDINARY ||
        declaration->kind == PSX_DECL_FUNCTION ||
        !declaration->name || declaration->name_len != name_len ||
        memcmp(declaration->name, name, (size_t)name_len) != 0)
      continue;
    return declaration;
  }
  return NULL;
}

void psx_resolve_function_declaration(
    const psx_function_declaration_resolution_request_t *request,
    psx_function_declaration_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_FUNCTION_DECLARATION_INVALID;
  if (!request || !request->semantic_context || !request->name ||
      request->name_len <= 0 ||
      request->function_qual_type.type_id == PSX_TYPE_ID_INVALID) {
    return;
  }
  psx_semantic_context_t *semantic_context = request->semantic_context;
  psx_scope_graph_t *scope_graph = ps_ctx_scope_graph(semantic_context);
  if (!scope_graph) return;
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
  const psx_scope_declaration_t *file_scope_conflict =
      translation_unit_nonfunction_declaration(
          scope_graph, request->name, request->name_len);
  if (!request->is_block_scope && file_scope_conflict) {
    resolution->status =
        file_scope_conflict->kind == PSX_DECL_GLOBAL_OBJECT
            ? PSX_FUNCTION_DECLARATION_OBJECT_NAME_CONFLICT
            : PSX_FUNCTION_DECLARATION_TYPE_CONFLICT;
    return;
  }
  if (existing && (existing->kind == PSX_DECL_GLOBAL_OBJECT ||
                   existing->kind == PSX_DECL_LOCAL_OBJECT)) {
    resolution->status = PSX_FUNCTION_DECLARATION_OBJECT_NAME_CONFLICT;
    return;
  }
  int block_scope_nonlinkage_shadow =
      request->is_block_scope && existing &&
      (existing->kind == PSX_DECL_TYPEDEF ||
       existing->kind == PSX_DECL_ENUM_CONSTANT);
  if (existing && existing->kind != PSX_DECL_FUNCTION &&
      !block_scope_nonlinkage_shadow) {
    resolution->status = PSX_FUNCTION_DECLARATION_TYPE_CONFLICT;
    return;
  }
  const psx_function_symbol_t *existing_function =
      existing && existing->kind == PSX_DECL_FUNCTION
          ? existing->payload : NULL;
  if (existing_function && request->is_static &&
      !ps_function_symbol_has_internal_linkage(existing_function)) {
    resolution->status = PSX_FUNCTION_DECLARATION_LINKAGE_CONFLICT;
    return;
  }
  resolution->function =
      request->is_block_scope
          ? ps_ctx_register_block_function_qual_type_in(
                semantic_context, request->name, request->name_len,
                request->function_qual_type)
          : ps_ctx_register_function_qual_type_in(
                semantic_context, request->name, request->name_len,
                request->function_qual_type);
  if (!resolution->function) {
    resolution->status = PSX_FUNCTION_DECLARATION_TYPE_CONFLICT;
    return;
  }
  if (request->is_static &&
      !ps_ctx_mark_function_internal_linkage_in(
          semantic_context, request->name, request->name_len)) {
    resolution->function = NULL;
    return;
  }
  if (request->is_extern &&
      !ps_ctx_mark_function_explicit_extern_in(
          semantic_context, request->name, request->name_len)) {
    resolution->function = NULL;
    return;
  }
  if (request->is_noreturn &&
      !ps_ctx_mark_function_noreturn_in(
          semantic_context, request->name, request->name_len)) {
    resolution->function = NULL;
    return;
  }
  if (request->is_definition &&
      !ps_ctx_track_function_defined_in(
          semantic_context, request->name, request->name_len)) {
    resolution->function = NULL;
    resolution->status = PSX_FUNCTION_DECLARATION_DUPLICATE_DEFINITION;
    return;
  }
  resolution->effective_is_static =
      ps_function_symbol_has_internal_linkage(resolution->function);
  resolution->status = PSX_FUNCTION_DECLARATION_OK;
}
