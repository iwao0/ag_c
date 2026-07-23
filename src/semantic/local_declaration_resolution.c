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

static psx_local_vla_dimension_t dimension_for_op(
    const psx_runtime_declarator_application_t *application,
    int op_index) {
  const psx_declarator_op_t *op =
      application && op_index >= 0 &&
              op_index < application->shape.count
          ? &application->shape.ops[op_index]
          : NULL;
  const psx_runtime_array_bound_t *bound =
      bound_for_op(application, op_index);
  if (bound) {
    return (psx_local_vla_dimension_t){
        .expression_id = bound->expression_id,
        .constant_value =
            bound->is_constant ? bound->constant_value : 0,
        .is_constant = bound->is_constant,
    };
  }
  return (psx_local_vla_dimension_t){
      .expression_id = PSX_SEMANTIC_EXPR_ID_INVALID,
      .constant_value =
          op && op->kind == PSX_DECL_OP_ARRAY ? op->array_len : 0,
      .is_constant =
          op && op->kind == PSX_DECL_OP_ARRAY &&
          !op->is_vla_array && op->array_len > 0,
  };
}

void psx_resolve_local_declaration(
    const psx_local_declaration_resolution_request_t *request,
    psx_local_declaration_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_LOCAL_DECLARATION_INVALID;
  if (!request || !request->arena_context || !request->semantic_types ||
      !request->record_layouts ||
      !ag_data_layout_is_valid(request->data_layout) ||
      request->type_id == PSX_TYPE_ID_INVALID || !request->application)
    return;

  psx_type_shape_t type = {0};
  if (!psx_semantic_type_table_describe(
          request->semantic_types, request->type_id, &type))
    return;
  if (type.kind == PSX_TYPE_VOID) {
    resolution->status = PSX_LOCAL_DECLARATION_VOID_OBJECT;
    return;
  }
  if (type.kind == PSX_TYPE_FUNCTION) return;

  const psx_runtime_declarator_application_t *application =
      request->application;
  if (application->shape.count > 0) {
    resolution->dimensions = arena_alloc_in(
        request->arena_context,
        (size_t)application->shape.count * sizeof(*resolution->dimensions));
    if (!resolution->dimensions) return;
  }
  int leading_array_count = 0;
  int leading_array_has_vla = 0;
  for (int i = 0; i < application->shape.count; i++) {
    const psx_declarator_op_t *op = &application->shape.ops[i];
    if (op->kind != PSX_DECL_OP_ARRAY) break;
    resolution->dimensions[leading_array_count++] =
        dimension_for_op(application, i);
    if (op->is_vla_array) leading_array_has_vla = 1;
  }

  psx_qual_type_t element_identity =
      psx_semantic_type_table_pointee_value(
          request->semantic_types, request->type_id);
  int element_size =
      psx_type_layout_sizeof(request->semantic_types, request->record_layouts,
                        element_identity.type_id, request->data_layout);

  if (type.kind == PSX_TYPE_ARRAY && leading_array_has_vla) {
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

  if (type.kind == PSX_TYPE_ARRAY && type.array_len <= 0 &&
      !type.is_vla) {
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

  if (type.kind == PSX_TYPE_POINTER &&
      application->shape.count > 1 &&
      application->shape.ops[0].kind == PSX_DECL_OP_POINTER) {
    int pointer_op_count = 0;
    while (pointer_op_count < application->shape.count &&
           application->shape.ops[pointer_op_count].kind ==
               PSX_DECL_OP_POINTER)
      pointer_op_count++;
    int pointer_array_count = 0;
    int pointer_array_has_vla = 0;
    for (int i = pointer_op_count;
         i < application->shape.count; i++) {
      const psx_declarator_op_t *op = &application->shape.ops[i];
      if (op->kind != PSX_DECL_OP_ARRAY) break;
      resolution->dimensions[pointer_array_count++] =
          dimension_for_op(application, i);
      if (op->is_vla_array) pointer_array_has_vla = 1;
    }
    if (pointer_array_has_vla) {
      if (pointer_array_count <= 0 || element_size <= 0) return;
      resolution->storage_kind = PSX_LOCAL_STORAGE_POINTER_TO_VLA;
      resolution->dimension_count = pointer_array_count;
      resolution->pointer_indirections = pointer_op_count - 1;
      resolution->status = PSX_LOCAL_DECLARATION_OK;
      return;
    }
  }

  if (psx_type_layout_sizeof(request->semantic_types, request->record_layouts,
                        request->type_id, request->data_layout) <= 0) {
    resolution->status = PSX_LOCAL_DECLARATION_INCOMPLETE_OBJECT;
    return;
  }
  resolution->storage_kind = PSX_LOCAL_STORAGE_COMPLETE;
  resolution->status = PSX_LOCAL_DECLARATION_OK;
}
