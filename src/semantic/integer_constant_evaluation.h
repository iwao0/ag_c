#ifndef SEMANTIC_INTEGER_CONSTANT_EVALUATION_H
#define SEMANTIC_INTEGER_CONSTANT_EVALUATION_H

#include "../parser/syntax_node_kind.h"
#include "../type_system/type_shape.h"

typedef struct ag_data_layout_t ag_data_layout_t;

typedef enum {
  PSX_INTEGER_CONSTANT_OP_INVALID = 0,
  PSX_INTEGER_CONSTANT_OP_UNARY_PLUS,
  PSX_INTEGER_CONSTANT_OP_NEGATE,
  PSX_INTEGER_CONSTANT_OP_LOGICAL_NOT,
  PSX_INTEGER_CONSTANT_OP_BITWISE_NOT,
  PSX_INTEGER_CONSTANT_OP_ADD,
  PSX_INTEGER_CONSTANT_OP_SUB,
  PSX_INTEGER_CONSTANT_OP_MUL,
  PSX_INTEGER_CONSTANT_OP_DIV,
  PSX_INTEGER_CONSTANT_OP_MOD,
  PSX_INTEGER_CONSTANT_OP_SHL,
  PSX_INTEGER_CONSTANT_OP_SHR,
  PSX_INTEGER_CONSTANT_OP_BITAND,
  PSX_INTEGER_CONSTANT_OP_BITXOR,
  PSX_INTEGER_CONSTANT_OP_BITOR,
  PSX_INTEGER_CONSTANT_OP_EQ,
  PSX_INTEGER_CONSTANT_OP_NE,
  PSX_INTEGER_CONSTANT_OP_LT,
  PSX_INTEGER_CONSTANT_OP_LE,
  PSX_INTEGER_CONSTANT_OP_GT,
  PSX_INTEGER_CONSTANT_OP_GE,
  PSX_INTEGER_CONSTANT_OP_LOGAND,
  PSX_INTEGER_CONSTANT_OP_LOGOR,
  PSX_INTEGER_CONSTANT_OP_COMMA,
} psx_integer_constant_operation_t;

int psx_normalize_integer_constant_cast(
    const psx_type_shape_t *target, long long operand, long long *result);
int psx_normalize_floating_constant_cast(
    const psx_type_shape_t *target, double operand, long long *result);
int psx_apply_integer_constant_binary(
    psx_syntax_node_kind_t operation,
    long long lhs, long long rhs, long long *result);
int psx_apply_typed_integer_constant_unary(
    psx_integer_constant_operation_t operation,
    const psx_type_shape_t *result_type,
    const ag_data_layout_t *data_layout,
    long long operand, long long *result);
int psx_apply_typed_integer_constant_binary(
    psx_integer_constant_operation_t operation,
    const psx_type_shape_t *lhs_type,
    const psx_type_shape_t *rhs_type,
    const psx_type_shape_t *result_type,
    const ag_data_layout_t *data_layout,
    long long lhs, long long rhs, long long *result);

#endif
