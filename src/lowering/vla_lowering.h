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
  token_t *diag_tok;
} psx_vla_lowering_request_t;

typedef struct {
  lvar_t *var;
  node_t *init;
} psx_vla_lowering_result_t;

psx_vla_lowering_result_t lower_vla_declaration(
    const psx_vla_lowering_request_t *request);

#endif
