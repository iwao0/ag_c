#include "parameter_declaration_resolution.h"
#include "../parser/arena.h"
#include "../parser/declarator_shape_builder.h"
#include "../parser/semantic_ctx.h"
#include "../parser/vla_runtime.h"
#include "../type_layout.h"

#include <string.h>

static int has_runtime_inner_dimension(
    const psx_parameter_declaration_resolution_request_t *request) {
  for (int i = 0; i < request->inner_dimension_count; i++) {
    const psx_parameter_dimension_t *dimension =
        &request->inner_dimensions[i];
    if (!dimension->is_constant &&
        dimension->expression_id != PSX_SEMANTIC_EXPR_ID_INVALID)
      return 1;
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

static int derived_leaf_is_aggregate(
    const psx_semantic_type_table_t *types,
    psx_type_id_t type_id) {
  psx_type_shape_t shape = {0};
  while (psx_semantic_type_table_describe(types, type_id, &shape) &&
         (shape.kind == PSX_TYPE_POINTER ||
          shape.kind == PSX_TYPE_ARRAY)) {
    psx_qual_type_t base =
        psx_semantic_type_table_base(types, type_id);
    if (base.type_id == PSX_TYPE_ID_INVALID) return 0;
    type_id = base.type_id;
  }
  return shape.kind == PSX_TYPE_STRUCT ||
         shape.kind == PSX_TYPE_UNION;
}

int psx_resolve_parameter_declaration(
    const psx_parameter_declaration_resolution_request_t *request,
    psx_parameter_declaration_resolution_t *resolution) {
  if (!request || !resolution || request->inner_dimension_count < 0 ||
      (request->inner_dimension_count > 0 && !request->inner_dimensions)) {
    return 0;
  }
  memset(resolution, 0, sizeof(*resolution));
  psx_qual_type_t identity =
      psx_resolve_decl_qual_type(&request->type);
  psx_type_shape_t shape = {0};
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(request->type.semantic_context);
  if (identity.type_id == PSX_TYPE_ID_INVALID ||
      !psx_semantic_type_table_describe(
          types, identity.type_id, &shape)) {
    return 0;
  }
  if (shape.kind == PSX_TYPE_ARRAY) {
    psx_qual_type_t element =
        psx_semantic_type_table_base(types, identity.type_id);
    identity = ps_ctx_intern_pointer_to_qual_type_in(
        request->type.semantic_context, element);
    const psx_declarator_shape_t *declarator_shape =
        request->type.declarator_shape;
    if (declarator_shape && declarator_shape->count > 0 &&
        declarator_shape->ops[0].kind == PSX_DECL_OP_ARRAY) {
      const psx_declarator_op_t *array_op = &declarator_shape->ops[0];
      if (array_op->is_const_qualified)
        identity.qualifiers |= PSX_TYPE_QUALIFIER_CONST;
      if (array_op->is_volatile_qualified)
        identity.qualifiers |= PSX_TYPE_QUALIFIER_VOLATILE;
      if (array_op->is_restrict_qualified)
        identity.qualifiers |= PSX_TYPE_QUALIFIER_RESTRICT;
      if (array_op->is_atomic_qualified)
        identity.qualifiers |= PSX_TYPE_QUALIFIER_ATOMIC;
    }
  } else if (shape.kind == PSX_TYPE_FUNCTION) {
    identity = ps_ctx_intern_pointer_to_qual_type_in(
        request->type.semantic_context, identity);
  }
  if (identity.type_id == PSX_TYPE_ID_INVALID ||
      psx_type_layout_sizeof(
          types,
          ps_ctx_record_layout_table_in(request->type.semantic_context),
          identity.type_id,
          ps_ctx_data_layout(request->type.semantic_context)) <= 0) {
    return 0;
  }
  resolution->declaration_qual_type = identity;
  resolution->function_qual_type = identity;
  /* C11 6.7.6.3p15 removes top-level qualifiers from the function
   * type, while the parameter object in a definition remains qualified. */
  resolution->function_qual_type.qualifiers =
      PSX_TYPE_QUALIFIER_NONE;

  int leaf_is_aggregate = derived_leaf_is_aggregate(
      types, identity.type_id);
  int has_pointer = request_shape_has(request, PSX_DECL_OP_POINTER);
  int has_array = request_shape_has(request, PSX_DECL_OP_ARRAY);
  int has_function = request_shape_has(request, PSX_DECL_OP_FUNCTION);
  if ((has_array && !leaf_is_aggregate && !has_pointer && !has_function) ||
      (has_pointer && has_array && !leaf_is_aggregate && !has_function &&
       has_runtime_inner_dimension(request))) {
    resolution->lowering_kind = PSX_PARAMETER_LOWER_VLA;
  }

  if (resolution->lowering_kind == PSX_PARAMETER_LOWER_VLA &&
      has_runtime_inner_dimension(request)) {
    if (ag_data_layout_scalar_size(
            ps_ctx_data_layout(request->type.semantic_context),
            AG_TARGET_SCALAR_LONG_LONG) != PSX_VLA_RUNTIME_SLOT_SIZE)
      return 0;
    psx_qual_type_t slot = ps_ctx_intern_integer_qual_type_in(
        request->type.semantic_context,
        PSX_INTEGER_KIND_LONG_LONG, 1, 0);
    psx_qual_type_t storage_identity = request->inner_dimension_count == 1
        ? slot
        : ps_ctx_intern_array_of_qual_type_in(
              request->type.semantic_context, slot,
              request->inner_dimension_count, 0);
    if (storage_identity.type_id == PSX_TYPE_ID_INVALID) return 0;
    resolution->runtime_stride_storage_type_id =
        storage_identity.type_id;
  }

  for (int i = 0; i < request->inner_dimension_count; i++) {
    if (!request->inner_dimensions[i].is_constant &&
        request->inner_dimensions[i].expression_id ==
            PSX_SEMANTIC_EXPR_ID_INVALID)
      return 0;
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
