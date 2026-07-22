#ifndef LOWERING_STATIC_INITIALIZER_PLAN_H
#define LOWERING_STATIC_INITIALIZER_PLAN_H

typedef struct global_var_t global_var_t;

typedef enum {
  PSX_STATIC_AGGREGATE_INITIALIZER_FAILURE_NONE = 0,
  PSX_STATIC_AGGREGATE_INITIALIZER_FAILURE_NON_CONSTANT,
} psx_static_aggregate_initializer_failure_t;

typedef struct psx_static_aggregate_initializer_plan_t {
  long long *values;
  double *floating_values;
  char **symbols;
  int *symbol_lengths;
  int *union_ordinals;
  int *offsets;
  int value_count;
  int union_ordinal;
  psx_static_aggregate_initializer_failure_t failure;
} psx_static_aggregate_initializer_plan_t;

int psx_apply_static_aggregate_initializer_plan(
    global_var_t *global,
    const psx_static_aggregate_initializer_plan_t *plan);

#endif
