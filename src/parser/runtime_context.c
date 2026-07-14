#include "runtime_context.h"

#include <stdlib.h>

static psx_parser_runtime_context_t default_parser_runtime_context = {
    .enable_size_compatible_nonscalar_cast = true,
    .enable_union_scalar_pointer_cast = true,
    .enable_union_array_member_nonbrace_init = true,
    .enable_struct_scalar_pointer_cast = true,
};
static psx_parser_runtime_context_t *active_parser_runtime_context;

psx_parser_runtime_context_t *ps_parser_runtime_context_create(void) {
  psx_parser_runtime_context_t *ctx = calloc(1, sizeof(*ctx));
  if (!ctx) return NULL;
  ctx->enable_size_compatible_nonscalar_cast =
      default_parser_runtime_context.enable_size_compatible_nonscalar_cast;
  ctx->enable_union_scalar_pointer_cast =
      default_parser_runtime_context.enable_union_scalar_pointer_cast;
  ctx->enable_union_array_member_nonbrace_init =
      default_parser_runtime_context.enable_union_array_member_nonbrace_init;
  ctx->enable_struct_scalar_pointer_cast =
      default_parser_runtime_context.enable_struct_scalar_pointer_cast;
  return ctx;
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
  ctx->string_label_count = 0;
  ctx->float_label_count = 0;
  ctx->pragma_pack_current = 0;
  ctx->pragma_pack_stack_depth = 0;
  ctx->recoverable_syntax_error = 0;
  ctx->function_block_depth = 0;
  ctx->recovery_block_depth = 0;
}
