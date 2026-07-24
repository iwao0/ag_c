#include "function_call_resolution.h"

#include "../parser/ast.h"
#include "../parser/semantic_ctx.h"
#include "assignment_resolution.h"
#include "type_completeness.h"

#include <string.h>

psx_builtin_call_kind_t psx_function_call_builtin_kind(
    const node_function_call_t *call) {
  static const char builtin_expect[] = "__builtin_expect";
  if (!call || !call->callee ||
      call->callee->kind != ND_IDENTIFIER)
    return PSX_BUILTIN_CALL_NONE;
  const node_identifier_t *identifier =
      (const node_identifier_t *)call->callee;
  if (identifier->name_len ==
          (int)(sizeof(builtin_expect) - 1) &&
      memcmp(
          identifier->name, builtin_expect,
          sizeof(builtin_expect) - 1) == 0)
    return PSX_BUILTIN_CALL_EXPECT;
  static const struct {
    const char *name;
    size_t length;
    psx_builtin_call_kind_t kind;
  } atomic_builtins[] = {
      {"__ag_atomic_load", sizeof("__ag_atomic_load") - 1,
       PSX_BUILTIN_CALL_ATOMIC_LOAD},
      {"__ag_atomic_store", sizeof("__ag_atomic_store") - 1,
       PSX_BUILTIN_CALL_ATOMIC_STORE},
      {"__ag_atomic_exchange", sizeof("__ag_atomic_exchange") - 1,
       PSX_BUILTIN_CALL_ATOMIC_EXCHANGE},
      {"__ag_atomic_cas", sizeof("__ag_atomic_cas") - 1,
       PSX_BUILTIN_CALL_ATOMIC_COMPARE_EXCHANGE},
      {"__ag_atomic_fetch_add", sizeof("__ag_atomic_fetch_add") - 1,
       PSX_BUILTIN_CALL_ATOMIC_FETCH_ADD},
      {"__ag_atomic_fetch_sub", sizeof("__ag_atomic_fetch_sub") - 1,
       PSX_BUILTIN_CALL_ATOMIC_FETCH_SUB},
      {"__ag_atomic_fetch_or", sizeof("__ag_atomic_fetch_or") - 1,
       PSX_BUILTIN_CALL_ATOMIC_FETCH_OR},
      {"__ag_atomic_fetch_xor", sizeof("__ag_atomic_fetch_xor") - 1,
       PSX_BUILTIN_CALL_ATOMIC_FETCH_XOR},
      {"__ag_atomic_fetch_and", sizeof("__ag_atomic_fetch_and") - 1,
       PSX_BUILTIN_CALL_ATOMIC_FETCH_AND},
      {"__ag_atomic_fence", sizeof("__ag_atomic_fence") - 1,
       PSX_BUILTIN_CALL_ATOMIC_FENCE},
  };
  for (size_t i = 0;
       i < sizeof(atomic_builtins) / sizeof(atomic_builtins[0]); i++) {
    if (identifier->name_len == (int)atomic_builtins[i].length &&
        memcmp(identifier->name, atomic_builtins[i].name,
               atomic_builtins[i].length) == 0)
      return atomic_builtins[i].kind;
  }
  return PSX_BUILTIN_CALL_NONE;
}

int psx_builtin_call_is_atomic(psx_builtin_call_kind_t kind) {
  return kind >= PSX_BUILTIN_CALL_ATOMIC_LOAD &&
         kind <= PSX_BUILTIN_CALL_ATOMIC_FENCE;
}

static int atomic_builtin_argument_count(
    psx_builtin_call_kind_t kind) {
  switch (kind) {
    case PSX_BUILTIN_CALL_ATOMIC_FENCE: return 0;
    case PSX_BUILTIN_CALL_ATOMIC_LOAD: return 1;
    case PSX_BUILTIN_CALL_ATOMIC_STORE:
    case PSX_BUILTIN_CALL_ATOMIC_EXCHANGE:
    case PSX_BUILTIN_CALL_ATOMIC_FETCH_ADD:
    case PSX_BUILTIN_CALL_ATOMIC_FETCH_SUB:
    case PSX_BUILTIN_CALL_ATOMIC_FETCH_OR:
    case PSX_BUILTIN_CALL_ATOMIC_FETCH_XOR:
    case PSX_BUILTIN_CALL_ATOMIC_FETCH_AND:
      return 2;
    case PSX_BUILTIN_CALL_ATOMIC_COMPARE_EXCHANGE: return 3;
    case PSX_BUILTIN_CALL_NONE:
    case PSX_BUILTIN_CALL_EXPECT:
      return -1;
  }
  return -1;
}

static int atomic_builtin_returns_object_value(
    psx_builtin_call_kind_t kind) {
  return kind == PSX_BUILTIN_CALL_ATOMIC_LOAD ||
         kind == PSX_BUILTIN_CALL_ATOMIC_EXCHANGE ||
         kind == PSX_BUILTIN_CALL_ATOMIC_FETCH_ADD ||
         kind == PSX_BUILTIN_CALL_ATOMIC_FETCH_SUB ||
         kind == PSX_BUILTIN_CALL_ATOMIC_FETCH_OR ||
         kind == PSX_BUILTIN_CALL_ATOMIC_FETCH_XOR ||
         kind == PSX_BUILTIN_CALL_ATOMIC_FETCH_AND;
}

