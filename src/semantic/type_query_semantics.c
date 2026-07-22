#include "type_query_semantics.h"

#include <string.h>

#include "../parser/semantic_ctx.h"
#include "../type_layout.h"

static int qual_type_size(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t qual_type) {
  return psx_qual_type_layout_sizeof(
      ps_ctx_semantic_type_table_in(semantic_context),
      ps_ctx_record_layout_table_in(semantic_context), qual_type,
      ps_ctx_data_layout(semantic_context));
}

static int qual_type_alignment(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t qual_type) {
  return psx_qual_type_layout_alignof(
      ps_ctx_semantic_type_table_in(semantic_context),
      ps_ctx_record_layout_table_in(semantic_context), qual_type,
      ps_ctx_data_layout(semantic_context));
}

static int begin_plan(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t queried_qual_type,
    psx_type_query_plan_t *plan) {
  if (plan) memset(plan, 0, sizeof(*plan));
  psx_type_shape_t shape = {0};
  if (!semantic_context || !plan ||
      queried_qual_type.type_id == PSX_TYPE_ID_INVALID ||
      !psx_semantic_type_table_describe(
          ps_ctx_semantic_type_table_in(semantic_context),
          queried_qual_type.type_id, &shape))
    return 0;
  psx_qual_type_t result = ps_ctx_intern_integer_qual_type_in(
      semantic_context, PSX_INTEGER_KIND_LONG, 1, 0);
  if (result.type_id == PSX_TYPE_ID_INVALID) return 0;
  plan->result_qual_type = result;
  plan->queried_qual_type = queried_qual_type;
  return 1;
}

int psx_resolve_sizeof_qual_type_plan_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t queried_qual_type,
    int has_constant_override, long long constant_override,
    psx_type_query_plan_t *plan) {
  if (!begin_plan(semantic_context, queried_qual_type, plan)) return 0;
  psx_type_shape_t shape = {0};
  if (!psx_semantic_type_table_describe(
          ps_ctx_semantic_type_table_in(semantic_context),
          queried_qual_type.type_id, &shape))
    return 0;
  long long size = has_constant_override
                       ? constant_override
                       : qual_type_size(
                             semantic_context, queried_qual_type);
  if (!has_constant_override && shape.kind == PSX_TYPE_VOID) size = 1;
  if (size < 0 || (!has_constant_override && size == 0)) return 0;
  plan->kind = PSX_TYPE_QUERY_PLAN_CONSTANT;
  plan->constant_factor = size;
  return 1;
}

int psx_resolve_sizeof_runtime_product_plan_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t queried_qual_type,
    long long constant_factor, int runtime_factor_count,
    psx_type_query_plan_t *plan) {
  if (constant_factor <= 0 || runtime_factor_count <= 0 ||
      !begin_plan(semantic_context, queried_qual_type, plan))
    return 0;
  plan->kind = PSX_TYPE_QUERY_PLAN_RUNTIME_PRODUCT;
  plan->constant_factor = constant_factor;
  plan->runtime_factor_count = runtime_factor_count;
  return 1;
}

int psx_resolve_sizeof_runtime_slot_plan_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t queried_qual_type, int runtime_size_slot,
    psx_type_query_plan_t *plan) {
  if (runtime_size_slot == 0 ||
      !begin_plan(semantic_context, queried_qual_type, plan))
    return 0;
  plan->kind = PSX_TYPE_QUERY_PLAN_RUNTIME_SLOT;
  plan->runtime_size_slot = runtime_size_slot;
  return 1;
}

int psx_resolve_alignof_qual_type_plan_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t queried_qual_type,
    psx_type_query_plan_t *plan) {
  if (!begin_plan(semantic_context, queried_qual_type, plan)) return 0;
  int alignment = qual_type_alignment(
      semantic_context, queried_qual_type);
  if (alignment <= 0) return 0;
  plan->kind = PSX_TYPE_QUERY_PLAN_CONSTANT;
  plan->constant_factor = alignment;
  return 1;
}
