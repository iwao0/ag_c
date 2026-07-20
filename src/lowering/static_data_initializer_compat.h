#ifndef LOWERING_STATIC_DATA_INITIALIZER_COMPAT_H
#define LOWERING_STATIC_DATA_INITIALIZER_COMPAT_H

#include "../parser/ast.h"
#include "static_data_initializer.h"

int lower_static_object_initializer(
    psx_lowering_context_t *lowering_context,
    global_var_t *global, node_init_list_t *initializer,
    token_t *fallback_tok);
int lower_static_scalar_array_initializer(
    psx_lowering_context_t *lowering_context,
    global_var_t *global, node_init_list_t *initializer,
    token_t *fallback_tok);
int psx_build_static_aggregate_initializer_plan(
    psx_global_registry_t *global_registry,
    psx_lowering_context_t *lowering_context,
    psx_qual_type_t object_type, node_init_list_t *initializer,
    token_t *fallback_tok,
    psx_static_aggregate_initializer_plan_t *plan);

#endif
