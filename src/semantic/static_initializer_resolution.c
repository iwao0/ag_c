#include "static_initializer_resolution.h"

#include "declaration_resolution.h"
#include "../parser/node_utils.h"
#include "../parser/semantic_ctx.h"
#include "../parser/type_builder.h"

#include <string.h>

void psx_resolve_static_initializer(
    const psx_static_initializer_resolution_request_t *request,
    psx_static_initializer_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_STATIC_INITIALIZER_INVALID;
  if (!request || !request->semantic_context ||
      request->type.type_id == PSX_TYPE_ID_INVALID ||
      !request->initializer) return;
  if (request->already_initialized) {
    resolution->status = PSX_STATIC_INITIALIZER_DUPLICATE_DEFINITION;
    return;
  }

  resolution->kind = request->kind;
  resolution->initializer = request->initializer;
  psx_semantic_context_t *semantic_context = request->semantic_context;
  const psx_semantic_type_table_t *semantic_types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_type_shape_t object_shape = {0};
  if (!psx_semantic_type_table_describe(
          semantic_types, request->type.type_id, &object_shape))
    return;

  psx_qual_type_t object_qual_type = request->type;

  if (object_shape.kind == PSX_TYPE_ARRAY && object_shape.array_len <= 0 &&
      !object_shape.is_vla) {
    const psx_type_t *type_view =
        psx_semantic_type_table_lookup_qual_type(
            semantic_types, request->type);
    if (!type_view) return;
    psx_type_t *type = ps_type_clone_in(
        ps_ctx_arena(semantic_context), type_view);
    if (!type) return;
    if (!psx_resolve_incomplete_array_initializer(
            semantic_context, type, resolution->kind,
            resolution->initializer)) {
      resolution->status = PSX_STATIC_INITIALIZER_ARRAY_COMPLETION_FAILED;
      return;
    }
    resolution->type_completed = 1;
    object_qual_type = ps_ctx_intern_qual_type_in(
        semantic_context, type);
    if (object_qual_type.type_id == PSX_TYPE_ID_INVALID) return;
    if (!psx_semantic_type_table_describe(
            semantic_types, object_qual_type.type_id, &object_shape))
      return;
  }

  resolution->object_qual_type = object_qual_type;

  if (resolution->kind == PSX_DECL_INIT_LIST) {
    if (resolution->initializer->kind != ND_INIT_LIST) return;
    if (object_shape.kind == PSX_TYPE_ARRAY ||
        psx_type_kind_is_aggregate(object_shape.kind)) {
      resolution->is_aggregate_initializer = 1;
      resolution->status = PSX_STATIC_INITIALIZER_OK;
      return;
    }

    node_init_list_t *list = (node_init_list_t *)resolution->initializer;
    if (list->entry_count != 1 ||
        list->entries[0].designator_count > 0 ||
        !list->entries[0].value ||
        list->entries[0].value->kind == ND_INIT_LIST) {
      resolution->status = PSX_STATIC_INITIALIZER_INVALID_SCALAR_LIST;
      return;
    }
    resolution->initializer = list->entries[0].value;
    resolution->kind = PSX_DECL_INIT_EXPR;
  }

  resolution->status = PSX_STATIC_INITIALIZER_OK;
}
