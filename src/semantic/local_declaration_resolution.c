#include "local_declaration_resolution.h"
#include "../parser/arena.h"
#include "../type_layout.h"

#include <string.h>

static const psx_runtime_array_bound_t *bound_for_op(
    const psx_runtime_declarator_application_t *application,
    int op_index) {
  for (int i = 0; application && i < application->array_bound_count; i++) {
    if (application->array_bounds[i].declarator_op_index == op_index)
      return &application->array_bounds[i];
  }
  return NULL;
}

void psx_resolve_local_declaration(
    const psx_local_declaration_resolution_request_t *request,
    psx_local_declaration_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_LOCAL_DECLARATION_INVALID;
  if (!request || !request->arena_context || !request->semantic_types ||
      !request->record_layouts ||
      request->type_id == PSX_TYPE_ID_INVALID ||
      !request->application)
    return;

  const psx_type_t *type = psx_semantic_type_table_lookup(
      request->semantic_types, request->type_id);
  if (!type) return;
  if (type->kind == PSX_TYPE_VOID) {
    resolution->status = PSX_LOCAL_DECLARATION_VOID_OBJECT;
    return;
  }
  if (type->kind == PSX_TYPE_FUNCTION) return;

  const psx_runtime_declarator_application_t *application =
      request->application;
  if (application->shape.count > 0) {
    resolution->dimensions = arena_alloc_in(
        request->arena_context,
        (size_t)application->shape.count * sizeof(*resolution->dimensions));
  }
  int leading_array_count = 0;
  int leading_array_has_vla = 0;
  for (int i = 0; i < application->shape.count; i++) {
    const psx_declarator_op_t *op = &application->shape.ops[i];
    if (op->kind != PSX_DECL_OP_ARRAY) break;
    const psx_runtime_array_bound_t *bound = bound_for_op(application, i);
    resolution->dimensions[leading_array_count++] =
        (psx_local_vla_dimension_t){
            .expression_id = bound ? bound->expression_id
                                   : PSX_SEMANTIC_EXPR_ID_INVALID,
            .constant_value = bound && bound->is_constant
                                  ? bound->constant_value : 0,
            .is_constant = bound && bound->is_constant,
        };
    if (op->is_vla_array) leading_array_has_vla = 1;
  }

  psx_qual_type_t element_identity =
      psx_semantic_type_table_pointee_value(
          request->semantic_types, request->type_id);
  int element_size = ps_type_sizeof_id(
      request->semantic_types, request->record_layouts,
      element_identity.type_id, ag_target_info_data_layout(request->target));

  if (type->kind == PSX_TYPE_ARRAY && leading_array_has_vla) {
    if (element_size <= 0) {
      resolution->status = PSX_LOCAL_DECLARATION_INCOMPLETE_OBJECT;
      return;
    }
    if (request->has_initializer) {
      resolution->status = PSX_LOCAL_DECLARATION_VLA_INITIALIZER_FORBIDDEN;
      return;
    }
    resolution->storage_kind = PSX_LOCAL_STORAGE_VLA_OBJECT;
    resolution->dimension_count = leading_array_count;
    resolution->status = PSX_LOCAL_DECLARATION_OK;
    return;
  }

  if (ps_type_is_incomplete_array(type)) {
    if (element_size <= 0) {
      resolution->status = PSX_LOCAL_DECLARATION_INCOMPLETE_OBJECT;
      return;
    }
    if (!request->has_initializer) {
      resolution->status =
          PSX_LOCAL_DECLARATION_INCOMPLETE_ARRAY_NEEDS_INITIALIZER;
      return;
    }
    resolution->storage_kind = PSX_LOCAL_STORAGE_INCOMPLETE_ARRAY;
    resolution->status = PSX_LOCAL_DECLARATION_OK;
    return;
  }

  if (type->kind == PSX_TYPE_POINTER) {
    for (int i = 0; i < application->shape.count; i++) {
      const psx_declarator_op_t *op = &application->shape.ops[i];
      if (op->kind != PSX_DECL_OP_ARRAY || !op->is_vla_array) continue;
      const psx_runtime_array_bound_t *bound = bound_for_op(application, i);
      if (!bound ||
          bound->expression_id == PSX_SEMANTIC_EXPR_ID_INVALID ||
          element_size <= 0)
        return;
      resolution->storage_kind = PSX_LOCAL_STORAGE_POINTER_TO_VLA;
      resolution->pointer_row_dimension_id = bound->expression_id;
      resolution->status = PSX_LOCAL_DECLARATION_OK;
      return;
    }
  }

  if (ps_type_sizeof_id(request->semantic_types, request->record_layouts,
                        request->type_id,
                        ag_target_info_data_layout(request->target)) <= 0) {
    resolution->status = PSX_LOCAL_DECLARATION_INCOMPLETE_OBJECT;
    return;
  }
  resolution->storage_kind = PSX_LOCAL_STORAGE_COMPLETE;
  resolution->status = PSX_LOCAL_DECLARATION_OK;
}
