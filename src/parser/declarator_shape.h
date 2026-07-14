#ifndef PARSER_DECLARATOR_SHAPE_H
#define PARSER_DECLARATOR_SHAPE_H

#include "type_fwd.h"

typedef enum {
  PSX_DECL_OP_POINTER = 0,
  PSX_DECL_OP_ARRAY,
  PSX_DECL_OP_FUNCTION,
} psx_declarator_op_kind_t;

typedef struct {
  psx_declarator_op_kind_t kind;
  int array_len;
  unsigned int is_const_qualified : 1;
  unsigned int is_volatile_qualified : 1;
  unsigned int is_incomplete_array : 1;
  unsigned int is_vla_array : 1;
  unsigned int has_canonical_function_params : 1;
  const psx_type_t **function_param_types;
  int function_param_count;
  int function_is_variadic;
} psx_declarator_op_t;

typedef struct {
  psx_declarator_op_t *ops;
  int count;
  int capacity;
} psx_declarator_shape_t;

/* Operators are stored from the declared identifier outward. Applying them in
 * reverse order preserves parenthesized declarator binding. */

#endif
