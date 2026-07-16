#ifndef SEMANTIC_EXPRESSION_OPERAND_RESOLUTION_H
#define SEMANTIC_EXPRESSION_OPERAND_RESOLUTION_H

#include "../parser/ast.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;

typedef enum {
  PSX_DEREF_OPERAND_OK = 0,
  PSX_DEREF_OPERAND_NOT_POINTER,
  PSX_DEREF_OPERAND_VOID_POINTER,
} psx_deref_operand_status_t;

psx_deref_operand_status_t psx_resolve_deref_operand(node_t *operand);
const psx_type_t *psx_resolve_indirection_result_type(
    psx_semantic_context_t *semantic_context, node_t *operand);
const psx_type_t *psx_resolve_arithmetic_unary_result_type(
    psx_semantic_context_t *semantic_context,
    psx_work_node_kind_t kind, node_t *operand);
const psx_type_t *psx_resolve_binary_result_type(
    psx_semantic_context_t *semantic_context,
    psx_work_node_kind_t kind, node_t *lhs, node_t *rhs);
const psx_type_t *psx_resolve_conditional_result_type(
    psx_semantic_context_t *semantic_context,
    node_t *then_expr, node_t *else_expr);
const psx_type_t *psx_resolve_sequence_result_type(
    psx_semantic_context_t *semantic_context, node_t *value);
const psx_type_t *psx_resolve_address_result_type(
    psx_semantic_context_t *semantic_context, node_t *operand);
const psx_type_t *psx_resolve_incdec_result_type(
    psx_semantic_context_t *semantic_context, node_t *operand);

typedef enum {
  PSX_SUBSCRIPT_OPERANDS_OK = 0,
  PSX_SUBSCRIPT_OPERANDS_INVALID,
} psx_subscript_operands_status_t;

typedef struct {
  psx_subscript_operands_status_t status;
  node_t *base;
  node_t *index;
  int swapped;
} psx_subscript_operands_resolution_t;

void psx_resolve_subscript_operands(
    node_t *left, node_t *right,
    psx_subscript_operands_resolution_t *resolution);

#endif
