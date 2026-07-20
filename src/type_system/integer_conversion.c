#include "integer_conversion.h"

#include "../target_info.h"

static ag_target_scalar_kind_t scalar_kind_for_rank(int rank) {
  if (rank >= 5) return AG_TARGET_SCALAR_LONG_LONG;
  if (rank == 4) return AG_TARGET_SCALAR_LONG;
  if (rank == 2) return AG_TARGET_SCALAR_SHORT;
  if (rank == 1) return AG_TARGET_SCALAR_CHAR;
  return AG_TARGET_SCALAR_INT;
}

static int integer_size_for_rank(
    int rank, const ag_data_layout_t *data_layout) {
  return ag_data_layout_scalar_size(
      data_layout, scalar_kind_for_rank(rank));
}

psx_integer_conversion_t psx_integer_conversion_from_shape(
    const psx_type_shape_t *shape) {
  psx_integer_conversion_t result = {0};
  if (!shape || (shape->kind != PSX_TYPE_BOOL &&
                 shape->kind != PSX_TYPE_INTEGER))
    return result;
  result.is_integer = 1;
  result.is_unsigned = shape->is_unsigned ? 1 : 0;
  if (shape->kind == PSX_TYPE_BOOL) return result;
  if (shape->is_plain_char) {
    result.rank = 1;
    return result;
  }
  switch (shape->integer_kind) {
    case PSX_INTEGER_KIND_CHAR: result.rank = 1; break;
    case PSX_INTEGER_KIND_SHORT: result.rank = 2; break;
    case PSX_INTEGER_KIND_INT:
    case PSX_INTEGER_KIND_ENUM: result.rank = 3; break;
    case PSX_INTEGER_KIND_LONG: result.rank = 4; break;
    case PSX_INTEGER_KIND_LONG_LONG: result.rank = 5; break;
    default: result.is_integer = 0; break;
  }
  return result;
}

int psx_integer_conversion_size_for_data_layout(
    psx_integer_conversion_t type,
    const ag_data_layout_t *data_layout) {
  if (!type.is_integer || !ag_data_layout_is_valid(data_layout)) return 0;
  return integer_size_for_rank(type.rank, data_layout);
}

psx_integer_conversion_t psx_integer_promotion_for_data_layout(
    psx_integer_conversion_t type,
    const ag_data_layout_t *data_layout) {
  if (!type.is_integer || !ag_data_layout_is_valid(data_layout))
    return (psx_integer_conversion_t){0};
  psx_integer_conversion_t result = type;
  result.rank = type.rank < 3 ? 3 : type.rank;
  result.is_unsigned =
      type.rank >= 3
          ? type.is_unsigned
          : type.is_unsigned &&
                psx_integer_conversion_size_for_data_layout(
                    type, data_layout) >=
                    integer_size_for_rank(3, data_layout);
  return result;
}

psx_integer_conversion_t psx_usual_integer_conversion_for_data_layout(
    psx_integer_conversion_t lhs, psx_integer_conversion_t rhs,
    const ag_data_layout_t *data_layout) {
  lhs = psx_integer_promotion_for_data_layout(lhs, data_layout);
  rhs = psx_integer_promotion_for_data_layout(rhs, data_layout);
  if (!lhs.is_integer || !rhs.is_integer)
    return (psx_integer_conversion_t){0};
  psx_integer_conversion_t result = {
      .rank = lhs.rank > rhs.rank ? lhs.rank : rhs.rank,
      .is_integer = 1,
  };
  if (lhs.is_unsigned == rhs.is_unsigned) {
    result.is_unsigned = lhs.is_unsigned;
    return result;
  }
  psx_integer_conversion_t unsigned_type = lhs.is_unsigned ? lhs : rhs;
  psx_integer_conversion_t signed_type = lhs.is_unsigned ? rhs : lhs;
  if (unsigned_type.rank >= signed_type.rank) {
    result.is_unsigned = 1;
    return result;
  }
  if (integer_size_for_rank(signed_type.rank, data_layout) >
      integer_size_for_rank(unsigned_type.rank, data_layout))
    return result;
  result.rank = signed_type.rank;
  result.is_unsigned = 1;
  return result;
}
