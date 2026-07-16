#ifndef AGC_LOWERING_CAST_LOWERING_H
#define AGC_LOWERING_CAST_LOWERING_H

#include "../parser/ast.h"
#include "../compilation_options.h"

typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_lowering_context_t psx_lowering_context_t;

typedef struct {
  node_t *value;
} psx_source_cast_lowering_plan_t;

node_t *lower_implicit_value_conversion(
                                        psx_lowering_context_t *lowering_context,
                                        node_t *operand,
                                        const psx_type_t *target_type,
                                        token_t *fallback_diag_tok,
                                        const ag_compilation_options_t *options);
int psx_plan_source_cast_expression(
    psx_lowering_context_t *lowering_context,
    psx_local_registry_t *local_registry,
    node_source_cast_t *cast, token_t *fallback_diag_tok,
    const ag_compilation_options_t *options,
    psx_source_cast_lowering_plan_t *plan);
node_t *lower_aggregate_address_expression(
    psx_lowering_context_t *lowering_context, node_t *node);

#endif
