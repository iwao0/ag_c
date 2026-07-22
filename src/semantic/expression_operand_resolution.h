#ifndef SEMANTIC_EXPRESSION_OPERAND_RESOLUTION_H
#define SEMANTIC_EXPRESSION_OPERAND_RESOLUTION_H

#include "../type_system/type_operators.h"
#include "type_identity.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;

typedef enum {
  PSX_DEREF_OPERAND_OK = 0,
  PSX_DEREF_OPERAND_NOT_POINTER,
  PSX_DEREF_OPERAND_VOID_POINTER,
} psx_deref_operand_status_t;

psx_deref_operand_status_t psx_resolve_deref_operand_qual_type_in(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t operand_type);

psx_qual_type_t psx_resolve_arithmetic_unary_result_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_type_arithmetic_unary_op_t operator,
    psx_qual_type_t operand_type);
psx_qual_type_t psx_resolve_logical_not_result_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t operand_type);
psx_qual_type_t psx_resolve_bitwise_not_result_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t operand_type);
psx_qual_type_t psx_resolve_binary_result_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_type_binary_op_t operator,
    psx_qual_type_t lhs_type,
    psx_qual_type_t rhs_type);

typedef enum {
  PSX_BINARY_TYPES_OK = 0,
  PSX_BINARY_TYPES_INVALID,
  PSX_BINARY_OPERANDS_INCOMPATIBLE,
} psx_binary_types_status_t;

typedef struct {
  psx_binary_types_status_t status;
  psx_qual_type_t result_qual_type;
} psx_binary_types_resolution_t;

void psx_resolve_binary_qual_types_in(
    psx_semantic_context_t *semantic_context,
    psx_type_binary_op_t operator,
    psx_qual_type_t lhs_type,
    psx_qual_type_t rhs_type,
    int lhs_is_null_pointer_constant,
    int rhs_is_null_pointer_constant,
    psx_binary_types_resolution_t *resolution);

typedef enum {
  PSX_CONDITIONAL_TYPES_OK = 0,
  PSX_CONDITIONAL_TYPES_INVALID,
  PSX_CONDITIONAL_CONDITION_NOT_SCALAR,
  PSX_CONDITIONAL_BRANCH_TYPES_INCOMPATIBLE,
} psx_conditional_types_status_t;

typedef struct {
  psx_conditional_types_status_t status;
  psx_qual_type_t result_qual_type;
} psx_conditional_types_resolution_t;

void psx_resolve_conditional_qual_types_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t condition_type,
    psx_qual_type_t then_type,
    psx_qual_type_t else_type,
    int then_is_null_pointer_constant,
    int else_is_null_pointer_constant,
    psx_conditional_types_resolution_t *resolution);
psx_qual_type_t psx_resolve_conditional_result_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t then_type,
    psx_qual_type_t else_type,
    int then_is_null_pointer_constant,
    int else_is_null_pointer_constant);
psx_qual_type_t psx_resolve_indirection_result_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t operand_type);
psx_qual_type_t psx_resolve_address_result_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t operand_type);

typedef enum {
  PSX_ADDRESS_OPERAND_NOT_ADDRESSABLE = 0,
  PSX_ADDRESS_OPERAND_OBJECT_LVALUE,
  PSX_ADDRESS_OPERAND_FUNCTION_DESIGNATOR,
} psx_address_operand_category_t;

typedef enum {
  PSX_ADDRESS_OPERAND_OK = 0,
  PSX_ADDRESS_OPERAND_INVALID,
  PSX_ADDRESS_OPERAND_REQUIRES_ADDRESSABLE_VALUE,
  PSX_ADDRESS_OPERAND_IS_BITFIELD,
} psx_address_operand_status_t;

typedef struct {
  psx_address_operand_status_t status;
  psx_qual_type_t result_qual_type;
} psx_address_operand_resolution_t;

void psx_resolve_address_operand_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t operand_type,
    psx_address_operand_category_t category,
    int operand_is_bitfield,
    psx_address_operand_resolution_t *resolution);

typedef enum {
  PSX_INCDEC_OPERAND_OK = 0,
  PSX_INCDEC_OPERAND_CONST,
  PSX_INCDEC_OPERAND_INVALID_TYPE,
  PSX_INCDEC_OPERAND_POINTER_NOT_COMPLETE_OBJECT,
} psx_incdec_operand_status_t;

typedef struct {
  psx_incdec_operand_status_t status;
  psx_qual_type_t result_qual_type;
} psx_incdec_operand_resolution_t;

void psx_resolve_incdec_operand_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t operand_type,
    psx_incdec_operand_resolution_t *resolution);
psx_qual_type_t psx_resolve_incdec_result_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t operand_type);
psx_qual_type_t psx_resolve_value_decay_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t expression_type);
int psx_qual_type_is_scalar_in(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t type);

typedef enum {
  PSX_CONTROL_EXPRESSION_REQUIRES_SCALAR = 0,
  PSX_CONTROL_EXPRESSION_REQUIRES_INTEGER,
} psx_control_expression_requirement_t;

typedef enum {
  PSX_CONTROL_EXPRESSION_OK = 0,
  PSX_CONTROL_EXPRESSION_INVALID,
  PSX_CONTROL_EXPRESSION_NOT_SCALAR,
  PSX_CONTROL_EXPRESSION_NOT_INTEGER,
} psx_control_expression_status_t;

void psx_resolve_control_expression_qual_type_in(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t type,
    psx_control_expression_requirement_t requirement,
    psx_control_expression_status_t *status);

typedef enum {
  PSX_SUBSCRIPT_OPERANDS_OK = 0,
  PSX_SUBSCRIPT_OPERANDS_INVALID,
} psx_subscript_operands_status_t;

typedef struct {
  psx_subscript_operands_status_t status;
  psx_qual_type_t base_qual_type;
  psx_qual_type_t index_qual_type;
  psx_qual_type_t result_qual_type;
  int swapped;
} psx_subscript_qual_types_resolution_t;

void psx_resolve_subscript_qual_types_in(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t left_type,
    psx_qual_type_t right_type,
    psx_subscript_qual_types_resolution_t *resolution);

#endif
