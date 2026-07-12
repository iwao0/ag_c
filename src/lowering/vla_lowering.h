#ifndef LOWERING_VLA_LOWERING_H
#define LOWERING_VLA_LOWERING_H

#include "../parser/ast.h"
#include "../parser/lvar_public.h"

#define PSX_VLA_MAX_DIMS 8

typedef struct {
  char *name;
  int name_len;
  int element_size;
  node_t *dimensions[PSX_VLA_MAX_DIMS];
  long long const_values[PSX_VLA_MAX_DIMS];
  unsigned char is_const[PSX_VLA_MAX_DIMS];
  int dimension_count;
  const psx_type_t *type;
  int requested_alignment;
  token_t *diag_tok;
} psx_vla_lowering_request_t;

typedef struct {
  lvar_t *var;
  node_t *init;
  int type_attached;
} psx_vla_lowering_result_t;

psx_vla_lowering_result_t lower_vla_declaration(
    const psx_vla_lowering_request_t *request);

typedef struct {
  char *name;
  int name_len;
  int element_size;
  node_t *row_dimension;
  const psx_type_t *type;
  int requested_alignment;
  token_t *diag_tok;
} psx_pointer_vla_lowering_request_t;

psx_vla_lowering_result_t lower_pointer_to_vla_declaration(
    const psx_pointer_vla_lowering_request_t *request);

#define PSX_VLA_PARAM_MAX_INNER_DIMS 7

typedef struct {
  int constant;
  char *source_name;
  int source_name_len;
} psx_parameter_vla_dimension_t;

typedef struct {
  char *name;
  int name_len;
  int element_size;
  psx_parameter_vla_dimension_t
      inner_dimensions[PSX_VLA_PARAM_MAX_INNER_DIMS];
  int inner_dimension_count;
  const psx_type_t *type;
  token_t *diag_tok;
} psx_parameter_vla_lowering_request_t;

typedef struct {
  lvar_t *var;
  lvar_t *stride_storage;
  int type_attached;
} psx_parameter_vla_lowering_result_t;

psx_parameter_vla_lowering_result_t lower_parameter_vla_declaration(
    const psx_parameter_vla_lowering_request_t *request);

#endif
