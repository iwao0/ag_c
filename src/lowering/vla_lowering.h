#ifndef LOWERING_VLA_LOWERING_H
#define LOWERING_VLA_LOWERING_H

#include "../parser/ast.h"
#include "../parser/lvar_public.h"
#include "../semantic/vla_runtime_plan.h"

typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_lowering_context_t psx_lowering_context_t;

typedef struct {
  psx_local_registry_t *local_registry;
  psx_lowering_context_t *lowering_context;
  char *name;
  int name_len;
  psx_vla_runtime_dimension_t *dimensions;
  psx_qual_type_t constant_qual_type;
  int dimension_count;
  psx_qual_type_t type;
  int requested_alignment;
  token_t *diag_tok;
} psx_vla_lowering_request_t;

typedef struct {
  lvar_t *var;
  node_t *init;
  struct psx_vla_runtime_plan_t *runtime_plan;
} psx_vla_lowering_result_t;

psx_vla_lowering_result_t lower_vla_declaration(
    const psx_vla_lowering_request_t *request);
psx_vla_lowering_result_t lower_vla_declaration_plan(
    const psx_vla_lowering_request_t *request);

typedef struct {
  psx_local_registry_t *local_registry;
  psx_lowering_context_t *lowering_context;
  char *name;
  int name_len;
  psx_semantic_expr_id_t row_dimension_id;
  psx_qual_type_t type;
  int requested_alignment;
  token_t *diag_tok;
} psx_pointer_vla_lowering_request_t;

psx_vla_lowering_result_t lower_pointer_to_vla_declaration(
    const psx_pointer_vla_lowering_request_t *request);
psx_vla_lowering_result_t lower_pointer_to_vla_declaration_plan(
    const psx_pointer_vla_lowering_request_t *request);

typedef struct {
  psx_semantic_expr_id_t expression_id;
  long long constant_value;
  int is_constant;
} psx_parameter_vla_dimension_t;

typedef struct {
  psx_local_registry_t *local_registry;
  psx_lowering_context_t *lowering_context;
  const psx_semantic_expression_table_t *semantic_expressions;
  char *name;
  int name_len;
  psx_parameter_vla_dimension_t *inner_dimensions;
  int inner_dimension_count;
  psx_qual_type_t type;
  psx_qual_type_t stride_storage_type;
  token_t *diag_tok;
} psx_parameter_vla_lowering_request_t;

typedef struct {
  lvar_t *var;
  lvar_t *stride_storage;
} psx_parameter_vla_lowering_result_t;

psx_parameter_vla_lowering_result_t lower_parameter_vla_declaration(
    const psx_parameter_vla_lowering_request_t *request);

#endif
