#include "runtime_context.h"

#include <stdlib.h>

psx_parser_runtime_context_t *ps_parser_runtime_context_create(
    arena_context_t *arena_context) {
  if (!arena_context) return NULL;
  psx_parser_runtime_context_t *ctx = calloc(1, sizeof(*ctx));
  if (ctx) ctx->arena_context = arena_context;
  return ctx;
}

void ps_parser_runtime_context_destroy(psx_parser_runtime_context_t *ctx) {
  if (!ctx) return;
  free(ctx);
}

arena_context_t *ps_parser_runtime_arena(
    const psx_parser_runtime_context_t *ctx) {
  return ctx ? ctx->arena_context : NULL;
}

void ps_parser_runtime_context_reset_translation_unit(
    psx_parser_runtime_context_t *ctx) {
  if (!ctx) return;
  ctx->anonymous_tag_seq = 0;
  ctx->pragma_pack_current = 0;
  ctx->pragma_pack_stack_depth = 0;
  ctx->recoverable_syntax_error = 0;
  ctx->function_block_depth = 0;
  ctx->recovery_block_depth = 0;
}
