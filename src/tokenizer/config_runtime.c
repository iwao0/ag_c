#include "tokenizer.h"
#include "allocator.h"

#include <limits.h>

static int init_context(
    tokenizer_context_t *ctx,
    ag_diagnostic_context_t *diagnostic_context,
    tk_allocator_context_t *allocator_context) {
  if (!ctx) return 0;
  *ctx = (tokenizer_context_t){0};
  if (!diagnostic_context) return 0;
  *ctx = (tokenizer_context_t){
      .allocator_context = allocator_context,
      .diagnostic_context = diagnostic_context,
      .enable_trigraphs = true,
      .enable_binary_literals = true,
      .max_token_len_for_test = (size_t)INT_MAX,
  };
  return 1;
}

int tk_context_init(
    tokenizer_context_t *ctx,
    ag_diagnostic_context_t *diagnostic_context,
    tk_allocator_context_t *allocator_context) {
  if (!allocator_context ||
      tk_allocator_diagnostics(allocator_context) != diagnostic_context) {
    if (ctx) *ctx = (tokenizer_context_t){0};
    return 0;
  }
  return init_context(ctx, diagnostic_context, allocator_context);
}

int tk_cursor_context_init(
    tokenizer_context_t *ctx,
    ag_diagnostic_context_t *diagnostic_context) {
  return init_context(ctx, diagnostic_context, NULL);
}

ag_diagnostic_context_t *tk_context_diagnostics(
    const tokenizer_context_t *ctx) {
  return ctx ? ctx->diagnostic_context : NULL;
}

tk_allocator_context_t *tk_context_allocator(
    const tokenizer_context_t *ctx) {
  return ctx ? ctx->allocator_context : NULL;
}

void tk_context_dispose(tokenizer_context_t *ctx) {
  if (!ctx) return;
  tk_filename_reset_translation_unit_ctx(ctx);
  *ctx = (tokenizer_context_t){0};
}

bool tk_ctx_get_strict_c11_mode(const tokenizer_context_t *ctx) {
  return ctx && ctx->strict_c11_mode;
}

void tk_ctx_set_strict_c11_mode(tokenizer_context_t *ctx, bool strict) {
  if (ctx) ctx->strict_c11_mode = strict;
}

bool tk_ctx_get_enable_trigraphs(const tokenizer_context_t *ctx) {
  return ctx && ctx->enable_trigraphs;
}

void tk_ctx_set_enable_trigraphs(tokenizer_context_t *ctx, bool enable) {
  if (ctx) ctx->enable_trigraphs = enable;
}

bool tk_ctx_get_enable_binary_literals(const tokenizer_context_t *ctx) {
  return ctx && ctx->enable_binary_literals;
}

void tk_ctx_set_enable_binary_literals(tokenizer_context_t *ctx, bool enable) {
  if (ctx) ctx->enable_binary_literals = enable;
}

bool tk_ctx_get_enable_c11_audit_extensions(const tokenizer_context_t *ctx) {
  return ctx && ctx->enable_c11_audit_extensions;
}

void tk_ctx_set_enable_c11_audit_extensions(
    tokenizer_context_t *ctx, bool enable) {
  if (ctx) ctx->enable_c11_audit_extensions = enable;
}
