#include "../pragma_pack.h"
#include "runtime_context.h"

void pragma_pack_push(int alignment) {
  psx_parser_runtime_context_t *ctx =
      ps_parser_runtime_context_active();
  if (ctx->pragma_pack_stack_depth < PSX_PRAGMA_PACK_STACK_MAX) {
    ctx->pragma_pack_stack[ctx->pragma_pack_stack_depth++] =
        ctx->pragma_pack_current;
  }
  ctx->pragma_pack_current = alignment;
}

void pragma_pack_pop(void) {
  psx_parser_runtime_context_t *ctx =
      ps_parser_runtime_context_active();
  if (ctx->pragma_pack_stack_depth > 0) {
    ctx->pragma_pack_current =
        ctx->pragma_pack_stack[--ctx->pragma_pack_stack_depth];
  } else {
    ctx->pragma_pack_current = 0;
  }
}

void pragma_pack_set(int alignment) {
  ps_parser_runtime_context_active()->pragma_pack_current = alignment;
}

void pragma_pack_reset_in(psx_parser_runtime_context_t *ctx) {
  if (!ctx) return;
  ctx->pragma_pack_current = 0;
  ctx->pragma_pack_stack_depth = 0;
}

void pragma_pack_reset(void) {
  pragma_pack_reset_in(ps_parser_runtime_context_active());
}

int pragma_pack_current_alignment(void) {
  return ps_parser_runtime_context_active()->pragma_pack_current;
}
