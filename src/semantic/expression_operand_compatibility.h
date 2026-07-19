#ifndef SEMANTIC_EXPRESSION_OPERAND_COMPATIBILITY_H
#define SEMANTIC_EXPRESSION_OPERAND_COMPATIBILITY_H

#include "expression_operand_resolution.h"
#include "resolved_node_kind.h"

typedef struct psx_resolution_store_t psx_resolution_store_t;
typedef struct psx_type_t psx_type_t;

typedef struct {
  psx_subscript_operands_status_t status;
  node_t *base;
  node_t *index;
  int swapped;
} psx_subscript_operands_resolution_t;

psx_deref_operand_status_t psx_resolve_deref_operand(
    const psx_resolution_store_t *store, node_t *operand);
const psx_type_t *psx_resolve_indirection_result_type(
    psx_semantic_context_t *semantic_context, node_t *operand);
const psx_type_t *psx_resolve_arithmetic_unary_result_type(
    psx_semantic_context_t *semantic_context,
    psx_resolution_node_kind_t kind, node_t *operand);
const psx_type_t *psx_resolve_binary_result_type(
    psx_semantic_context_t *semantic_context,
    psx_resolution_node_kind_t kind, node_t *lhs, node_t *rhs);
const psx_type_t *psx_resolve_conditional_result_type(
    psx_semantic_context_t *semantic_context,
    node_t *then_expr, node_t *else_expr);
const psx_type_t *psx_resolve_sequence_result_type(
    psx_semantic_context_t *semantic_context, node_t *value);
const psx_type_t *psx_resolve_address_result_type(
    psx_semantic_context_t *semantic_context, node_t *operand);
const psx_type_t *psx_resolve_incdec_result_type(
    psx_semantic_context_t *semantic_context, node_t *operand);

void psx_resolve_subscript_operands(
    const psx_resolution_store_t *store,
    node_t *left, node_t *right,
    psx_subscript_operands_resolution_t *resolution);

#endif
