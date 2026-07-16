#ifndef LOWERING_STATIC_DATA_INITIALIZER_H
#define LOWERING_STATIC_DATA_INITIALIZER_H

#include "../parser/ast.h"
#include "../parser/symtab.h"
#include "../semantic/static_initializer_resolution.h"
#include "static_initializer_plan.h"

typedef struct psx_lowering_context_t psx_lowering_context_t;
typedef struct psx_global_registry_t psx_global_registry_t;

typedef struct {
  int type_completed;
  int initialized;
} psx_static_declaration_initializer_result_t;

int lower_resolved_static_initializer(
    psx_global_registry_t *global_registry,
    psx_lowering_context_t *lowering_context, global_var_t *global,
    const psx_static_initializer_resolution_t *resolution,
    token_t *diag_tok,
    psx_static_declaration_initializer_result_t *result);

int lower_static_object_initializer(
    psx_lowering_context_t *lowering_context,
    global_var_t *global, const psx_type_t *type,
    node_init_list_t *initializer, token_t *fallback_tok);
int lower_static_scalar_array_initializer(
    psx_lowering_context_t *lowering_context,
    global_var_t *global, const psx_type_t *type,
    node_init_list_t *initializer, token_t *fallback_tok);
int psx_build_static_aggregate_initializer_plan(
    psx_global_registry_t *global_registry,
    psx_lowering_context_t *lowering_context,
    const psx_type_t *type, node_init_list_t *initializer,
    token_t *fallback_tok,
    psx_static_aggregate_initializer_plan_t *plan);

#endif
