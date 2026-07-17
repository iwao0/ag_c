#ifndef SEMANTIC_EXPRESSION_OPERAND_RESOLUTION_H
#define SEMANTIC_EXPRESSION_OPERAND_RESOLUTION_H

#include "../parser/ast.h"
#include "resolved_node_kind.h"
#include "type_identity.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_type_t psx_type_t;

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

psx_qual_type_t psx_resolve_arithmetic_unary_result_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_resolution_node_kind_t kind,
    psx_qual_type_t operand_type);
psx_qual_type_t psx_resolve_binary_result_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_resolution_node_kind_t kind,
    psx_qual_type_t lhs_type,
    psx_qual_type_t rhs_type);
psx_qual_type_t psx_resolve_conditional_result_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t then_type,
    psx_qual_type_t else_type);
int psx_qual_type_is_scalar_in(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t type);

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
