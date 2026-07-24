#include "type_completeness.h"

#include "../parser/semantic_ctx.h"
#include "record_decl.h"
#include "type_identity.h"

static int semantic_record_contains_flexible_array_member(
    psx_semantic_context_t *semantic_context,
    psx_record_id_t record_id, int depth);

static int semantic_type_contains_flexible_array_member(
    psx_semantic_context_t *semantic_context,
    psx_type_id_t type_id, int depth) {
  if (!semantic_context || type_id == PSX_TYPE_ID_INVALID ||
      depth > 128)
    return 0;
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_type_shape_t shape = {0};
  if (!psx_semantic_type_table_describe(types, type_id, &shape))
    return 0;
  if (shape.kind == PSX_TYPE_POINTER ||
      shape.kind == PSX_TYPE_FUNCTION)
    return 0;
  if (shape.kind == PSX_TYPE_ARRAY) {
    if (!shape.is_vla && shape.array_len <= 0) return 1;
    psx_qual_type_t element =
        psx_semantic_type_table_base(types, type_id);
    return semantic_type_contains_flexible_array_member(
        semantic_context, element.type_id, depth + 1);
  }
  if (shape.kind == PSX_TYPE_STRUCT ||
      shape.kind == PSX_TYPE_UNION)
    return semantic_record_contains_flexible_array_member(
        semantic_context, shape.record_id, depth + 1);
  return 0;
}

static int semantic_record_contains_flexible_array_member(
    psx_semantic_context_t *semantic_context,
    psx_record_id_t record_id, int depth) {
  if (!semantic_context || record_id == PSX_RECORD_ID_INVALID ||
      depth > 128)
    return 0;
  const psx_record_decl_t *record = ps_ctx_get_record_decl_in(
      semantic_context, record_id);
  if (!record) return 0;
  for (int i = 0; i < record->member_count; i++) {
    const psx_record_member_decl_t *member = &record->members[i];
    if (semantic_type_contains_flexible_array_member(
            semantic_context, member->decl_qual_type.type_id,
            depth + 1))
      return 1;
  }
  return 0;
}

int psx_semantic_record_contains_flexible_array_member_in(
    psx_semantic_context_t *semantic_context,
    psx_record_id_t record_id) {
  return semantic_record_contains_flexible_array_member(
      semantic_context, record_id, 0);
}

static int semantic_type_has_flexible_array_element(
    psx_semantic_context_t *semantic_context,
    psx_type_id_t type_id, int depth) {
  if (!semantic_context || type_id == PSX_TYPE_ID_INVALID ||
      depth > 128)
    return 0;
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_type_shape_t shape = {0};
  if (!psx_semantic_type_table_describe(types, type_id, &shape))
    return 0;
  if (shape.kind == PSX_TYPE_ARRAY) {
    psx_qual_type_t element =
        psx_semantic_type_table_base(types, type_id);
    if (semantic_type_contains_flexible_array_member(
            semantic_context, element.type_id, depth + 1))
      return 1;
    return semantic_type_has_flexible_array_element(
        semantic_context, element.type_id, depth + 1);
  }
  if (shape.kind == PSX_TYPE_POINTER) {
    psx_qual_type_t pointee =
        psx_semantic_type_table_base(types, type_id);
    return semantic_type_has_flexible_array_element(
        semantic_context, pointee.type_id, depth + 1);
  }
  if (shape.kind == PSX_TYPE_FUNCTION) {
    psx_qual_type_t return_type =
        psx_semantic_type_table_base(types, type_id);
    if (semantic_type_has_flexible_array_element(
            semantic_context, return_type.type_id, depth + 1))
      return 1;
    for (int i = 0; i < shape.parameter_count; i++) {
      psx_qual_type_t parameter =
          psx_semantic_type_table_parameter(types, type_id, i);
      if (semantic_type_has_flexible_array_element(
              semantic_context, parameter.type_id, depth + 1))
        return 1;
    }
  }
  return 0;
}

int psx_semantic_type_has_flexible_array_element_in(
    psx_semantic_context_t *semantic_context,
    psx_type_id_t type_id) {
  return semantic_type_has_flexible_array_element(
      semantic_context, type_id, 0);
}

static int semantic_type_has_incomplete_array_element(
    psx_semantic_context_t *semantic_context,
    psx_type_id_t type_id, int depth) {
  if (!semantic_context || type_id == PSX_TYPE_ID_INVALID ||
      depth > 128)
    return 0;
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_type_shape_t shape = {0};
  if (!psx_semantic_type_table_describe(types, type_id, &shape))
    return 0;
  if (shape.kind == PSX_TYPE_ARRAY) {
    psx_qual_type_t element =
        psx_semantic_type_table_base(types, type_id);
    if (!psx_semantic_type_is_complete_object_in(
            semantic_context, element.type_id))
      return 1;
    return semantic_type_has_incomplete_array_element(
        semantic_context, element.type_id, depth + 1);
  }
  if (shape.kind == PSX_TYPE_POINTER) {
    psx_qual_type_t pointee =
        psx_semantic_type_table_base(types, type_id);
    return semantic_type_has_incomplete_array_element(
        semantic_context, pointee.type_id, depth + 1);
  }
  if (shape.kind == PSX_TYPE_FUNCTION) {
    psx_qual_type_t return_type =
        psx_semantic_type_table_base(types, type_id);
    if (semantic_type_has_incomplete_array_element(
            semantic_context, return_type.type_id, depth + 1))
      return 1;
    for (int i = 0; i < shape.parameter_count; i++) {
      psx_qual_type_t parameter =
          psx_semantic_type_table_parameter(types, type_id, i);
      if (semantic_type_has_incomplete_array_element(
              semantic_context, parameter.type_id, depth + 1))
        return 1;
    }
  }
  return 0;
}

