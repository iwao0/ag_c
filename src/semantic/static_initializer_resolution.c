#include "static_initializer_resolution.h"

#include "declaration_resolution.h"
#include "../parser/node_utils.h"
#include "../parser/semantic_ctx.h"
#include "syntax_typed_hir_resolution.h"

#include <string.h>

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_global_registry_t *global_registry;
  psx_local_registry_t *local_registry;
} static_initializer_constant_context_t;

static int resolve_static_initializer_constant_index(
    void *opaque, const node_t *expression, long long *value) {
  static_initializer_constant_context_t *context = opaque;
  if (!context || !expression) return 0;
  const psx_typed_hir_tree_t *typed_hir = NULL;
  psx_syntax_integer_constant_result_t constant = {0};
  psx_resolved_hir_build_failure_t failure = {0};
  psx_syntax_typed_hir_resolution_status_t status =
      psx_resolve_syntax_integer_constant_expression_direct_to_typed_hir_in_contexts(
          context->semantic_context, context->global_registry,
          context->local_registry, NULL, expression, &typed_hir,
          &constant, &failure);
  if (status != PSX_SYNTAX_TYPED_HIR_RESOLVED || !typed_hir ||
      !constant.is_constant)
    return 0;
  if (value) *value = constant.value;
  return 1;
}

void psx_resolve_static_initializer(
    const psx_static_initializer_resolution_request_t *request,
    psx_static_initializer_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_STATIC_INITIALIZER_INVALID;
  if (!request || !request->semantic_context ||
      !request->global_registry || !request->local_registry ||
      request->type.type_id == PSX_TYPE_ID_INVALID ||
      !request->initializer) return;
  if (request->already_initialized) {
    resolution->status = PSX_STATIC_INITIALIZER_DUPLICATE_DEFINITION;
    return;
  }

  psx_decl_init_kind_t kind = request->kind;
  const node_t *initializer = request->initializer;
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
    static_initializer_constant_context_t constant_context = {
        .semantic_context = semantic_context,
        .global_registry = request->global_registry,
        .local_registry = request->local_registry,
    };
    psx_incomplete_array_resolution_t array_resolution;
    if (!psx_resolve_incomplete_array_initializer_shape_in(
            semantic_context, request->type, kind, initializer,
            resolve_static_initializer_constant_index,
            &constant_context, &array_resolution) ||
        !psx_resolve_completed_incomplete_array_qual_type_in(
            semantic_context, request->type, &array_resolution,
            &object_qual_type)) {
      resolution->status = PSX_STATIC_INITIALIZER_ARRAY_COMPLETION_FAILED;
      return;
    }
    resolution->type_completed = 1;
    if (!psx_semantic_type_table_describe(
            semantic_types, object_qual_type.type_id, &object_shape))
      return;
  }

  resolution->object_qual_type = object_qual_type;

  if (kind == PSX_DECL_INIT_LIST) {
    if (initializer->kind != ND_INIT_LIST) return;
    if (object_shape.kind == PSX_TYPE_ARRAY ||
        psx_type_kind_is_aggregate(object_shape.kind)) {
      resolution->is_aggregate_initializer = 1;
      resolution->status = PSX_STATIC_INITIALIZER_OK;
      return;
    }

    const node_init_list_t *list = (const node_init_list_t *)initializer;
    if (list->entry_count != 1 || !list->entries ||
        list->entries[0].designator_count > 0 ||
        !list->entries[0].value ||
        list->entries[0].value->kind == ND_INIT_LIST) {
      resolution->status = PSX_STATIC_INITIALIZER_INVALID_SCALAR_LIST;
      return;
    }
    resolution->scalar_list_value_selected = 1;
  }

  resolution->status = PSX_STATIC_INITIALIZER_OK;
}
