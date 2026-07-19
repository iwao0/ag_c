#include "assignment_resolution.h"

#include <string.h>

#include "../parser/semantic_ctx.h"
#include "../parser/type.h"
#include "type_identity.h"

static int is_arithmetic_type(const psx_type_t *type) {
  return type &&
         (type->kind == PSX_TYPE_BOOL ||
          type->kind == PSX_TYPE_INTEGER ||
          type->kind == PSX_TYPE_FLOAT ||
          type->kind == PSX_TYPE_COMPLEX);
}

static int is_integer_type(const psx_type_t *type) {
  return type &&
         (type->kind == PSX_TYPE_BOOL ||
          type->kind == PSX_TYPE_INTEGER);
}

static int pointed_types_are_compatible(
    const psx_type_t *target, const psx_type_t *value) {
  if (!target || !value || target->kind != value->kind) return 0;
  if (target->kind != PSX_TYPE_ARRAY)
    return ps_type_unqualified_semantic_matches(target, value);

  int target_has_constant_bound =
      !target->is_vla && target->array_len > 0;
  int value_has_constant_bound =
      !value->is_vla && value->array_len > 0;
  if (target_has_constant_bound && value_has_constant_bound &&
      target->array_len != value->array_len)
    return 0;
  return pointed_types_are_compatible(target->base, value->base);
}

static int resolve_modifiable_target(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t target_type,
    psx_assignment_types_resolution_t *resolution,
    const psx_type_t **target) {
  if (!semantic_context ||
      target_type.type_id == PSX_TYPE_ID_INVALID)
    return 0;
  const psx_type_t *canonical = ps_ctx_type_by_id_in(
      semantic_context, target_type.type_id);
  if (!canonical) return 0;
  if ((target_type.qualifiers & PSX_TYPE_QUALIFIER_CONST) != 0 ||
      canonical->kind == PSX_TYPE_ARRAY ||
      canonical->kind == PSX_TYPE_FUNCTION ||
      canonical->kind == PSX_TYPE_VOID) {
    resolution->status = PSX_ASSIGNMENT_TARGET_NOT_MODIFIABLE;
    return 0;
  }
  if (target) *target = canonical;
  return 1;
}

static int pointer_targets_are_compatible(
    const psx_type_t *target, const psx_type_t *value) {
  if (!target || !value || !target->base || !value->base)
    return 0;
  int target_function = target->base->kind == PSX_TYPE_FUNCTION;
  int value_function = value->base->kind == PSX_TYPE_FUNCTION;
  if (target_function || value_function) {
    if (target_function && value_function)
      return ps_type_unqualified_semantic_matches(
          target->base, value->base);
    return 0;
  }
  if (target->base->kind == PSX_TYPE_VOID ||
      value->base->kind == PSX_TYPE_VOID)
    return 1;
  return pointed_types_are_compatible(target->base, value->base);
}

void psx_resolve_assignment_qual_types_in(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t target_type,
    psx_qual_type_t value_type,
    int value_is_null_pointer_constant,
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

  const psx_type_t *value = ps_ctx_type_by_id_in(
      semantic_context, value_type.type_id);
  const psx_type_t *target = NULL;
  if (!value || !resolve_modifiable_target(
                    semantic_context, target_type,
                    resolution, &target))
    return;

  int compatible = 0;
  if (target->kind == PSX_TYPE_BOOL && ps_type_is_scalar(value)) {
    compatible = 1;
  } else if (is_arithmetic_type(target) && is_arithmetic_type(value)) {
    compatible = 1;
  } else if (target->kind == PSX_TYPE_POINTER &&
             value->kind == PSX_TYPE_POINTER) {
    compatible = pointer_targets_are_compatible(target, value);
    if (compatible) {
      psx_qual_type_t target_pointee =
          psx_semantic_type_table_pointee_value(
              ps_ctx_semantic_type_table_in(semantic_context),
              target_type.type_id);
      psx_qual_type_t value_pointee =
          psx_semantic_type_table_pointee_value(
              ps_ctx_semantic_type_table_in(semantic_context),
              value_type.type_id);
      if ((value_pointee.qualifiers &
           ~target_pointee.qualifiers) != 0) {
        resolution->status =
            PSX_ASSIGNMENT_DISCARDS_QUALIFIERS;
        return;
      }
    }
  } else if (target->kind == PSX_TYPE_POINTER &&
             is_arithmetic_type(value) &&
             value_is_null_pointer_constant) {
    compatible = 1;
  } else if (ps_type_is_tag_aggregate(target) &&
             ps_type_is_tag_aggregate(value)) {
    compatible = ps_type_unqualified_semantic_matches(
        target, value);
  }

  if (!compatible) {
    resolution->status = PSX_ASSIGNMENT_TYPES_INCOMPATIBLE;
    return;
  }
  resolution->status = PSX_ASSIGNMENT_TYPES_OK;
  resolution->result_qual_type = (psx_qual_type_t){
      target_type.type_id, PSX_TYPE_QUALIFIER_NONE};
}

void psx_resolve_return_qual_types_in(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t return_type,
    psx_qual_type_t value_type,
    int value_is_null_pointer_constant,
    psx_return_types_status_t *status) {
  if (!status) return;
  *status = PSX_RETURN_TYPES_INVALID;
  return_type.qualifiers = PSX_TYPE_QUALIFIER_NONE;
  psx_assignment_types_resolution_t assignment;
  psx_resolve_assignment_qual_types_in(
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
    const psx_semantic_context_t *semantic_context,
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

  const psx_type_t *target = NULL;
  const psx_type_t *value = ps_ctx_type_by_id_in(
      semantic_context, value_type.type_id);
  if (!value || !resolve_modifiable_target(
                    semantic_context, target_type,
                    resolution, &target))
    return;

  int compatible = 0;
  switch (operation) {
    case PSX_COMPOUND_ASSIGN_ADD:
    case PSX_COMPOUND_ASSIGN_SUB:
      compatible = target->kind == PSX_TYPE_POINTER
                       ? is_integer_type(value)
                       : is_arithmetic_type(target) &&
                             is_arithmetic_type(value);
      break;
    case PSX_COMPOUND_ASSIGN_MUL:
    case PSX_COMPOUND_ASSIGN_DIV:
      compatible = is_arithmetic_type(target) &&
                   is_arithmetic_type(value);
      break;
    case PSX_COMPOUND_ASSIGN_MOD:
    case PSX_COMPOUND_ASSIGN_SHL:
    case PSX_COMPOUND_ASSIGN_SHR:
    case PSX_COMPOUND_ASSIGN_BITAND:
    case PSX_COMPOUND_ASSIGN_BITXOR:
    case PSX_COMPOUND_ASSIGN_BITOR:
      compatible = is_integer_type(target) &&
                   is_integer_type(value);
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
