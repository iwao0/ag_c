#include "static_initializer_resolution.h"

#include "declaration_resolution.h"
#include "../parser/node_utils.h"
#include "../parser/semantic_ctx.h"

#include <string.h>

void psx_resolve_static_initializer(
    const psx_static_initializer_resolution_request_t *request,
    psx_static_initializer_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_STATIC_INITIALIZER_INVALID;
  if (!request || !request->type || !request->initializer) return;
  if (request->already_initialized) {
    resolution->status = PSX_STATIC_INITIALIZER_DUPLICATE_DEFINITION;
    return;
  }

  resolution->type = request->type;
  resolution->kind = request->kind;
  resolution->initializer = request->initializer;
  ps_ctx_attach_aggregate_definitions(resolution->type);

  if (ps_type_is_incomplete_array(resolution->type)) {
    if (!psx_resolve_incomplete_array_initializer(
            resolution->type, resolution->kind,
            resolution->initializer)) {
      resolution->status = PSX_STATIC_INITIALIZER_ARRAY_COMPLETION_FAILED;
      return;
    }
    resolution->type_completed = 1;
  }

  if (resolution->kind == PSX_DECL_INIT_LIST) {
    if (resolution->initializer->kind != ND_INIT_LIST) return;
    if (resolution->type->kind == PSX_TYPE_ARRAY ||
        ps_type_is_tag_aggregate(resolution->type)) {
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
