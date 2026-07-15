#include "runtime_context.h"

#include <stdlib.h>

psx_parser_runtime_context_t *ps_parser_runtime_context_create(
    arena_context_t *arena_context,
    tokenizer_context_t *tokenizer_context) {
  if (!arena_context || !tokenizer_context) return NULL;
  psx_parser_runtime_context_t *ctx = calloc(1, sizeof(*ctx));
  if (ctx) {
    ctx->arena_context = arena_context;
    ctx->tokenizer_context = tokenizer_context;
  }
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

tokenizer_context_t *ps_parser_runtime_tokenizer(
    const psx_parser_runtime_context_t *ctx) {
  return ctx ? ctx->tokenizer_context : NULL;
}

tokenizer_context_t *ps_parser_runtime_bind_tokenizer(
    psx_parser_runtime_context_t *ctx,
    tokenizer_context_t *tokenizer_context) {
  if (!ctx || !tokenizer_context) return NULL;
  tokenizer_context_t *previous = ctx->tokenizer_context;
  ctx->tokenizer_context = tokenizer_context;
  return previous;
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
