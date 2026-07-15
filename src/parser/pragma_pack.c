#include "../pragma_pack.h"
#include "runtime_context.h"

void pragma_pack_push_in(
    psx_parser_runtime_context_t *ctx, int alignment) {
  if (!ctx) return;
  if (ctx->pragma_pack_stack_depth < PSX_PRAGMA_PACK_STACK_MAX) {
    ctx->pragma_pack_stack[ctx->pragma_pack_stack_depth++] =
        ctx->pragma_pack_current;
  }
  ctx->pragma_pack_current = alignment;
}

void pragma_pack_pop_in(psx_parser_runtime_context_t *ctx) {
  if (!ctx) return;
  if (ctx->pragma_pack_stack_depth > 0) {
    ctx->pragma_pack_current =
        ctx->pragma_pack_stack[--ctx->pragma_pack_stack_depth];
  } else {
    ctx->pragma_pack_current = 0;
  }
}

void pragma_pack_set_in(
    psx_parser_runtime_context_t *ctx, int alignment) {
  if (!ctx) return;
  ctx->pragma_pack_current = alignment;
}

void pragma_pack_reset_in(psx_parser_runtime_context_t *ctx) {
  if (!ctx) return;
  ctx->pragma_pack_current = 0;
  ctx->pragma_pack_stack_depth = 0;
}

int pragma_pack_current_alignment_in(
    const psx_parser_runtime_context_t *ctx) {
  return ctx ? ctx->pragma_pack_current : 0;
}
