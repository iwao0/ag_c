#include "parameter_declaration_resolution.h"

#include <string.h>

static const psx_type_t *parameter_leaf_type(const psx_type_t *type) {
  while (type && (type->kind == PSX_TYPE_POINTER ||
                  type->kind == PSX_TYPE_ARRAY)) {
    type = type->base;
  }
  return type;
}

static int has_runtime_inner_dimension(
    const psx_parameter_declaration_resolution_request_t *request) {
  for (int i = 0; i < request->inner_dimension_count; i++) {
    const psx_parameter_dimension_t *dimension =
        &request->inner_dimensions[i];
    if (dimension->constant <= 0 && dimension->source_name) return 1;
  }
  return 0;
}

int psx_resolve_parameter_declaration(
    const psx_parameter_declaration_resolution_request_t *request,
    psx_parameter_declaration_resolution_t *resolution) {
  if (!request || !resolution || request->inner_dimension_count < 0 ||
      request->inner_dimension_count > PSX_PARAMETER_MAX_INNER_DIMS) {
    return 0;
  }
  memset(resolution, 0, sizeof(*resolution));
  resolution->type = psx_resolve_decl_type(&request->type);
  if (!resolution->type) return 0;
  resolution->type = ps_type_adjust_parameter_type(resolution->type);
  if (!resolution->type ||
      !psx_plan_parameter_storage(resolution->type,
                                  &resolution->storage)) {
    return 0;
  }

  const psx_type_t *leaf = parameter_leaf_type(resolution->type);
  int leaf_is_aggregate = leaf && ps_type_is_tag_aggregate(leaf);
  resolution->element_size = ps_type_sizeof(leaf);
  if (resolution->element_size <= 0) resolution->element_size = 8;
  if ((request->is_array_declarator && !leaf_is_aggregate &&
       !request->is_pointer_declarator) ||
      (request->is_pointer_declarator && request->is_array_declarator &&
       !leaf_is_aggregate && !request->has_function_suffix &&
       has_runtime_inner_dimension(request))) {
    resolution->lowering_kind = PSX_PARAMETER_LOWER_VLA;
  }

  resolution->inner_dimension_count = request->inner_dimension_count;
  for (int i = 0; i < request->inner_dimension_count; i++) {
    resolution->inner_dimensions[i] = request->inner_dimensions[i];
  }
  return 1;
}
