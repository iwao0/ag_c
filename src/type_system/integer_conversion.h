#ifndef TYPE_SYSTEM_INTEGER_CONVERSION_H
#define TYPE_SYSTEM_INTEGER_CONVERSION_H

#include "type_shape.h"

typedef struct ag_data_layout_t ag_data_layout_t;

typedef struct {
  int rank;
  unsigned char is_unsigned;
  unsigned char is_integer;
} psx_integer_conversion_t;

psx_integer_conversion_t psx_integer_conversion_from_shape(
    const psx_type_shape_t *shape);
int psx_integer_conversion_size_for_data_layout(
    psx_integer_conversion_t type,
    const ag_data_layout_t *data_layout);
psx_integer_conversion_t psx_integer_promotion_for_data_layout(
    psx_integer_conversion_t type,
    const ag_data_layout_t *data_layout);
psx_integer_conversion_t psx_usual_integer_conversion_for_data_layout(
    psx_integer_conversion_t lhs, psx_integer_conversion_t rhs,
    const ag_data_layout_t *data_layout);

#endif
