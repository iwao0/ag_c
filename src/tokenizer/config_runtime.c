#include "tokenizer.h"

#include "allocator.h"
#include "../diag/diag.h"

#include <limits.h>

void tk_context_init(tokenizer_context_t *ctx) {
  if (!ctx) return;
  ag_diagnostic_context_t *diagnostic_context = diag_context_create();
  tk_allocator_context_t *allocator_context =
      tk_allocator_context_create(diagnostic_context);
  *ctx = (tokenizer_context_t){
      .allocator_context = allocator_context,
      .diagnostic_context = diagnostic_context,
      .owns_allocator_context = allocator_context != NULL,
      .owns_diagnostic_context = diagnostic_context != NULL,
      .enable_trigraphs = true,
      .enable_binary_literals = true,
      .max_token_len_for_test = (size_t)INT_MAX,
  };
}

void tk_context_bind_diagnostic_context(
    tokenizer_context_t *ctx,
    ag_diagnostic_context_t *diagnostic_context) {
  if (!ctx || ctx->diagnostic_context == diagnostic_context) return;
  if (ctx->owns_diagnostic_context)
    diag_context_destroy(ctx->diagnostic_context);
  ctx->diagnostic_context = diagnostic_context;
  ctx->owns_diagnostic_context = false;
  tk_allocator_bind_diagnostic_context_in(
      ctx->allocator_context, diagnostic_context);
}

ag_diagnostic_context_t *tk_context_diagnostics(
    const tokenizer_context_t *ctx) {
  return ctx ? ctx->diagnostic_context : NULL;
}

void tk_context_set_allocator(
    tokenizer_context_t *ctx, tk_allocator_context_t *allocator_context) {
  if (!ctx || ctx->allocator_context == allocator_context) return;
  if (ctx->owns_allocator_context)
    tk_allocator_context_destroy(ctx->allocator_context);
  ctx->allocator_context = allocator_context;
  ctx->owns_allocator_context = false;
  tk_allocator_bind_diagnostic_context_in(
      allocator_context, ctx->diagnostic_context);
}

tk_allocator_context_t *tk_context_allocator(
    const tokenizer_context_t *ctx) {
  return ctx ? ctx->allocator_context : NULL;
}

void tk_context_dispose(tokenizer_context_t *ctx) {
  if (!ctx) return;
  tk_filename_reset_translation_unit_ctx(ctx);
  if (ctx->owns_allocator_context)
    tk_allocator_context_destroy(ctx->allocator_context);
  if (ctx->owns_diagnostic_context)
    diag_context_destroy(ctx->diagnostic_context);
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
