#include "integer_constant_evaluation.h"

#include <limits.h>
#include <stdint.h>

#include "../target_info.h"
#include "../type_system/integer_conversion.h"

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

int psx_normalize_floating_constant_cast(
    const psx_type_shape_t *target, double operand, long long *result) {
  if (!target || !result) return 0;
  if (target->kind == PSX_TYPE_BOOL) {
    *result = operand != 0.0;
    return 1;
  }
  if (target->kind != PSX_TYPE_INTEGER || operand != operand) return 0;

  int width = integer_cast_width(target);
  int bits = width > 0 ? width * 8 : 64;
  long double value = (long double)operand;
  if (target->is_unsigned) {
    long double maximum = bits >= 64
                              ? (long double)ULLONG_MAX
                              : (long double)((UINT64_C(1) << bits) - 1);
    if (value <= -1.0L || value >= maximum + 1.0L) return 0;
    unsigned long long converted = (unsigned long long)value;
    return psx_normalize_integer_constant_cast(
        target, (long long)converted, result);
  }

  long double minimum = bits >= 64
                            ? (long double)LLONG_MIN
                            : -(long double)(UINT64_C(1) << (bits - 1));
  long double maximum = bits >= 64
                            ? (long double)LLONG_MAX
                            : (long double)((UINT64_C(1) << (bits - 1)) - 1);
  if (value <= minimum - 1.0L || value >= maximum + 1.0L) return 0;
  return psx_normalize_integer_constant_cast(
      target, (long long)value, result);
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

static int normalize_integer_conversion(
    psx_integer_conversion_t conversion,
    const ag_data_layout_t *data_layout,
    long long operand, long long *result) {
  if (!conversion.is_integer || !result) return 0;
  int width = psx_integer_conversion_size_for_data_layout(
      conversion, data_layout);
  int bits = width * 8;
  if (bits <= 0 || bits > 64) return 0;
  uint64_t mask = bits == 64
                      ? UINT64_MAX
                      : (UINT64_C(1) << bits) - 1;
  uint64_t normalized = (uint64_t)operand & mask;
  if (!conversion.is_unsigned && bits < 64 &&
      (normalized & (UINT64_C(1) << (bits - 1))))
    normalized |= ~mask;
  *result = (long long)normalized;
  return 1;
}

static int normalize_integer_shape(
    const psx_type_shape_t *shape,
    const ag_data_layout_t *data_layout,
    long long operand, long long *result) {
  if (!shape || !result) return 0;
  if (shape->kind == PSX_TYPE_BOOL) {
    *result = operand != 0;
    return 1;
  }
  return normalize_integer_conversion(
      psx_integer_conversion_from_shape(shape),
      data_layout, operand, result);
}

static int signed_integer_conversion_bounds(
    psx_integer_conversion_t conversion,
    const ag_data_layout_t *data_layout,
    long long *minimum, long long *maximum) {
  if (!conversion.is_integer || conversion.is_unsigned ||
      !minimum || !maximum)
    return 0;
  int bits = psx_integer_conversion_size_for_data_layout(
                 conversion, data_layout) * 8;
  if (bits <= 0 || bits > 64) return 0;
  if (bits == 64) {
    *minimum = LLONG_MIN;
    *maximum = LLONG_MAX;
    return 1;
  }
  *minimum = -(long long)(UINT64_C(1) << (bits - 1));
  *maximum = (long long)((UINT64_C(1) << (bits - 1)) - 1);
  return 1;
}

int psx_apply_typed_integer_constant_unary(
    psx_integer_constant_operation_t operation,
    const psx_type_shape_t *result_type,
    const ag_data_layout_t *data_layout,
    long long operand, long long *result) {
  if (!result) return 0;
  if (operation == PSX_INTEGER_CONSTANT_OP_LOGICAL_NOT) {
    *result = !operand;
    return 1;
  }
  long long normalized = 0;
  if (!normalize_integer_shape(
          result_type, data_layout, operand, &normalized))
    return 0;
  uint64_t bits = (uint64_t)normalized;
  switch (operation) {
    case PSX_INTEGER_CONSTANT_OP_UNARY_PLUS:
      *result = normalized;
      return 1;
    case PSX_INTEGER_CONSTANT_OP_NEGATE: {
      psx_integer_conversion_t conversion =
          psx_integer_conversion_from_shape(result_type);
      long long minimum = 0;
      long long maximum = 0;
      if (!conversion.is_unsigned &&
          (!signed_integer_conversion_bounds(
               conversion, data_layout, &minimum, &maximum) ||
           normalized == minimum))
        return 0;
      return normalize_integer_shape(
          result_type, data_layout,
          (long long)(UINT64_C(0) - bits), result);
    }
    case PSX_INTEGER_CONSTANT_OP_BITWISE_NOT:
      return normalize_integer_shape(
          result_type, data_layout,
          (long long)~bits, result);
    default:
      return 0;
  }
}

static int normalized_binary_conversion(
    const psx_type_shape_t *lhs_type,
    const psx_type_shape_t *rhs_type,
    const ag_data_layout_t *data_layout,
    long long lhs, long long rhs,
    psx_integer_conversion_t *conversion,
    long long *normalized_lhs,
    long long *normalized_rhs) {
  if (!conversion || !normalized_lhs || !normalized_rhs)
    return 0;
  *conversion = psx_usual_integer_conversion_for_data_layout(
      psx_integer_conversion_from_shape(lhs_type),
      psx_integer_conversion_from_shape(rhs_type),
      data_layout);
  return conversion->is_integer &&
         normalize_integer_conversion(
             *conversion, data_layout, lhs, normalized_lhs) &&
         normalize_integer_conversion(
             *conversion, data_layout, rhs, normalized_rhs);
}

int psx_apply_typed_integer_constant_binary(
    psx_integer_constant_operation_t operation,
    const psx_type_shape_t *lhs_type,
    const psx_type_shape_t *rhs_type,
    const psx_type_shape_t *result_type,
    const ag_data_layout_t *data_layout,
    long long lhs, long long rhs, long long *result) {
  if (!result || !ag_data_layout_is_valid(data_layout)) return 0;
  if (operation == PSX_INTEGER_CONSTANT_OP_LOGAND) {
    *result = lhs && rhs;
    return 1;
  }
  if (operation == PSX_INTEGER_CONSTANT_OP_LOGOR) {
    *result = lhs || rhs;
    return 1;
  }
  if (operation == PSX_INTEGER_CONSTANT_OP_COMMA) return 0;

  psx_integer_conversion_t conversion = {0};
  long long normalized_lhs = 0;
  long long normalized_rhs = 0;
  if (operation == PSX_INTEGER_CONSTANT_OP_SHL ||
      operation == PSX_INTEGER_CONSTANT_OP_SHR) {
    conversion = psx_integer_promotion_for_data_layout(
        psx_integer_conversion_from_shape(lhs_type), data_layout);
    psx_integer_conversion_t rhs_conversion =
        psx_integer_promotion_for_data_layout(
            psx_integer_conversion_from_shape(rhs_type), data_layout);
    if (!normalize_integer_conversion(
            conversion, data_layout, lhs, &normalized_lhs) ||
        !normalize_integer_conversion(
            rhs_conversion, data_layout, rhs, &normalized_rhs))
      return 0;
  } else if (!normalized_binary_conversion(
                 lhs_type, rhs_type, data_layout, lhs, rhs,
                 &conversion, &normalized_lhs, &normalized_rhs)) {
    return 0;
  }

  uint64_t unsigned_lhs = (uint64_t)normalized_lhs;
  uint64_t unsigned_rhs = (uint64_t)normalized_rhs;
  long long signed_minimum = 0;
  long long signed_maximum = 0;
  if (!conversion.is_unsigned &&
      !signed_integer_conversion_bounds(
          conversion, data_layout, &signed_minimum, &signed_maximum))
    return 0;
  long long value = 0;
  switch (operation) {
    case PSX_INTEGER_CONSTANT_OP_ADD:
      if (!conversion.is_unsigned &&
          ((normalized_rhs > 0 &&
            normalized_lhs > signed_maximum - normalized_rhs) ||
           (normalized_rhs < 0 &&
            normalized_lhs < signed_minimum - normalized_rhs)))
        return 0;
      value = conversion.is_unsigned
                  ? (long long)(unsigned_lhs + unsigned_rhs)
                  : normalized_lhs + normalized_rhs;
      break;
    case PSX_INTEGER_CONSTANT_OP_SUB:
      if (!conversion.is_unsigned &&
          ((normalized_rhs > 0 &&
            normalized_lhs < signed_minimum + normalized_rhs) ||
           (normalized_rhs < 0 &&
            normalized_lhs > signed_maximum + normalized_rhs)))
        return 0;
      value = conversion.is_unsigned
                  ? (long long)(unsigned_lhs - unsigned_rhs)
                  : normalized_lhs - normalized_rhs;
      break;
    case PSX_INTEGER_CONSTANT_OP_MUL: {
      if (!conversion.is_unsigned && normalized_lhs != 0 &&
          normalized_rhs != 0) {
        if ((normalized_lhs > 0 && normalized_rhs > 0 &&
             normalized_lhs > signed_maximum / normalized_rhs) ||
            (normalized_lhs > 0 && normalized_rhs < 0 &&
             normalized_rhs < signed_minimum / normalized_lhs) ||
            (normalized_lhs < 0 && normalized_rhs > 0 &&
             normalized_lhs < signed_minimum / normalized_rhs) ||
            (normalized_lhs < 0 && normalized_rhs < 0 &&
             normalized_lhs < signed_maximum / normalized_rhs))
          return 0;
      }
      value = conversion.is_unsigned
                  ? (long long)(unsigned_lhs * unsigned_rhs)
                  : normalized_lhs * normalized_rhs;
      break;
    }
    case PSX_INTEGER_CONSTANT_OP_DIV:
      if (!normalized_rhs ||
          (!conversion.is_unsigned &&
           normalized_lhs == signed_minimum &&
           normalized_rhs == -1))
        return 0;
      value = conversion.is_unsigned
                  ? (long long)(unsigned_lhs / unsigned_rhs)
                  : normalized_lhs / normalized_rhs;
      break;
    case PSX_INTEGER_CONSTANT_OP_MOD:
      if (!normalized_rhs ||
          (!conversion.is_unsigned &&
           normalized_lhs == signed_minimum &&
           normalized_rhs == -1))
        return 0;
      value = conversion.is_unsigned
                  ? (long long)(unsigned_lhs % unsigned_rhs)
                  : normalized_lhs % normalized_rhs;
      break;
    case PSX_INTEGER_CONSTANT_OP_BITAND:
      value = (long long)(unsigned_lhs & unsigned_rhs);
      break;
    case PSX_INTEGER_CONSTANT_OP_BITXOR:
      value = (long long)(unsigned_lhs ^ unsigned_rhs);
      break;
    case PSX_INTEGER_CONSTANT_OP_BITOR:
      value = (long long)(unsigned_lhs | unsigned_rhs);
      break;
    case PSX_INTEGER_CONSTANT_OP_SHL: {
      int width =
          psx_integer_conversion_size_for_data_layout(
              conversion, data_layout) * 8;
      if (normalized_rhs < 0 || normalized_rhs >= width) return 0;
      uint64_t unsigned_maximum =
          width == 64
              ? UINT64_MAX
              : (UINT64_C(1) << width) - 1;
      if (!conversion.is_unsigned &&
          (normalized_lhs < 0 ||
           unsigned_lhs >
               (unsigned_maximum >> normalized_rhs)))
        return 0;
      value = (long long)(unsigned_lhs << normalized_rhs);
      break;
    }
    case PSX_INTEGER_CONSTANT_OP_SHR: {
      int width =
          psx_integer_conversion_size_for_data_layout(
              conversion, data_layout) * 8;
      if (normalized_rhs < 0 || normalized_rhs >= width) return 0;
      value = conversion.is_unsigned
                  ? (long long)(unsigned_lhs >> normalized_rhs)
                  : normalized_lhs >> normalized_rhs;
      break;
    }
    case PSX_INTEGER_CONSTANT_OP_EQ:
      *result = conversion.is_unsigned
                    ? unsigned_lhs == unsigned_rhs
                    : normalized_lhs == normalized_rhs;
      return 1;
    case PSX_INTEGER_CONSTANT_OP_NE:
      *result = conversion.is_unsigned
                    ? unsigned_lhs != unsigned_rhs
                    : normalized_lhs != normalized_rhs;
      return 1;
    case PSX_INTEGER_CONSTANT_OP_LT:
      *result = conversion.is_unsigned
                    ? unsigned_lhs < unsigned_rhs
                    : normalized_lhs < normalized_rhs;
      return 1;
    case PSX_INTEGER_CONSTANT_OP_LE:
      *result = conversion.is_unsigned
                    ? unsigned_lhs <= unsigned_rhs
                    : normalized_lhs <= normalized_rhs;
      return 1;
    case PSX_INTEGER_CONSTANT_OP_GT:
      *result = conversion.is_unsigned
                    ? unsigned_lhs > unsigned_rhs
                    : normalized_lhs > normalized_rhs;
      return 1;
    case PSX_INTEGER_CONSTANT_OP_GE:
      *result = conversion.is_unsigned
                    ? unsigned_lhs >= unsigned_rhs
                    : normalized_lhs >= normalized_rhs;
      return 1;
    default:
      return 0;
  }
  return normalize_integer_shape(
      result_type, data_layout, value, result);
}
