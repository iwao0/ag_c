#include "runtime_context.h"
#include "../target_info.h"

#include <stdlib.h>
#include <string.h>

psx_lowering_context_t *ps_lowering_context_create(
    arena_context_t *arena_context,
    ag_diagnostic_context_t *diagnostic_context) {
  if (!arena_context || !diagnostic_context) return NULL;
  psx_lowering_context_t *ctx = calloc(1, sizeof(*ctx));
  if (ctx) {
    ctx->arena_context = arena_context;
    ctx->diagnostic_context = diagnostic_context;
    ctx->target = ag_target_info_host();
  }
  return ctx;
}

void ps_lowering_context_destroy(psx_lowering_context_t *ctx) {
  if (!ctx) return;
  free(ctx);
}

void ps_lowering_context_bind_target(
    psx_lowering_context_t *ctx, const ag_target_info_t *target) {
  if (!ctx) return;
  ctx->target = target ? *target : ag_target_info_host();
  ctx->target.pointer_size = ag_target_info_pointer_size(&ctx->target);
}

const ag_target_info_t *ps_lowering_target(
    const psx_lowering_context_t *ctx) {
  return ctx ? &ctx->target : NULL;
}

arena_context_t *ps_lowering_arena(
    const psx_lowering_context_t *ctx) {
  return ctx ? ctx->arena_context : NULL;
}

ag_diagnostic_context_t *ps_lowering_diagnostics(
    const psx_lowering_context_t *ctx) {
  return ctx ? ctx->diagnostic_context : NULL;
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
