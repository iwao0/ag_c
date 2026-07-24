#include "assignment_resolution.h"

#include <string.h>

#include "../parser/semantic_ctx.h"
#include "type_completeness.h"
#include "type_identity.h"

static int describe_type(
    const psx_semantic_type_table_t *types,
    psx_qual_type_t type, psx_type_shape_t *shape) {
  return shape && psx_semantic_type_table_qual_type_is_valid(types, type) &&
         psx_semantic_type_table_describe(types, type.type_id, shape);
}

static int kind_is_arithmetic(psx_type_kind_t kind) {
  return kind == PSX_TYPE_BOOL || kind == PSX_TYPE_INTEGER ||
         kind == PSX_TYPE_FLOAT || kind == PSX_TYPE_COMPLEX;
}

static int kind_is_integer(psx_type_kind_t kind) {
  return kind == PSX_TYPE_BOOL || kind == PSX_TYPE_INTEGER;
}

static int kind_is_scalar(psx_type_kind_t kind) {
  return kind_is_arithmetic(kind) || kind == PSX_TYPE_POINTER;
}

static int kind_is_aggregate(psx_type_kind_t kind) {
  return kind == PSX_TYPE_STRUCT || kind == PSX_TYPE_UNION;
}

static int unqualified_types_are_compatible(
    const psx_semantic_type_table_t *types,
    psx_qual_type_t left, psx_qual_type_t right) {
  left.qualifiers = PSX_TYPE_QUALIFIER_NONE;
  right.qualifiers = PSX_TYPE_QUALIFIER_NONE;
  return psx_semantic_type_table_types_compatible(
      types, left, right);
}

static int pointed_types_are_compatible(
    const psx_semantic_type_table_t *types,
    psx_qual_type_t target, psx_qual_type_t value,
    int require_identical_qualifiers) {
  psx_type_shape_t target_shape = {0};
  psx_type_shape_t value_shape = {0};
  if (!describe_type(types, target, &target_shape) ||
      !describe_type(types, value, &value_shape) ||
      target_shape.kind != value_shape.kind)
    return 0;
  if (require_identical_qualifiers &&
      target.qualifiers != value.qualifiers)
    return 0;
  if (target_shape.kind == PSX_TYPE_POINTER)
    return pointed_types_are_compatible(
        types, psx_semantic_type_table_base(types, target.type_id),
        psx_semantic_type_table_base(types, value.type_id), 1);
  if (target_shape.kind == PSX_TYPE_FUNCTION)
    return psx_semantic_type_table_function_types_compatible(
        types, target, value);
  if (target_shape.kind != PSX_TYPE_ARRAY)
    return unqualified_types_are_compatible(
        types, target, value);

  int target_has_constant_bound =
      !target_shape.is_vla && target_shape.array_len > 0;
  int value_has_constant_bound =
      !value_shape.is_vla && value_shape.array_len > 0;
  if (target_has_constant_bound && value_has_constant_bound &&
      target_shape.array_len != value_shape.array_len)
    return 0;
  return pointed_types_are_compatible(
      types, psx_semantic_type_table_base(types, target.type_id),
      psx_semantic_type_table_base(types, value.type_id),
      require_identical_qualifiers);
}

static int resolve_assignment_target(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t target_type,
    int require_modifiable,
    psx_assignment_types_resolution_t *resolution,
    psx_type_shape_t *target) {
  if (!semantic_context ||
      target_type.type_id == PSX_TYPE_ID_INVALID)
    return 0;
  psx_type_shape_t canonical = {0};
  if (!describe_type(
          ps_ctx_semantic_type_table_in(semantic_context),
          target_type, &canonical))
    return 0;
  if ((require_modifiable &&
       psx_semantic_qual_type_has_const_subobject_in(
           semantic_context, target_type)) ||
      canonical.kind == PSX_TYPE_ARRAY ||
      canonical.kind == PSX_TYPE_FUNCTION ||
      canonical.kind == PSX_TYPE_VOID ||
      !psx_semantic_type_is_complete_object_in(
          (psx_semantic_context_t *)semantic_context,
          target_type.type_id)) {
    resolution->status = PSX_ASSIGNMENT_TARGET_NOT_MODIFIABLE;
    return 0;
  }
  if (target) *target = canonical;
  return 1;
}

