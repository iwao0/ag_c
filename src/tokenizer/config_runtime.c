#include "tokenizer.h"

static tokenizer_context_t default_ctx = {
    .strict_c11_mode = false,
    .enable_trigraphs = true,
    .enable_binary_literals = true,
    .enable_c11_audit_extensions = false,
    .current_token = NULL,
    .user_input = NULL,
    .current_filename = NULL,
};

tokenizer_context_t *tk_get_default_context(void) {
  return &default_ctx;
}

void tk_context_init(tokenizer_context_t *ctx) {
  if (!ctx) return;
  *ctx = default_ctx;
}

bool tk_ctx_get_strict_c11_mode(const tokenizer_context_t *ctx) {
  return ctx ? ctx->strict_c11_mode : default_ctx.strict_c11_mode;
}

void tk_ctx_set_strict_c11_mode(tokenizer_context_t *ctx, bool strict) {
  if (!ctx) return;
  ctx->strict_c11_mode = strict;
}

bool tk_ctx_get_enable_trigraphs(const tokenizer_context_t *ctx) {
  return ctx ? ctx->enable_trigraphs : default_ctx.enable_trigraphs;
}

void tk_ctx_set_enable_trigraphs(tokenizer_context_t *ctx, bool enable) {
  if (!ctx) return;
  ctx->enable_trigraphs = enable;
}

bool tk_ctx_get_enable_binary_literals(const tokenizer_context_t *ctx) {
  return ctx ? ctx->enable_binary_literals : default_ctx.enable_binary_literals;
}

void tk_ctx_set_enable_binary_literals(tokenizer_context_t *ctx, bool enable) {
  if (!ctx) return;
  ctx->enable_binary_literals = enable;
}

bool tk_ctx_get_enable_c11_audit_extensions(const tokenizer_context_t *ctx) {
  return ctx ? ctx->enable_c11_audit_extensions : default_ctx.enable_c11_audit_extensions;
}

void tk_ctx_set_enable_c11_audit_extensions(tokenizer_context_t *ctx, bool enable) {
  if (!ctx) return;
  ctx->enable_c11_audit_extensions = enable;
}

bool tk_get_strict_c11_mode(void) {
  return tk_ctx_get_strict_c11_mode(&default_ctx);
}

void tk_set_strict_c11_mode(bool strict) {
  tk_ctx_set_strict_c11_mode(&default_ctx, strict);
}

bool tk_get_enable_trigraphs(void) {
  return tk_ctx_get_enable_trigraphs(&default_ctx);
}

void tk_set_enable_trigraphs(bool enable) {
  tk_ctx_set_enable_trigraphs(&default_ctx, enable);
}

bool tk_get_enable_binary_literals(void) {
  return tk_ctx_get_enable_binary_literals(&default_ctx);
}

void tk_set_enable_binary_literals(bool enable) {
  tk_ctx_set_enable_binary_literals(&default_ctx, enable);
}

bool tk_get_enable_c11_audit_extensions(void) {
  return tk_ctx_get_enable_c11_audit_extensions(&default_ctx);
}

void tk_set_enable_c11_audit_extensions(bool enable) {
  tk_ctx_set_enable_c11_audit_extensions(&default_ctx, enable);
}
