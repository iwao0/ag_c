#include "runtime_context.h"

#include <stdlib.h>
#include <string.h>

static psx_lowering_context_t default_lowering_context;
static psx_lowering_context_t *active_lowering_context;

psx_lowering_context_t *ps_lowering_context_create(void) {
  return calloc(1, sizeof(psx_lowering_context_t));
}

void ps_lowering_context_destroy(psx_lowering_context_t *ctx) {
  if (!ctx || ctx == &default_lowering_context) return;
  if (active_lowering_context == ctx) active_lowering_context = NULL;
  free(ctx);
}

psx_lowering_context_t *ps_lowering_context_activate(
    psx_lowering_context_t *ctx) {
  psx_lowering_context_t *previous = active_lowering_context;
  active_lowering_context = ctx;
  return previous;
}

psx_lowering_context_t *ps_lowering_context_active(void) {
  return active_lowering_context ? active_lowering_context
                                 : &default_lowering_context;
}

void ps_lowering_context_reset_translation_unit(psx_lowering_context_t *ctx) {
  if (!ctx) return;
  frame_layout_reset(&ctx->local_frame_layout);
  memset(ctx->static_local_sequences, 0,
         sizeof(ctx->static_local_sequences));
  ctx->file_scope_compound_sequence = 0;
  ctx->local_compound_sequence = 0;
  ctx->aggregate_cast_temp_sequence = 0;
  ctx->compound_assignment_temp_sequence = 0;
  ctx->member_rvalue_sequence = 0;
}
