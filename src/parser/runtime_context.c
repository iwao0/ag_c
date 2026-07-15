#include "runtime_context.h"

#include <stdlib.h>

static psx_parser_runtime_context_t default_parser_runtime_context;
static psx_parser_runtime_context_t *active_parser_runtime_context;

psx_parser_runtime_context_t *ps_parser_runtime_context_create(void) {
  return calloc(1, sizeof(psx_parser_runtime_context_t));
}

void ps_parser_runtime_context_destroy(psx_parser_runtime_context_t *ctx) {
  if (!ctx || ctx == &default_parser_runtime_context) return;
  if (active_parser_runtime_context == ctx)
    active_parser_runtime_context = NULL;
  free(ctx);
}

psx_parser_runtime_context_t *ps_parser_runtime_context_activate(
    psx_parser_runtime_context_t *ctx) {
  psx_parser_runtime_context_t *previous = active_parser_runtime_context;
  active_parser_runtime_context = ctx;
  return previous;
}

psx_parser_runtime_context_t *ps_parser_runtime_context_active(void) {
  return active_parser_runtime_context ? active_parser_runtime_context
                                       : &default_parser_runtime_context;
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