int psx_semantic_type_has_incomplete_array_element_in(
    psx_semantic_context_t *semantic_context,
    psx_type_id_t type_id) {
  return semantic_type_has_incomplete_array_element(
      semantic_context, type_id, 0);
}

int psx_semantic_type_is_complete_object_in(
    psx_semantic_context_t *semantic_context,
    psx_type_id_t type_id) {
  if (!semantic_context || type_id == PSX_TYPE_ID_INVALID)
    return 0;
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_type_shape_t type = {0};
  if (!psx_semantic_type_table_describe(types, type_id, &type) ||
      type.kind == PSX_TYPE_INVALID || type.kind == PSX_TYPE_VOID ||
      type.kind == PSX_TYPE_FUNCTION)
    return 0;
  if (type.kind == PSX_TYPE_INTEGER &&
      type.integer_kind == PSX_INTEGER_KIND_ENUM)
    return ps_ctx_enum_declaration_is_complete_in(
        semantic_context, type.enum_decl_id);
  if (psx_type_kind_is_aggregate(type.kind)) {
    const psx_record_decl_t *record = ps_ctx_get_record_decl_in(
        semantic_context, type.record_id);
    return record && record->is_complete;
  }
  if (type.kind == PSX_TYPE_ARRAY) {
    psx_qual_type_t element =
        psx_semantic_type_table_base(types, type_id);
    return (type.array_len > 0 || type.is_vla) &&
           psx_semantic_type_is_complete_object_in(
               semantic_context, element.type_id);
  }
  return 1;
}

int psx_semantic_pointer_points_to_complete_object_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t pointer_type) {
  if (!semantic_context ||
      pointer_type.type_id == PSX_TYPE_ID_INVALID)
    return 0;
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_type_shape_t pointer = {0};
  if (!psx_semantic_type_table_describe(
          types, pointer_type.type_id, &pointer) ||
      pointer.kind != PSX_TYPE_POINTER)
    return 0;
  psx_qual_type_t pointee =
      psx_semantic_type_table_base(types, pointer_type.type_id);
  return psx_semantic_type_is_complete_object_in(
      semantic_context, pointee.type_id);
}

static int semantic_qual_type_has_const_subobject(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t type, int depth) {
  if (!semantic_context || type.type_id == PSX_TYPE_ID_INVALID ||
      depth > 128)
    return 0;
  if ((type.qualifiers & PSX_TYPE_QUALIFIER_CONST) != 0)
    return 1;
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_type_shape_t shape = {0};
  if (!psx_semantic_type_table_describe(
          types, type.type_id, &shape))
    return 0;
  if (shape.kind == PSX_TYPE_ARRAY) {
    return semantic_qual_type_has_const_subobject(
        semantic_context,
        psx_semantic_type_table_base(types, type.type_id),
        depth + 1);
  }
  if (shape.kind != PSX_TYPE_STRUCT &&
      shape.kind != PSX_TYPE_UNION)
    return 0;
  const psx_record_decl_t *record = ps_ctx_get_record_decl_in(
      semantic_context, shape.record_id);
  if (!record || !record->is_complete) return 0;
  for (int i = 0; i < record->member_count; i++) {
    if (semantic_qual_type_has_const_subobject(
            semantic_context, record->members[i].decl_qual_type,
            depth + 1))
      return 1;
  }
  return 0;
}

int psx_semantic_qual_type_has_const_subobject_in(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t type) {
  return semantic_qual_type_has_const_subobject(
      semantic_context, type, 0);
}

static int semantic_type_has_invalid_atomic_qualification(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t type, int depth) {
  if (!semantic_context || type.type_id == PSX_TYPE_ID_INVALID ||
      depth > 128)
    return 1;
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_type_shape_t shape = {0};
  if (!psx_semantic_type_table_describe(
          types, type.type_id, &shape))
    return 1;
  if ((type.qualifiers & PSX_TYPE_QUALIFIER_ATOMIC) != 0 &&
      (shape.kind == PSX_TYPE_ARRAY ||
       !psx_semantic_type_is_complete_object_in(
           semantic_context, type.type_id)))
    return 1;
  if (shape.kind == PSX_TYPE_POINTER ||
      shape.kind == PSX_TYPE_ARRAY ||
      shape.kind == PSX_TYPE_FUNCTION) {
    psx_qual_type_t base =
        psx_semantic_type_table_base(types, type.type_id);
    if (semantic_type_has_invalid_atomic_qualification(
            semantic_context, base, depth + 1))
      return 1;
  }
  if (shape.kind == PSX_TYPE_FUNCTION) {
    for (int i = 0; i < shape.parameter_count; i++) {
      psx_qual_type_t parameter =
          psx_semantic_type_table_parameter(
              types, type.type_id, i);
      if (semantic_type_has_invalid_atomic_qualification(
              semantic_context, parameter, depth + 1))
        return 1;
    }
  }
  return 0;
}

int psx_semantic_type_has_invalid_atomic_qualification_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t type) {
  return semantic_type_has_invalid_atomic_qualification(
      semantic_context, type, 0);
}
