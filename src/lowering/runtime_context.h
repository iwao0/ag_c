#ifndef LOWERING_RUNTIME_CONTEXT_H
#define LOWERING_RUNTIME_CONTEXT_H

#include "frame_layout.h"
#include "static_local_lowering.h"

typedef struct arena_context_t arena_context_t;

typedef struct psx_lowering_context_t {
  arena_context_t *arena_context;
  frame_layout_t local_frame_layout;
  int static_local_sequences[PSX_STATIC_LOCAL_KIND_COUNT];
  int file_scope_compound_sequence;
  int local_compound_sequence;
  int aggregate_cast_temp_sequence;
  int compound_assignment_temp_sequence;
  int member_rvalue_sequence;
} psx_lowering_context_t;

psx_lowering_context_t *ps_lowering_context_create(
    arena_context_t *arena_context);
void ps_lowering_context_destroy(psx_lowering_context_t *ctx);
arena_context_t *ps_lowering_arena(
    const psx_lowering_context_t *ctx);
void ps_lowering_context_reset_translation_unit(psx_lowering_context_t *ctx);

#endif
