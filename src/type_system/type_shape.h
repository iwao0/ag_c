#ifndef TYPE_SYSTEM_TYPE_SHAPE_H
#define TYPE_SYSTEM_TYPE_SHAPE_H

#include "type_ids.h"

typedef enum {
  PSX_TYPE_INVALID = 0,
  PSX_TYPE_VOID,
  PSX_TYPE_BOOL,
  PSX_TYPE_INTEGER,
  PSX_TYPE_FLOAT,
  PSX_TYPE_POINTER,
  PSX_TYPE_ARRAY,
  PSX_TYPE_FUNCTION,
  PSX_TYPE_STRUCT,
  PSX_TYPE_UNION,
  PSX_TYPE_COMPLEX,
} psx_type_kind_t;

typedef enum {
  PSX_INTEGER_KIND_NONE = 0,
  PSX_INTEGER_KIND_BOOL,
  PSX_INTEGER_KIND_CHAR,
  PSX_INTEGER_KIND_SHORT,
  PSX_INTEGER_KIND_INT,
  PSX_INTEGER_KIND_LONG,
  PSX_INTEGER_KIND_LONG_LONG,
  PSX_INTEGER_KIND_ENUM,
} psx_integer_kind_t;

typedef enum {
  PSX_FLOATING_KIND_NONE = 0,
  PSX_FLOATING_KIND_FLOAT,
  PSX_FLOATING_KIND_DOUBLE,
  PSX_FLOATING_KIND_LONG_DOUBLE,
} psx_floating_kind_t;

/* Immutable node identity for an interned C type. Recursive relations are
 * stored as TypeId/QualType edges by the semantic type table. Qualifiers and
 * target layout are intentionally absent. */
typedef struct {
  psx_type_kind_t kind;
  int array_len;
  psx_integer_kind_t integer_kind;
  psx_floating_kind_t floating_kind;
  psx_record_id_t record_id;
  const char *record_tag_name;
  int record_tag_length;
  const char *enum_tag_name;
  int enum_tag_length;
  psx_decl_id_t enum_decl_id;
  int parameter_count;
  unsigned char is_unsigned;
  unsigned char is_plain_char;
  unsigned char is_vla;
  unsigned char has_function_prototype;
  unsigned char is_variadic_function;
} psx_type_shape_t;

static inline int psx_type_kind_is_scalar(psx_type_kind_t kind) {
  return kind == PSX_TYPE_BOOL || kind == PSX_TYPE_INTEGER ||
         kind == PSX_TYPE_FLOAT || kind == PSX_TYPE_COMPLEX ||
         kind == PSX_TYPE_POINTER;
}

static inline int psx_type_kind_is_aggregate(psx_type_kind_t kind) {
  return kind == PSX_TYPE_STRUCT || kind == PSX_TYPE_UNION;
}

#endif
