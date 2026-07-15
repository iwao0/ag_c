#include "tokenizer.h"
#include <limits.h>

static tokenizer_context_t default_ctx = {
    .strict_c11_mode = false,
    .enable_trigraphs = true,
    .enable_binary_literals = true,
    .enable_c11_audit_extensions = false,
    .current_token = NULL,
    .user_input = NULL,
    .current_filename = NULL,
    .cursor_hook = NULL,
    .cursor_hook_user_data = NULL,
    .ensure_lookahead_hook = NULL,
    .ensure_lookahead_hook_user_data = NULL,
    .tolerate_untokenizable = false,
    .tolerate_jump_target = NULL,
    .stats_base_chunks = 0,
    .stats_base_reserved_bytes = 0,
    .max_token_len_for_test = (size_t)INT_MAX,
};

static tokenizer_context_t *active_ctx;

tokenizer_context_t *tk_get_default_context(void) {
  return &default_ctx;
}

tokenizer_context_t *tk_context_activate(tokenizer_context_t *ctx) {
  tokenizer_context_t *previous = active_ctx;
  active_ctx = ctx;
  return previous;
}

tokenizer_context_t *tk_context_active(void) {
  return active_ctx ? active_ctx : &default_ctx;
}

void tk_context_init(tokenizer_context_t *ctx) {
  if (!ctx) return;
  *ctx = (tokenizer_context_t){
      .strict_c11_mode = default_ctx.strict_c11_mode,
      .enable_trigraphs = default_ctx.enable_trigraphs,
      .enable_binary_literals = default_ctx.enable_binary_literals,
      .enable_c11_audit_extensions =
          default_ctx.enable_c11_audit_extensions,
      .max_token_len_for_test = (size_t)INT_MAX,
  };
}

void tk_context_dispose(tokenizer_context_t *ctx) {
  if (!ctx) return;
  tk_filename_reset_translation_unit_ctx(ctx);
  *ctx = (tokenizer_context_t){0};
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
  return tk_ctx_get_strict_c11_mode(tk_context_active());
}

void tk_set_strict_c11_mode(bool strict) {
  tk_ctx_set_strict_c11_mode(tk_context_active(), strict);
}

bool tk_get_enable_trigraphs(void) {
  return tk_ctx_get_enable_trigraphs(tk_context_active());
}

void tk_set_enable_trigraphs(bool enable) {
  tk_ctx_set_enable_trigraphs(tk_context_active(), enable);
}

bool tk_get_enable_binary_literals(void) {
  return tk_ctx_get_enable_binary_literals(tk_context_active());
}

void tk_set_enable_binary_literals(bool enable) {
  tk_ctx_set_enable_binary_literals(tk_context_active(), enable);
}

bool tk_get_enable_c11_audit_extensions(void) {
  return tk_ctx_get_enable_c11_audit_extensions(tk_context_active());
}

void tk_set_enable_c11_audit_extensions(bool enable) {
  tk_ctx_set_enable_c11_audit_extensions(tk_context_active(), enable);
}
