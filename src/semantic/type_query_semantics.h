#ifndef SEMANTIC_TYPE_QUERY_SEMANTICS_H
#define SEMANTIC_TYPE_QUERY_SEMANTICS_H

#include "type_identity.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;

typedef enum {
  PSX_TYPE_QUERY_PLAN_CONSTANT = 0,
  PSX_TYPE_QUERY_PLAN_RUNTIME_PRODUCT,
  PSX_TYPE_QUERY_PLAN_RUNTIME_SLOT,
} psx_type_query_plan_kind_t;

typedef struct {
  psx_type_query_plan_kind_t kind;
  psx_qual_type_t result_qual_type;
  psx_qual_type_t queried_qual_type;
  long long constant_factor;
  int runtime_factor_count;
  int runtime_size_slot;
} psx_type_query_plan_t;

int psx_resolve_sizeof_qual_type_plan_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t queried_qual_type,
    int has_constant_override, long long constant_override,
    psx_type_query_plan_t *plan);
int psx_resolve_sizeof_runtime_product_plan_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t queried_qual_type,
    long long constant_factor, int runtime_factor_count,
    psx_type_query_plan_t *plan);
int psx_resolve_sizeof_runtime_slot_plan_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t queried_qual_type, int runtime_size_slot,
    psx_type_query_plan_t *plan);
int psx_resolve_alignof_qual_type_plan_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t queried_qual_type,
    psx_type_query_plan_t *plan);

#endif
