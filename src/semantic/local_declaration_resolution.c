#include "local_declaration_resolution.h"

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

static const psx_type_t *storage_element_type(const psx_type_t *type) {
  const psx_type_t *element = type;
  if (element && element->kind == PSX_TYPE_POINTER) element = element->base;
  return ps_type_array_leaf_type(element);
}

void psx_resolve_local_declaration(
    const psx_local_declaration_resolution_request_t *request,
    psx_local_declaration_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_LOCAL_DECLARATION_INVALID;
  if (!request || !request->type || !request->application) return;

  psx_type_t *type = request->type;
  if (type->kind == PSX_TYPE_VOID) {
    resolution->status = PSX_LOCAL_DECLARATION_VOID_OBJECT;
    return;
  }
  if (type->kind == PSX_TYPE_FUNCTION) return;

  const psx_runtime_declarator_application_t *application =
      request->application;
  int leading_array_count = 0;
  int leading_array_has_vla = 0;
  for (int i = 0; i < application->shape.count; i++) {
    const psx_declarator_op_t *op = &application->shape.ops[i];
    if (op->kind != PSX_DECL_OP_ARRAY) break;
    if (leading_array_count >= PSX_LOCAL_DECLARATION_MAX_VLA_DIMS) return;
    const psx_runtime_array_bound_t *bound = bound_for_op(application, i);
    resolution->dimensions[leading_array_count++] =
        (psx_local_vla_dimension_t){
            .expression = bound ? bound->expression : NULL,
            .constant_value = bound && bound->is_constant
                                  ? bound->constant_value : 0,
            .is_constant = bound && bound->is_constant,
        };
    if (op->is_vla_array) leading_array_has_vla = 1;
  }

  const psx_type_t *element = storage_element_type(type);
  resolution->element_size = ps_type_sizeof(element);

  if (type->kind == PSX_TYPE_ARRAY && leading_array_has_vla) {
    if (resolution->element_size <= 0) {
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

  if (type->kind == PSX_TYPE_ARRAY && type->array_len <= 0 &&
      !type->is_vla) {
    if (resolution->element_size <= 0) {
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
      if (!bound || !bound->expression || resolution->element_size <= 0)
        return;
      resolution->storage_kind = PSX_LOCAL_STORAGE_POINTER_TO_VLA;
      resolution->pointer_row_dimension = bound->expression;
      resolution->status = PSX_LOCAL_DECLARATION_OK;
      return;
    }
  }

  if (ps_type_sizeof(type) <= 0) {
    resolution->status = PSX_LOCAL_DECLARATION_INCOMPLETE_OBJECT;
    return;
  }
  resolution->storage_kind = PSX_LOCAL_STORAGE_COMPLETE;
  resolution->status = PSX_LOCAL_DECLARATION_OK;
}
