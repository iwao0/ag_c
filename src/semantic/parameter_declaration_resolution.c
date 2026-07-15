#include "parameter_declaration_resolution.h"
#include "declaration_type_builder.h"
#include "../parser/arena.h"
#include "../parser/declarator_shape_builder.h"
#include "../parser/semantic_ctx.h"
#include "../parser/type_builder.h"

#include <string.h>

static int has_runtime_inner_dimension(
    const psx_parameter_declaration_resolution_request_t *request) {
  for (int i = 0; i < request->inner_dimension_count; i++) {
    const psx_parameter_dimension_t *dimension =
        &request->inner_dimensions[i];
    if (dimension->constant <= 0 && dimension->source_name) return 1;
  }
  return 0;
}

static int request_shape_has(
    const psx_parameter_declaration_resolution_request_t *request,
    psx_declarator_op_kind_t kind) {
  return request && request->type.declarator_shape &&
         ps_declarator_shape_count_ops(
             request->type.declarator_shape, kind) > 0;
}

int psx_resolve_parameter_declaration(
    const psx_parameter_declaration_resolution_request_t *request,
    psx_parameter_declaration_resolution_t *resolution) {
  if (!request || !resolution || request->inner_dimension_count < 0 ||
      (request->inner_dimension_count > 0 && !request->inner_dimensions)) {
    return 0;
  }
  memset(resolution, 0, sizeof(*resolution));
  psx_type_t *type = psx_build_decl_type(&request->type);
  if (!type) return 0;
  type = ps_type_adjust_parameter_type_in(
      ps_ctx_arena(request->type.semantic_context), type);
  if (!type ||
      !psx_plan_parameter_storage(type,
                                  &resolution->storage)) {
    return 0;
  }
  resolution->type = type;

  const psx_type_t *leaf = ps_type_derived_leaf_type(type);
  int leaf_is_aggregate = leaf && ps_type_is_tag_aggregate(leaf);
  int has_pointer = request_shape_has(request, PSX_DECL_OP_POINTER);
  int has_array = request_shape_has(request, PSX_DECL_OP_ARRAY);
  int has_function = request_shape_has(request, PSX_DECL_OP_FUNCTION);
  if ((has_array && !leaf_is_aggregate && !has_pointer && !has_function) ||
      (has_pointer && has_array && !leaf_is_aggregate && !has_function &&
       has_runtime_inner_dimension(request))) {
    resolution->lowering_kind = PSX_PARAMETER_LOWER_VLA;
  }

  resolution->inner_dimension_count = request->inner_dimension_count;
  if (request->inner_dimension_count > 0) {
    resolution->inner_dimensions = arena_alloc_in(
        ps_ctx_arena(request->type.semantic_context),
        (size_t)request->inner_dimension_count *
        sizeof(*resolution->inner_dimensions));
    memcpy(resolution->inner_dimensions, request->inner_dimensions,
           (size_t)request->inner_dimension_count *
           sizeof(*resolution->inner_dimensions));
  }
  return 1;
}
