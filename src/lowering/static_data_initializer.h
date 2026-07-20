#ifndef LOWERING_STATIC_DATA_INITIALIZER_H
#define LOWERING_STATIC_DATA_INITIALIZER_H

#include "../hir/hir.h"
#include "../semantic/static_initializer_classification.h"
#include "static_initializer_plan.h"

typedef struct psx_lowering_context_t psx_lowering_context_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct global_var_t global_var_t;

typedef struct {
  int type_completed;
  int initialized;
} psx_static_declaration_initializer_result_t;

typedef struct psx_static_initializer_lowering_input_t {
  const psx_static_initializer_resolution_t *resolution;
  const psx_static_aggregate_initializer_plan_t *aggregate_plan;
  const psx_hir_module_t *initializer_hir;
  psx_hir_node_id_t initializer_hir_root;
} psx_static_initializer_lowering_input_t;

int lower_resolved_static_initializer(
    psx_global_registry_t *global_registry,
    psx_lowering_context_t *lowering_context, global_var_t *global,
    const psx_static_initializer_lowering_input_t *initializer,
    psx_static_declaration_initializer_result_t *result);

#endif
