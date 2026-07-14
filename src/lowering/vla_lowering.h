#ifndef LOWERING_VLA_LOWERING_H
#define LOWERING_VLA_LOWERING_H

#include "../parser/ast.h"
#include "../parser/lvar_public.h"

typedef struct psx_local_registry_t psx_local_registry_t;

typedef struct {
  psx_local_registry_t *local_registry;
  char *name;
  int name_len;
  node_t **dimensions;
  long long *const_values;
  unsigned char *is_const;
  int dimension_count;
  const psx_type_t *type;
  int requested_alignment;
  token_t *diag_tok;
} psx_vla_lowering_request_t;

typedef struct {
  lvar_t *var;
  node_t *init;
} psx_vla_lowering_result_t;

psx_vla_lowering_result_t lower_vla_declaration(
    const psx_vla_lowering_request_t *request);

typedef struct {
  psx_local_registry_t *local_registry;
  char *name;
  int name_len;
  node_t *row_dimension;
  const psx_type_t *type;
  int requested_alignment;
  token_t *diag_tok;
} psx_pointer_vla_lowering_request_t;

psx_vla_lowering_result_t lower_pointer_to_vla_declaration(
    const psx_pointer_vla_lowering_request_t *request);

typedef struct {
  int constant;
  char *source_name;
  int source_name_len;
} psx_parameter_vla_dimension_t;

typedef struct {
  psx_local_registry_t *local_registry;
  char *name;
  int name_len;
  psx_parameter_vla_dimension_t *inner_dimensions;
  int inner_dimension_count;
  const psx_type_t *type;
  token_t *diag_tok;
} psx_parameter_vla_lowering_request_t;

typedef struct {
  lvar_t *var;
  lvar_t *stride_storage;
} psx_parameter_vla_lowering_result_t;

psx_parameter_vla_lowering_result_t lower_parameter_vla_declaration(
    const psx_parameter_vla_lowering_request_t *request);

#endif