static int pointer_targets_are_compatible(
    const psx_semantic_type_table_t *types,
    psx_qual_type_t target, psx_qual_type_t value) {
  psx_qual_type_t target_base =
      psx_semantic_type_table_base(types, target.type_id);
  psx_qual_type_t value_base =
      psx_semantic_type_table_base(types, value.type_id);
  psx_type_shape_t target_shape = {0};
  psx_type_shape_t value_shape = {0};
  if (!describe_type(types, target_base, &target_shape) ||
      !describe_type(types, value_base, &value_shape))
    return 0;
  int target_function = target_shape.kind == PSX_TYPE_FUNCTION;
  int value_function = value_shape.kind == PSX_TYPE_FUNCTION;
  if (target_function || value_function) {
    if (target_function && value_function)
      return psx_semantic_type_table_function_types_compatible(
          types, target_base, value_base);
    return 0;
  }
  if (target_shape.kind == PSX_TYPE_VOID ||
      value_shape.kind == PSX_TYPE_VOID)
    return 1;
  if (((target_base.qualifiers ^ value_base.qualifiers) &
       PSX_TYPE_QUALIFIER_ATOMIC) != 0)
    return 0;
  return pointed_types_are_compatible(
      types, target_base, value_base, 0);
}

static void resolve_assignment_qual_types_in(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t target_type,
    psx_qual_type_t value_type,
    int value_is_null_pointer_constant,
    int require_modifiable,
    psx_assignment_types_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_ASSIGNMENT_TYPES_INVALID;
  resolution->result_qual_type = (psx_qual_type_t){
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  if (!semantic_context ||
      target_type.type_id == PSX_TYPE_ID_INVALID ||
      value_type.type_id == PSX_TYPE_ID_INVALID)
    return;

  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_type_shape_t value = {0};
  psx_type_shape_t target = {0};
  if (!describe_type(types, value_type, &value) ||
      !resolve_assignment_target(
          semantic_context, target_type, require_modifiable,
          resolution, &target))
    return;
  if ((value_type.qualifiers & PSX_TYPE_QUALIFIER_ATOMIC) != 0 &&
      (target_type.qualifiers & PSX_TYPE_QUALIFIER_ATOMIC) == 0) {
    resolution->status = PSX_ASSIGNMENT_TYPES_INCOMPATIBLE;
    return;
  }

  int compatible = 0;
  if (target.kind == PSX_TYPE_BOOL && kind_is_scalar(value.kind)) {
    compatible = 1;
  } else if (kind_is_arithmetic(target.kind) &&
             kind_is_arithmetic(value.kind)) {
    compatible = 1;
  } else if (target.kind == PSX_TYPE_POINTER &&
             value.kind == PSX_TYPE_POINTER) {
    compatible = pointer_targets_are_compatible(
        types, target_type, value_type);
    if (compatible) {
      psx_qual_type_t target_pointee =
          psx_semantic_type_table_pointee_value(
              types, target_type.type_id);
      psx_qual_type_t value_pointee =
          psx_semantic_type_table_pointee_value(
              types, value_type.type_id);
      unsigned int value_pointee_qualifiers =
          value_pointee.qualifiers;
      psx_type_shape_t target_pointee_shape = {0};
      psx_type_shape_t value_pointee_shape = {0};
      if (describe_type(types, target_pointee, &target_pointee_shape) &&
          describe_type(types, value_pointee, &value_pointee_shape) &&
          (target_pointee_shape.kind == PSX_TYPE_VOID ||
           value_pointee_shape.kind == PSX_TYPE_VOID)) {
        /* _Atomic identifies an atomic object type rather than an access
         * qualification that is lost through a void pointer.  The generic
         * atomic interfaces rely on conversion between A * and void *;
         * const/volatile/restrict still participate in qualifier checks. */
        value_pointee_qualifiers &= ~PSX_TYPE_QUALIFIER_ATOMIC;
      }
      if ((value_pointee_qualifiers &
           ~target_pointee.qualifiers) != 0) {
        resolution->status =
            PSX_ASSIGNMENT_DISCARDS_QUALIFIERS;
        return;
      }
    }
  } else if (target.kind == PSX_TYPE_POINTER &&
             kind_is_arithmetic(value.kind) &&
             value_is_null_pointer_constant) {
    compatible = 1;
  } else if (kind_is_aggregate(target.kind) &&
             kind_is_aggregate(value.kind)) {
    compatible = psx_semantic_type_table_unqualified_types_match(
        types, target_type, value_type);
  }

  if (!compatible) {
    resolution->status = PSX_ASSIGNMENT_TYPES_INCOMPATIBLE;
    return;
  }
  resolution->status = PSX_ASSIGNMENT_TYPES_OK;
  resolution->result_qual_type = (psx_qual_type_t){
      target_type.type_id, PSX_TYPE_QUALIFIER_NONE};
}

void psx_resolve_assignment_qual_types_in(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t target_type,
    psx_qual_type_t value_type,
    int value_is_null_pointer_constant,
    psx_assignment_types_resolution_t *resolution) {
  resolve_assignment_qual_types_in(
      semantic_context, target_type, value_type,
      value_is_null_pointer_constant, 1, resolution);
}

void psx_resolve_assignment_conversion_qual_types_in(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t target_type,
    psx_qual_type_t value_type,
    int value_is_null_pointer_constant,
    psx_assignment_types_resolution_t *resolution) {
  resolve_assignment_qual_types_in(
      semantic_context, target_type, value_type,
      value_is_null_pointer_constant, 0, resolution);
}

void psx_resolve_return_qual_types_in(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t return_type,
    psx_qual_type_t value_type,
    int value_is_null_pointer_constant,
    psx_return_types_status_t *status) {
  if (!status) return;
  *status = PSX_RETURN_TYPES_INVALID;
  return_type.qualifiers &= PSX_TYPE_QUALIFIER_ATOMIC;
  psx_assignment_types_resolution_t assignment;
  psx_resolve_assignment_conversion_qual_types_in(
      semantic_context, return_type, value_type,
      value_is_null_pointer_constant, &assignment);
  switch (assignment.status) {
    case PSX_ASSIGNMENT_TYPES_OK:
      *status = PSX_RETURN_TYPES_OK;
      return;
    case PSX_ASSIGNMENT_TYPES_INCOMPATIBLE:
      *status = PSX_RETURN_TYPES_INCOMPATIBLE;
      return;
    case PSX_ASSIGNMENT_DISCARDS_QUALIFIERS:
      *status = PSX_RETURN_TYPES_DISCARDS_QUALIFIERS;
      return;
    case PSX_ASSIGNMENT_TYPES_INVALID:
    case PSX_ASSIGNMENT_TARGET_NOT_MODIFIABLE:
      return;
  }
}

void psx_resolve_compound_assignment_qual_types_in(
    psx_semantic_context_t *semantic_context,
    psx_compound_assignment_operator_t operation,
    psx_qual_type_t target_type,
    psx_qual_type_t value_type,
    psx_assignment_types_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_ASSIGNMENT_TYPES_INVALID;
  resolution->result_qual_type = (psx_qual_type_t){
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  if (!semantic_context ||
      value_type.type_id == PSX_TYPE_ID_INVALID)
    return;

  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_type_shape_t target = {0};
  psx_type_shape_t value = {0};
  if (!describe_type(types, value_type, &value) ||
      !resolve_assignment_target(
          semantic_context, target_type, 1, resolution, &target))
    return;

  int compatible = 0;
  switch (operation) {
    case PSX_COMPOUND_ASSIGN_ADD:
    case PSX_COMPOUND_ASSIGN_SUB:
      compatible = target.kind == PSX_TYPE_POINTER
                       ? (target_type.qualifiers &
                          PSX_TYPE_QUALIFIER_ATOMIC) == 0 &&
                             kind_is_integer(value.kind) &&
                             psx_semantic_pointer_points_to_complete_object_in(
                                 semantic_context, target_type)
                       : kind_is_arithmetic(target.kind) &&
                             kind_is_arithmetic(value.kind);
      break;
    case PSX_COMPOUND_ASSIGN_MUL:
    case PSX_COMPOUND_ASSIGN_DIV:
      compatible = kind_is_arithmetic(target.kind) &&
                   kind_is_arithmetic(value.kind);
      break;
    case PSX_COMPOUND_ASSIGN_MOD:
    case PSX_COMPOUND_ASSIGN_SHL:
    case PSX_COMPOUND_ASSIGN_SHR:
    case PSX_COMPOUND_ASSIGN_BITAND:
    case PSX_COMPOUND_ASSIGN_BITXOR:
    case PSX_COMPOUND_ASSIGN_BITOR:
      compatible = kind_is_integer(target.kind) &&
                   kind_is_integer(value.kind);
      break;
  }
  if (!compatible) {
    resolution->status = PSX_ASSIGNMENT_TYPES_INCOMPATIBLE;
    return;
  }
  resolution->status = PSX_ASSIGNMENT_TYPES_OK;
  resolution->result_qual_type = (psx_qual_type_t){
      target_type.type_id, PSX_TYPE_QUALIFIER_NONE};
}
