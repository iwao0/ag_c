#ifndef LOWERING_PARAMETER_LOWERING_H
#define LOWERING_PARAMETER_LOWERING_H

#include "../parser/lvar_public.h"
#include "../semantic/parameter_declaration_resolution.h"
#include "parameter_storage_plan.h"

typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_lowering_context_t psx_lowering_context_t;

typedef struct {
  psx_local_registry_t *local_registry;
  psx_lowering_context_t *lowering_context;
  const psx_semantic_expression_table_t *semantic_expressions;
  char *name;
  int name_len;
  const psx_parameter_declaration_resolution_t *resolution;
  token_t *diag_tok;
} psx_resolved_parameter_lowering_request_t;

lvar_t *lower_resolved_parameter_declaration(
    const psx_resolved_parameter_lowering_request_t *request);

#endif
