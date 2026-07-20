#include "integer_constant_evaluation.h"

#include <stdint.h>

static int integer_cast_width(const psx_type_shape_t *type) {
  if (!type || type->kind != PSX_TYPE_INTEGER) return 0;
  switch (type->integer_kind) {
    case PSX_INTEGER_KIND_CHAR: return 1;
    case PSX_INTEGER_KIND_SHORT: return 2;
    case PSX_INTEGER_KIND_INT:
    case PSX_INTEGER_KIND_ENUM:
      return 4;
    case PSX_INTEGER_KIND_LONG_LONG: return 8;
    case PSX_INTEGER_KIND_BOOL:
    case PSX_INTEGER_KIND_LONG:
    case PSX_INTEGER_KIND_NONE:
      return 0;
  }
  return 0;
}

int psx_normalize_integer_constant_cast(
    const psx_type_shape_t *target, long long operand, long long *result) {
  if (!target || !result) return 0;
  if (target->kind == PSX_TYPE_BOOL) {
    *result = operand != 0;
    return 1;
  }
  if (target->kind != PSX_TYPE_INTEGER) return 0;
  int bits = integer_cast_width(target) * 8;
  if (bits <= 0 || bits >= 64) {
    *result = operand;
    return 1;
  }
  uint64_t mask = (UINT64_C(1) << bits) - 1;
  uint64_t normalized = (uint64_t)operand & mask;
  if (!target->is_unsigned &&
      (normalized & (UINT64_C(1) << (bits - 1))))
    normalized |= ~mask;
  *result = (long long)normalized;
  return 1;
}

int psx_apply_integer_constant_binary(
    psx_syntax_node_kind_t operation,
    long long lhs, long long rhs, long long *result) {
  if (!result) return 0;
  switch (operation) {
    case ND_ADD: *result = lhs + rhs; return 1;
    case ND_SUB: *result = lhs - rhs; return 1;
    case ND_MUL: *result = lhs * rhs; return 1;
    case ND_DIV:
      if (!rhs) return 0;
      *result = lhs / rhs;
      return 1;
    case ND_MOD:
      if (!rhs) return 0;
      *result = lhs % rhs;
      return 1;
    case ND_SHL:
      if (rhs < 0 || rhs >= 64) return 0;
      *result = lhs << rhs;
      return 1;
    case ND_SHR:
      if (rhs < 0 || rhs >= 64) return 0;
      *result = lhs >> rhs;
      return 1;
    case ND_BITAND: *result = lhs & rhs; return 1;
    case ND_BITXOR: *result = lhs ^ rhs; return 1;
    case ND_BITOR: *result = lhs | rhs; return 1;
    case ND_EQ: *result = lhs == rhs; return 1;
    case ND_NE: *result = lhs != rhs; return 1;
    case ND_LT: *result = lhs < rhs; return 1;
    case ND_LE: *result = lhs <= rhs; return 1;
    case ND_GT: *result = lhs > rhs; return 1;
    case ND_GE: *result = lhs >= rhs; return 1;
    case ND_LOGAND: *result = lhs && rhs; return 1;
    case ND_LOGOR: *result = lhs || rhs; return 1;
    default:
      return 0;
  }
}