static int describe_qual_type(
    const psx_semantic_type_table_t *types,
    psx_qual_type_t type, psx_type_shape_t *shape) {
  return shape &&
         psx_semantic_type_table_qual_type_is_valid(types, type) &&
         psx_semantic_type_table_describe(types, type.type_id, shape);
}

static int atomic_builtin_assignment_is_valid(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t target, psx_qual_type_t value,
    int value_is_null_pointer_constant) {
  psx_assignment_types_resolution_t assignment;
  target.qualifiers = PSX_TYPE_QUALIFIER_NONE;
  psx_resolve_assignment_conversion_qual_types_in(
      semantic_context, target, value,
      value_is_null_pointer_constant, &assignment);
  return assignment.status == PSX_ASSIGNMENT_TYPES_OK;
}

int psx_resolve_atomic_builtin_call(
    psx_semantic_context_t *semantic_context,
    psx_builtin_call_kind_t kind,
    const psx_qual_type_t *argument_types,
    const unsigned char *argument_is_null_pointer_constant,
    int argument_count, psx_qual_type_t *result_type) {
  int expected_count = atomic_builtin_argument_count(kind);
  if (!semantic_context || !psx_builtin_call_is_atomic(kind) ||
      expected_count < 0 || argument_count != expected_count)
    return 0;
  if (kind == PSX_BUILTIN_CALL_ATOMIC_FENCE) return 1;

  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_type_shape_t object_pointer_shape = {0};
  if (!argument_types || !argument_is_null_pointer_constant ||
      !describe_qual_type(types, argument_types[0],
                          &object_pointer_shape) ||
      (object_pointer_shape.kind != PSX_TYPE_POINTER &&
       object_pointer_shape.kind != PSX_TYPE_ARRAY))
    return 0;
  psx_qual_type_t object_type = psx_semantic_type_table_base(
      types, argument_types[0].type_id);
  psx_type_shape_t object_shape = {0};
  if (!describe_qual_type(types, object_type, &object_shape) ||
      (object_shape.kind != PSX_TYPE_BOOL &&
       object_shape.kind != PSX_TYPE_INTEGER &&
       object_shape.kind != PSX_TYPE_POINTER))
    return 0;

  if (atomic_builtin_returns_object_value(kind) && result_type) {
    *result_type = object_type;
    result_type->qualifiers = PSX_TYPE_QUALIFIER_NONE;
  }
  if (kind == PSX_BUILTIN_CALL_ATOMIC_LOAD) return 1;

  if (kind == PSX_BUILTIN_CALL_ATOMIC_STORE ||
      kind == PSX_BUILTIN_CALL_ATOMIC_EXCHANGE) {
    return atomic_builtin_assignment_is_valid(
        semantic_context, object_type, argument_types[1],
        argument_is_null_pointer_constant[1]);
  }

  if (kind == PSX_BUILTIN_CALL_ATOMIC_COMPARE_EXCHANGE) {
    psx_type_shape_t expected_pointer_shape = {0};
    if (!describe_qual_type(
            types, argument_types[1], &expected_pointer_shape) ||
        (expected_pointer_shape.kind != PSX_TYPE_POINTER &&
         expected_pointer_shape.kind != PSX_TYPE_ARRAY))
      return 0;
    psx_qual_type_t expected_object = psx_semantic_type_table_base(
        types, argument_types[1].type_id);
    if (!psx_semantic_type_table_unqualified_types_match(
            types, object_type, expected_object))
      return 0;
    return atomic_builtin_assignment_is_valid(
        semantic_context, object_type, argument_types[2],
        argument_is_null_pointer_constant[2]);
  }

  psx_type_shape_t operand_shape = {0};
  if (!describe_qual_type(types, argument_types[1], &operand_shape) ||
      (operand_shape.kind != PSX_TYPE_BOOL &&
       operand_shape.kind != PSX_TYPE_INTEGER))
    return 0;
  if (object_shape.kind == PSX_TYPE_POINTER)
    return (kind == PSX_BUILTIN_CALL_ATOMIC_FETCH_ADD ||
            kind == PSX_BUILTIN_CALL_ATOMIC_FETCH_SUB) &&
           psx_semantic_pointer_points_to_complete_object_in(
               semantic_context, object_type);
  return object_shape.kind == PSX_TYPE_BOOL ||
         object_shape.kind == PSX_TYPE_INTEGER;
}

const node_t *psx_builtin_expect_value_operand(
    const node_function_call_t *call) {
  return psx_function_call_builtin_kind(call) ==
                 PSX_BUILTIN_CALL_EXPECT &&
             call->argument_count == 2 && call->arguments &&
             call->arguments[0] && call->arguments[1]
         ? call->arguments[0] : NULL;
}

psx_qual_type_t psx_resolve_function_reference_qual_type(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t function_qual_type) {
  psx_type_shape_t shape = {0};
  if (!semantic_context ||
      !psx_semantic_type_table_describe(
          ps_ctx_semantic_type_table_in(semantic_context),
          function_qual_type.type_id, &shape) ||
      shape.kind != PSX_TYPE_FUNCTION) {
    return (psx_qual_type_t){
        PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  }
  return ps_ctx_intern_pointer_to_qual_type_in(
      semantic_context, function_qual_type);
}
