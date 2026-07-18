#ifndef AGC_LOWERING_CAST_LOWERING_H
#define AGC_LOWERING_CAST_LOWERING_H

#include "../parser/ast.h"
#include "../compilation_options.h"
#include "../semantic/aggregate_cast_resolution.h"
#include "../type_system/type_ids.h"

typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_lowering_context_t psx_lowering_context_t;
typedef struct token_t token_t;

typedef struct {
  struct lvar_t *temporary;
  psx_qual_type_t member_qual_type;
  int member_offset;
  unsigned char member_bit_width;
  unsigned char member_bit_offset;
  unsigned char member_bit_is_signed;
} psx_aggregate_source_cast_plan_t;

int psx_plan_aggregate_source_cast_resolution(
    psx_lowering_context_t *lowering_context,
    psx_local_registry_t *local_registry,
    const psx_aggregate_cast_resolution_t *resolution,
    psx_aggregate_source_cast_plan_t *plan);

int psx_plan_aggregate_source_cast_qual_types(
    psx_lowering_context_t *lowering_context,
    psx_local_registry_t *local_registry,
    psx_qual_type_t target_qual_type,
    psx_qual_type_t operand_qual_type,
    token_t *diag_tok,
    const ag_compilation_options_t *options,
    psx_aggregate_source_cast_plan_t *plan);

int psx_plan_aggregate_source_cast(
    psx_lowering_context_t *lowering_context,
    psx_local_registry_t *local_registry,
    node_source_cast_t *cast, token_t *fallback_diag_tok,
    const ag_compilation_options_t *options,
    psx_aggregate_source_cast_plan_t *plan);

#endif
