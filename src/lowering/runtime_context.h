#ifndef LOWERING_RUNTIME_CONTEXT_H
#define LOWERING_RUNTIME_CONTEXT_H

#include "frame_layout.h"
#include "static_local_lowering.h"
#include "../semantic/type_identity.h"
#include "../target_info.h"

typedef struct arena_context_t arena_context_t;
typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;

typedef struct psx_lowering_context_t {
  arena_context_t *arena_context;
  ag_diagnostic_context_t *diagnostic_context;
  ag_target_info_t target;
  const psx_semantic_type_table_t *semantic_types;
  frame_layout_t local_frame_layout;
  int static_local_sequences[PSX_STATIC_LOCAL_KIND_COUNT];
  int file_scope_compound_sequence;
  int local_compound_sequence;
  int aggregate_cast_temp_sequence;
  int compound_assignment_temp_sequence;
  int member_rvalue_sequence;
} psx_lowering_context_t;

psx_lowering_context_t *ps_lowering_context_create(
    arena_context_t *arena_context,
    ag_diagnostic_context_t *diagnostic_context);
void ps_lowering_context_destroy(psx_lowering_context_t *ctx);
arena_context_t *ps_lowering_arena(
    const psx_lowering_context_t *ctx);
ag_diagnostic_context_t *ps_lowering_diagnostics(
    const psx_lowering_context_t *ctx);
void ps_lowering_context_bind_target(
    psx_lowering_context_t *ctx, const ag_target_info_t *target);
void ps_lowering_context_bind_semantic_types(
    psx_lowering_context_t *ctx,
    const psx_semantic_type_table_t *semantic_types);
const psx_semantic_type_table_t *ps_lowering_semantic_types(
    const psx_lowering_context_t *ctx);
psx_type_id_t ps_lowering_type_id(
    const psx_lowering_context_t *ctx, const psx_type_t *type);
const ag_target_info_t *ps_lowering_target(
    const psx_lowering_context_t *ctx);
void ps_lowering_context_reset_translation_unit(psx_lowering_context_t *ctx);

#endif
