#ifndef LOWERING_PARAMETER_LOWERING_H
#define LOWERING_PARAMETER_LOWERING_H

#include "../parser/lvar_public.h"
#include "../semantic/parameter_declaration_plan.h"
#include "../semantic/parameter_declaration_resolution.h"
#include "../semantic/typed_hir_tree.h"

typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_lowering_context_t psx_lowering_context_t;

typedef struct {
  psx_local_registry_t *local_registry;
  psx_lowering_context_t *lowering_context;
  char *name;
  int name_len;
  const psx_type_t *type;
  token_t *diag_tok;
} psx_parameter_lowering_request_t;

lvar_t *lower_parameter_declaration(
    const psx_parameter_lowering_request_t *request);

typedef struct {
  psx_local_registry_t *local_registry;
  psx_lowering_context_t *lowering_context;
  char *name;
  int name_len;
  const psx_parameter_declaration_resolution_t *resolution;
  const psx_typed_hir_tree_t *const *inner_dimension_expressions;
  token_t *diag_tok;
} psx_resolved_parameter_lowering_request_t;

lvar_t *lower_resolved_parameter_declaration(
    const psx_resolved_parameter_lowering_request_t *request);

#endif
