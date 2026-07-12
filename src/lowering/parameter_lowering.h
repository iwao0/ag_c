#ifndef LOWERING_PARAMETER_LOWERING_H
#define LOWERING_PARAMETER_LOWERING_H

#include "../parser/lvar_public.h"
#include "../semantic/parameter_declaration_plan.h"
#include "../semantic/parameter_declaration_resolution.h"

typedef struct {
  char *name;
  int name_len;
  const psx_type_t *type;
} psx_parameter_lowering_request_t;

typedef struct {
  lvar_t *var;
  psx_parameter_storage_plan_t storage;
  int type_attached;
} psx_parameter_lowering_result_t;

int lower_parameter_declaration(
    const psx_parameter_lowering_request_t *request,
    psx_parameter_lowering_result_t *result);

typedef struct {
  char *name;
  int name_len;
  const psx_parameter_declaration_resolution_t *resolution;
  token_t *diag_tok;
} psx_resolved_parameter_lowering_request_t;

int lower_resolved_parameter_declaration(
    const psx_resolved_parameter_lowering_request_t *request,
    psx_parameter_lowering_result_t *result);

#endif
