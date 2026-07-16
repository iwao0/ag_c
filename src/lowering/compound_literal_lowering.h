#ifndef LOWERING_COMPOUND_LITERAL_LOWERING_H
#define LOWERING_COMPOUND_LITERAL_LOWERING_H

#include "../compilation_options.h"
#include "../parser/ast.h"

typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_lowering_context_t psx_lowering_context_t;

typedef struct {
  struct lvar_t *local_object;
  struct global_var_t *global_object;
  node_t *runtime_initialization;
  node_t *direct_value;
  const psx_type_t *object_type;
} psx_compound_literal_storage_plan_t;

int psx_plan_compound_literal_storage_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    node_compound_literal_t *compound,
    const token_t *fallback_diag_tok,
    psx_compound_literal_storage_plan_t *plan);
#endif
