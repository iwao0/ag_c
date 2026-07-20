#include "../../src/tokenizer/tokenizer_test.h"
#include "../../src/tokenizer/test_hook.h"
#include "../../src/tokenizer/allocator.h"
#include "../../src/diag/diag.h"
#include "../../src/source_manager.h"

#include <stdlib.h>

static tokenizer_context_t default_test_context;
static ag_source_manager_t *default_test_sources;
static ag_diagnostic_context_t *default_test_diagnostics;
static tk_allocator_context_t *default_test_allocator;
static tokenizer_context_t *active_test_context;
static int default_test_context_initialized;

static tokenizer_context_t *ensure_default_test_context(void) {
  if (!default_test_context_initialized) {
    default_test_sources = ag_source_manager_create();
    default_test_diagnostics = diag_context_create(default_test_sources);
    default_test_allocator = tk_allocator_context_create(
        default_test_diagnostics);
    if (!tk_context_init(
            &default_test_context, default_test_diagnostics,
            default_test_allocator, default_test_sources)) {
      abort();
    }
    default_test_context_initialized = 1;
  }
  return &default_test_context;
}

tokenizer_context_t *tk_get_default_context(void) {
  return ensure_default_test_context();
}

tokenizer_context_t *tk_context_activate(tokenizer_context_t *ctx) {
  tokenizer_context_t *previous = tk_context_active();
  active_test_context = ctx;
  return previous;
}

tokenizer_context_t *tk_context_active(void) {
  return active_test_context ? active_test_context
                             : ensure_default_test_context();
}

void tk_test_context_shutdown(void) {
  active_test_context = NULL;
  if (default_test_context_initialized) {
    tk_context_dispose(&default_test_context);
    tk_allocator_context_destroy(default_test_allocator);
    diag_context_destroy(default_test_diagnostics);
    ag_source_manager_destroy(default_test_sources);
    default_test_allocator = NULL;
    default_test_diagnostics = NULL;
    default_test_sources = NULL;
    default_test_context_initialized = 0;
  }
}

token_t *tk_get_current_token(void) {
  return tk_get_current_token_ctx(tk_context_active());
}

void tk_set_current_token(token_t *tok) {
  tk_set_current_token_ctx(tk_context_active(), tok);
}

bool tk_consume(char op) {
  return tk_consume_ctx(tk_context_active(), op);
}

bool tk_consume_str(const char *op) {
  return tk_consume_str_ctx(tk_context_active(), op);
}

token_ident_t *tk_consume_ident(void) {
  return tk_consume_ident_ctx(tk_context_active());
}

void tk_expect(char op) {
  tk_expect_ctx(tk_context_active(), op);
}

int tk_expect_number(void) {
  return tk_expect_number_ctx(tk_context_active());
}

bool tk_at_eof(void) {
  return tk_at_eof_ctx(tk_context_active());
}

token_t *tk_tokenize(const char *p) {
  return tk_tokenize_ctx(tk_context_active(), p);
}

void tk_set_strict_c11_mode(bool strict) {
  tk_ctx_set_strict_c11_mode(tk_context_active(), strict);
}

bool tk_get_strict_c11_mode(void) {
  return tk_ctx_get_strict_c11_mode(tk_context_active());
}

void tk_set_enable_trigraphs(bool enable) {
  tk_ctx_set_enable_trigraphs(tk_context_active(), enable);
}

bool tk_get_enable_trigraphs(void) {
  return tk_ctx_get_enable_trigraphs(tk_context_active());
}

void tk_set_enable_binary_literals(bool enable) {
  tk_ctx_set_enable_binary_literals(tk_context_active(), enable);
}

bool tk_get_enable_binary_literals(void) {
  return tk_ctx_get_enable_binary_literals(tk_context_active());
}

void tk_set_enable_c11_audit_extensions(bool enable) {
  tk_ctx_set_enable_c11_audit_extensions(tk_context_active(), enable);
}

bool tk_get_enable_c11_audit_extensions(void) {
  return tk_ctx_get_enable_c11_audit_extensions(tk_context_active());
}

void tk_reset_tokenizer_stats(void) {
  tk_reset_tokenizer_stats_ctx(tk_context_active());
}

tokenizer_stats_t tk_get_tokenizer_stats(void) {
  return tk_get_tokenizer_stats_ctx(tk_context_active());
}

void tk_set_max_token_len_for_test(size_t max_len) {
  tk_set_max_token_len_limit_for_test_ctx(
      tk_context_active(), max_len);
}
